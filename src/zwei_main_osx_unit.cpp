/**
   TODO(nicolas): FEATURE search timeline ranges [date-start,date-end)
   TODO(nicolas): FEATURE on OSX, use open command or similar to open a mail in
   Mail.app
*/

#include "zwei_inlines.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_iobuffer_inlines.hpp"
#include "zwei_logging.hpp"

#include "zwei_app.hpp"

#include "algos.hpp"

#include "../modules/uu.spdr/include/spdr/spdr.hh"

#include <cstdarg>
#include <cstdio>

zw_global SPDR_Context *global_spdr = nullptr;

#include <CommonCrypto/CommonDigest.h>
#include <limits>

struct Sha1 {
        uint8_t digest[20];
};

MayFail<Sha1> sha1(struct BufferRange *range)
{
        MayFail<Sha1> result;
        using algos::sink;

        size_t const MAX_CHUNK_SIZE = std::numeric_limits<CC_LONG>::max();
        auto chunk_size = [MAX_CHUNK_SIZE](size_t size) {
                return static_cast<CC_LONG>(
                    size < MAX_CHUNK_SIZE ? size : MAX_CHUNK_SIZE);
        };

        CC_SHA1_CTX ctx;
        zw_assert(1 == CC_SHA1_Init(&ctx), "CC_SHA1_Init");

        while (range->error == BR_NoError) {
                while (range->cursor < range->end) {
                        auto size = chunk_size(range->end - range->cursor);
                        zw_assert(1 ==
                                      CC_SHA1_Update(&ctx, range->cursor, size),
                                  "CC_SHA1_Update");
                        range->cursor += size;
                }
                range->next(range);
        }

        if (range->error == BR_ReadPastEnd) {
                zw_assert(1 == CC_SHA1_Final(sink(result).digest, &ctx),
                          "CC_SHA1_Final");
        } else {
                result.errorcode = 1;
        }

        return result;
}

#include <fcntl.h>
#include <unistd.h>

/// open an input stream on a path, with the intent of reading it
/// fully and only once
void inputstream_on_filepath(struct MemoryArena *arena,
                             struct BufferRange *result,
                             char const *path)
{
        struct StreamedFile {
                int filedesc = 0;
                char const *path = nullptr;
                bool32 is_opened = 0;

                // TODO(nicolas) what about io advice received before?
                uint8_t buffer[4 * 4 * 4 * 4096] = {};
        };

        auto next_on_streamed_file = [](struct BufferRange *range) {
                struct StreamedFile *file =
                    reinterpret_cast<struct StreamedFile *>(
                        range->start - offsetof(struct StreamedFile, buffer));

                if (BR_NoError == range->error && !file->is_opened) {
                        SPDR_SCOPE(global_spdr, "io", "open");
                        int filedesc = open(file->path, O_RDONLY, 0);
                        fcntl(filedesc, F_NOCACHE,
                              1); // we only process the files once

                        file->filedesc = filedesc;
                        if (file->filedesc < 0) {
                                return fail(range, BR_IOError);
                        }
                        file->is_opened = true;
                }

                SPDR_BEGIN1(global_spdr, "io", "read",
                            SPDR_INT("size", sizeof file->buffer));
                ssize_t size_read =
                    read(file->filedesc, &file->buffer, sizeof file->buffer);
                SPDR_END(global_spdr, "io", "read");

                if (size_read < 0) {
                        close(file->filedesc);
                        return fail(range, BR_IOError);
                } else if (size_read == 0) {
                        close(file->filedesc);
                        return fail(range, BR_ReadPastEnd);
                }

                range->start = file->buffer;
                range->cursor = range->start;
                range->end = range->start + size_read;

                return BR_NoError;
        };

        auto stream_on_file = [next_on_streamed_file](
            struct BufferRange *range, struct StreamedFile *file) {
                range->start = file->buffer;
                range->cursor = range->start;
                range->end = range->start;
                range->error = BR_NoError;
                range->next = next_on_streamed_file;
        };

        struct StreamedFile *file = push_pointer_rvalue(arena, file);
        *file = StreamedFile{};
        file->path = path;

        stream_on_file(result, file);
}

void inputstream_finish(struct BufferRange *range)
{
        while (range->error == BR_NoError) {
                range->next(range);
        }
}

#include <sys/vnode.h> // for enum vtype
#include <mach/mach.h> // for vm_allocate

struct FileList {
        size_t count;
        char const **paths;
        char const **filenames;
        struct {
                uint64_t size;
        } * attributes;
};

#include <sys/attr.h>
#include <cerrno>

zw_global bool global_can_ignore_file = false;

/**
   Query a directory and its sub-directories for all their files,
   sorted to maximize throughput when streaming their content.

   @return null or a valid file list allocated in the given arena
*/
zw_internal struct FileList *directory_query_all_files(
    struct MemoryArena *arena, char const *root_dir_path, bool trace_on)
{
        // NOTE(nicolas)
        //
        // for each directory in the stack of directories to visit,
        //
        // pop that directory off the stack of directories to visit,
        // and collect all entries using getattrlistbulk.
        // - directories are pushed onto the stack of directories to
        // visit.
        //
        // repeat until there is no more directories to visit

        // TODO(nicolas) @perf we're nowhere near
        // saturating the I/O there, especially when querying
        // a network shared drive.

        // TODO(nicolas) compare performance w/ CIFS and AFP network shares
        // TODO(nicolas) how to minimize seeking of a NAS/mounted disk?

        struct FSEntry;

        struct State {
                FSEntry *free_entry = nullptr;
                FSEntry *directories = nullptr;
                FSEntry *files = nullptr;
        };

        struct State *state = push_struct(arena, struct State);
        struct FSEntry {
                struct FSEntry *next;
                char const *path;
                uint64_t physical_offset; // for files
                uint64_t size;
        };

        auto push_entry = [&state, arena]() {
                FSEntry *result;

                if (!state->free_entry) {
                        result = push_struct(arena, FSEntry);
                } else {
                        result = state->free_entry;
                        state->free_entry = state->free_entry->next;
                }

                return result;
        };

        auto push_directory = [&state, push_entry]() {
                FSEntry *result = push_entry();

                result->physical_offset = 0;
                result->next = state->directories;
                state->directories = result;

                return result;
        };

        auto pop_directory = [&state]() {
                FSEntry *result = state->directories;
                if (result) {
                        state->directories = result->next;
                }

                return result;
        };

        auto push_file = [&state, push_entry]() {
                FSEntry *result = push_entry();

                result->next = state->files;
                state->files = result;

                return result;
        };

        auto discard_entry = [&state](FSEntry *entry) {
                entry->next = state->free_entry;
                state->free_entry = entry;
        };

        FSEntry *entry = push_directory();
        entry->path = root_dir_path;

        TextOutputGroup dirlisting = {};
        allocate(dirlisting, arena, KILOBYTES(1));

        auto print_dirlisting_line = [&dirlisting, trace_on]() {
                if (trace_on) {
                        trace(dirlisting);
                } else {
                        clear(dirlisting);
                }
        };

        // what we are asking getattrlistbulk
        struct attrlist query_attributes = {
            ATTR_BIT_MAP_COUNT,
            0,
            (ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_ERROR | ATTR_CMN_NAME |
             ATTR_CMN_OBJTYPE | ATTR_CMN_OBJTAG),
            0,
            0,
            ATTR_FILE_TOTALSIZE | ATTR_FILE_IOBLOCKSIZE,
            0,
        };

        uint64_t query_options =
            FSOPT_NOFOLLOW | FSOPT_REPORT_FULLSIZE | FSOPT_PACK_INVAL_ATTRS;

        // what getattrlistbulk will return
        struct DirEntryAttributes {
                uint32_t size;
                attribute_set_t attributes;
                uint32_t error;
                attrreference nameinfo;
                fsobj_type_t obj_type;
                fsobj_tag_t obj_tag;
                union {
                        struct {
                                off_t file_totalsize;
                                uint32_t file_ioblocksize;
                        };
                };
        } __attribute__((aligned(4), packed));

        // record a directory entry, depending if it's a file
        // or another directory
        auto record_directory_entry = [&dirlisting, print_dirlisting_line,
                                       arena, push_file, push_directory,
                                       trace_on](
            char const *dir_path, struct DirEntryAttributes const *entry) {
                char const *name = (char *)((uint8_t *)&entry->nameinfo) +
                                   entry->nameinfo.attr_dataoffset;

                auto path_size = cstr_len(dir_path) + 1 + cstr_len(name) + 1;
                char *path = push_array_rvalue(arena, path, path_size);
                {
                        auto const buffer_last = path + path_size;
                        char *string_last = path;
                        cstr_cat(string_last, buffer_last, dir_path);
                        cstr_cat(string_last, buffer_last, "/");
                        cstr_cat(string_last, buffer_last, name);
                        zw_assert(cstr_terminate(string_last, buffer_last),
                                  "unexpected size");
                }
                char *path_cstr = (char *)path;
                bool ignore_file = false;

                uint64_t physical_offset = 0;
                {
                        push_back_cstr(dirlisting, name);
                        if (entry->obj_type == VREG) {
                                push_back_cstr(dirlisting, "\t[f]");
                        } else if (entry->obj_type == VDIR) {
                                push_back_cstr(dirlisting, "\t[d]");
                        } else {
                                // not a file, not a directory
                                push_back_formatted(dirlisting, "\t[%d]",
                                                    entry->obj_type);
                        }
                        push_back_cstr(dirlisting, "\t");

                        // NOTE(nicolas) non native file systems
                        // (AFP/CIFS) are problematic not only for
                        // performance reasons (no possibility to
                        // maximize locality) and also because they
                        // may not faithfully represent the filenames
                        // of our directory stores, making
                        // renames/maintenance potentially difficult.

                        bool mounted_share = false;
                        bool supports_semicolon = false;
                        if (entry->obj_tag == VT_NFS) {
                                push_back_cstr(dirlisting, "NFS");
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_HFS) {
                                push_back_cstr(dirlisting, "HFS");
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_AFP) {
                                push_back_cstr(dirlisting, "AFP");
                                mounted_share = true;
                        } else if (entry->obj_tag == VT_CIFS) {
                                push_back_cstr(dirlisting, "CIFS");
                                mounted_share = true;
                        } else {
                                push_back_u32(dirlisting, entry->obj_tag);
                        }

                        if (mounted_share) {
                                MemoryArena temp_arena = *arena;
                                auto errorg = TextOutputGroup{};
                                allocate(errorg, &temp_arena, KILOBYTES(1));
                                push_back_cstr(errorg, "path '");
                                push_back_cstr(errorg, path_cstr);
                                push_back_cstr(errorg, "' is a mounted share.");
                                error(errorg);
                        }

                        if (entry->obj_type == VREG) {
                                push_back_cstr(dirlisting, "\t");
                                push_back_u32(dirlisting,
                                              entry->file_ioblocksize);
                                push_back_cstr(dirlisting, "\t");
                                push_back_u64(dirlisting,
                                              entry->file_totalsize);

                                if (!supports_semicolon &&
                                    global_can_ignore_file) {
                                        if (':' == *cstr_find(path_cstr, ':')) {
                                                MemoryArena temp_arena = *arena;
                                                auto errorg = TextOutputGroup{};
                                                allocate(errorg, &temp_arena,
                                                         KILOBYTES(1));
                                                push_back_cstr(errorg,
                                                               "path: '");
                                                push_back_cstr(errorg,
                                                               path_cstr);
                                                push_back_cstr(
                                                    errorg,
                                                    "'  contains a colon, "
                                                    "which is not supported "
                                                    "by vnode tag type");
                                                error(errorg);
                                                ignore_file = true;
                                        }
                                }

                                int const fd = open(path_cstr, O_RDONLY, 0);
                                // TODO(nicolas) permissions to open
                                // that file at all?
                                zw_assert(ignore_file || fd >= 0, "open file");
                                DEFER(close(fd));

                                if (!ignore_file && entry->file_totalsize > 0) {
                                        struct log2phys filephys;
                                        int fcntl_result =
                                            fcntl(fd, F_LOG2PHYS, &filephys);
                                        zw_assert(fcntl_result >= 0,
                                                  "could not get "
                                                  "phys");
                                        if (fcntl_result < 0) {
                                                error_print(
                                                    "could not get phys");
                                        } else {
                                                physical_offset =
                                                    filephys.l2p_devoffset;
                                                push_back_cstr(dirlisting,
                                                               "\t");
                                                push_back_u64(
                                                    dirlisting,
                                                    filephys.l2p_devoffset);
                                        }
                                }
                        }
                        print_dirlisting_line();
                }

                if (entry->obj_type == VREG && !ignore_file) {
                        FSEntry *file_entry = push_file();
                        file_entry->path = path_cstr;
                        file_entry->physical_offset = physical_offset;
                        file_entry->size = entry->file_totalsize;
                } else if (entry->obj_type == VDIR) {
                        FSEntry *dir_entry = push_directory();
                        dir_entry->path = path_cstr;
                }

        };

        FSEntry *dir_entry;
        while ((dir_entry = pop_directory())) {
                char const *dir_path;
                {
                        dir_path = dir_entry->path;
                        discard_entry(dir_entry);
                        dir_entry = nullptr;
                }

                int dir_fd;
                {
                        dir_fd = open(dir_path, O_RDONLY, 0);
                        if (dir_fd < 0) {
                                MemoryArena temp_arena = *arena;
                                TextOutputGroup errorg = {};
                                allocate(errorg, &temp_arena, KILOBYTES(1));
                                push_back_cstr(errorg,
                                               "could not find directory: ");
                                push_back_cstr(errorg, dir_path);
                                error(errorg);
                                return nullptr;
                        }
                }
                DEFER({ close(dir_fd); });

                push_back_cstr(dirlisting, "\nlisting directory: ");
                push_back_cstr(dirlisting, dir_path);
                print_dirlisting_line();

                // TODO(nicolas): push sep by "\t"
                push_back_cstr(dirlisting, "name");
                push_back_cstr(dirlisting, "\t");
                push_back_cstr(dirlisting, "type");
                push_back_cstr(dirlisting, "\t");
                push_back_cstr(dirlisting, "tag");
                push_back_cstr(dirlisting, "\t");
                push_back_cstr(dirlisting, "ioblocksize");
                push_back_cstr(dirlisting, "\t");
                push_back_cstr(dirlisting, "size");
                push_back_cstr(dirlisting, "\t");
                push_back_cstr(dirlisting, "physical location");
                print_dirlisting_line();

                uint32_t entrycount = 0;
                uint32_t result_batch[16 * 256];
                int result;
                while ((result = getattrlistbulk(
                            dir_fd, &query_attributes, &result_batch,
                            sizeof result_batch, query_options)) > 0) {
                        auto batch_entry_count = result;

                        uint8_t *data = (uint8_t *)result_batch;
                        for (int entry_index = 0;
                             entry_index < batch_entry_count; entry_index++) {
                                struct DirEntryAttributes *entry =
                                    (struct DirEntryAttributes *)data;
                                record_directory_entry(dir_path, entry);
                                data += entry->size;
                        }
                        entrycount += batch_entry_count;
                }
                if (result < 0) {
                        if (EBADF == errno || ENOTDIR == errno) {
                                error_print("not a readable directory");
                                return nullptr;
                        }
                        perror("getattrlistbulk");
                        return nullptr;
                }
        }

        zw_assert(state->directories == nullptr, "no more directories");

        size_t entry_array_count = 0;
        FSEntry *entry_array = nullptr;
        {
                size_t file_count = 0;
                for (FSEntry *fs_entry = state->files; fs_entry;
                     fs_entry = fs_entry->next) {
                        file_count++;
                }

                push_back_cstr(dirlisting, "found ");
                push_back_u64(dirlisting, file_count);
                push_back_cstr(dirlisting, " files");
                print_dirlisting_line();

                entry_array_count = file_count;
                entry_array =
                    push_array_rvalue(arena, entry_array, entry_array_count);

                for (FSEntry *fs_entry = state->files,
                             *dest_entry = entry_array;
                     fs_entry; fs_entry = fs_entry->next, dest_entry++) {
                        *dest_entry = *fs_entry;
                }
        }

        // NOTE(nicolas) we sort our files by physical offset to reduce
        // seeking and improve access times on HDD. It shouldn't
        // hurt for SSD.
        {
                // 64-bit radix sort
                // 8-bit character

                struct FSEntryInBucket {
                        FSEntry fs_entry_value;
                        FSEntryInBucket *next;
                };

                FSEntryInBucket *bucket_entries =
                    push_array_rvalue(arena, bucket_entries, entry_array_count);

                // circular linked-list per bucket, this bucket is pointing
                // to the last element
                struct {
                        FSEntryInBucket *last;
                } buckets[256] = {};

                for (size_t passIndex = 0; passIndex < sizeof(uint64_t);
                     passIndex++) {
                        size_t bucket_entry_index = 0;
                        for (size_t entry_index = 0;
                             entry_index < entry_array_count; entry_index++) {
                                auto fs_entry = &entry_array[entry_index];
                                uint8_t key = (fs_entry->physical_offset >>
                                               (8 * passIndex)) &
                                              0xFF;

                                FSEntryInBucket *bucket_entry =
                                    &bucket_entries[bucket_entry_index++];
                                bucket_entry->fs_entry_value = *fs_entry;

                                FSEntryInBucket *current_last_entry =
                                    buckets[key].last;
                                if (!current_last_entry) {
                                        bucket_entry->next = bucket_entry;
                                } else {
                                        bucket_entry->next =
                                            current_last_entry->next;
                                        current_last_entry->next = bucket_entry;
                                }
                                buckets[key].last = bucket_entry;
                        }

                        FSEntry *fs_entry = entry_array;
                        for (auto &bucket : buckets) {
                                if (bucket.last == nullptr) {
                                        continue;
                                }

                                auto first_entry = bucket.last->next;
                                auto entry_in_bucket = first_entry;
                                do {
                                        *(fs_entry++) =
                                            entry_in_bucket->fs_entry_value;
                                        entry_in_bucket = entry_in_bucket->next;
                                } while (entry_in_bucket != first_entry);
                                bucket.last = nullptr;
                        }
                }

#if defined(ZWEI_SLOW)
                // NOTE(nicolas) in SLOW mode assert that elements are well
                // sorted
                uint64_t previous_offset = 0;
                for (size_t i = 0; i < entry_array_count; i++) {
                        uint64_t this_offset = entry_array[i].physical_offset;
                        zw_assert(this_offset >= previous_offset,
                                  "file entries should be monotonic");
                        previous_offset = this_offset;
                }
#endif

                for (size_t i = 0; i < entry_array_count; i++) {
                        push_back_cstr(dirlisting, "FILE");
                        push_back_u64(dirlisting, i);
                        push_back_cstr(dirlisting, "\t");
                        push_back_u64(dirlisting,
                                      entry_array[i].physical_offset);
                        push_back_cstr(dirlisting, entry_array[i].path);
                        print_dirlisting_line();
                }
        }

        // construct file list from array
        struct FileList *result;
        result = push_struct(arena, struct FileList);
        result->count = entry_array_count;

        if (result->count) {
                size_t count = result->count;
                result->paths = push_array_rvalue(arena, result->paths, count);
                result->filenames =
                    push_array_rvalue(arena, result->filenames, count);
                result->attributes =
                    push_array_rvalue(arena, result->attributes, count);
                for (size_t i = 0; i < count; i++) {
                        result->paths[i] = entry_array[i].path;
                        result->filenames[i] =
                            1 + cstr_find_last(entry_array[i].path, '/');
                        result->attributes[i].size = entry_array[i].size;
                }
        }

        return result;
}

/**
   NOTE(nicolas)

   We want to read from a root directory a series of small files in
   the fastest possible way.

   These files may either be stored locally or on a remote drive.

   We extract features from these files in memory.
*/

#include <dlfcn.h>
#include <sys/stat.h>

struct LoadedLibrary {
        void *dlhandle;
        char file_path[4096];
        int64_t file_mtime;
};

zw_internal bool refresh_library(struct LoadedLibrary *library)
{
        struct stat stats;
        stat(library->file_path, &stats);
        if (stats.st_mtime > library->file_mtime) {
                if (library->dlhandle) {
                        zw_assert(0 == dlclose(library->dlhandle),
                                  "could not close library");
                }
                void *dlhandle =
                    dlopen(library->file_path, RTLD_NOW | RTLD_LOCAL);
                zw_assert(dlhandle, "could not open libzwei.dylib");
                if (!dlhandle) {
                        error_print("fatal error, could not load library");
                        return false;
                }

                library->dlhandle = dlhandle;
                library->file_mtime = stats.st_mtime;

                return true;
        }

        return false;
}

int main(int argc, char **argv)
{
        zw_assert(argc > 0, "unexpected argc");
        DEFER(trace_print("done"));
        char const *root_dir_path = nullptr;

        auto USAGE = "<program> [--help|--ls|--can-ignore|--debug] --root-dir "
                     "<root-dir>";

        auto directory_listing_on = false;
        auto debug_mode_on = false;
        auto current_arg = 1;
        while (current_arg < argc) {
                if (cstr_equals(argv[current_arg], "--root-dir")) {
                        current_arg++;
                        if (current_arg >= argc) {
                                error_print(
                                    "expected argument after --root-dir");
                        }
                        root_dir_path = argv[current_arg];
                        current_arg++;
                } else if (cstr_equals(argv[current_arg], "--ls")) {
                        current_arg++;
                        // FEATURE(nicolas): Optionally prints all files in
                        // <root-dir> using `--ls`
                        directory_listing_on = true;
                } else if (cstr_equals(argv[current_arg], "--can-ignore")) {
                        current_arg++;
                        global_can_ignore_file = true;
                } else if (cstr_equals(argv[current_arg], "--debug")) {
                        current_arg++;
                        debug_mode_on = true;
                } else {
                        error_print("unexpected argument");
                        trace_print(USAGE);
                        return 1;
                        current_arg++;
                }
        }

        void *permanent_storage = nullptr;
        auto permanent_storage_memory_size = GIGABYTES(1);
        void *transient_storage = nullptr;
        auto transient_storage_memory_size = MEGABYTES(64);
        {
                vm_address_t transient_storage_memory_address = 0x1000000000;
                vm_address_t permanent_storage_memory_address = 0x2000000000;
                kern_return_t vm_allocate_result;
                vm_allocate_result = vm_allocate(
                    mach_task_self(), &permanent_storage_memory_address,
                    permanent_storage_memory_size, false);
                if (KERN_SUCCESS != vm_allocate_result) {
                        error_print("could not allocate enough memory");
                        return 1;
                }
                permanent_storage = (void *)permanent_storage_memory_address;
                vm_allocate_result = vm_allocate(
                    mach_task_self(), &transient_storage_memory_address,
                    transient_storage_memory_size, false);
                if (KERN_SUCCESS != vm_allocate_result) {
                        error_print("could not allocate enough memory");
                        return 1;
                }
                transient_storage = (void *)transient_storage_memory_address;
        }

        struct MemoryArena permanent_arena =
            memory_arena(permanent_storage, permanent_storage_memory_size);
        if (spdr_init(&global_spdr, push_bytes(&permanent_arena, MEGABYTES(32)),
                      MEGABYTES(32))) {
                error_print("could not initialize memory for tracing");
                return 1;
        }
        spdr_enable_trace(global_spdr, 1);
        SPDR_METADATA1(global_spdr, "thread_name", SPDR_STR("name", "main"));

        AcceptMimeMessageFn *accept_mime_message;
        ParseZoeMailstorePathFn *parse_zoe_mailstore_path;

        struct LoadedLibrary zwei_app_library = {};
        {
                auto const buffer_first = zwei_app_library.file_path;
                auto const buffer_last =
                    buffer_first + sizeof zwei_app_library.file_path;
                char *string_last = buffer_first;
                cstr_cat(string_last, buffer_last, argv[0],
                         cstr_find_last(argv[0], '/'));
                cstr_cat(string_last, buffer_last, "/libzwei.dylib");
                if (!cstr_terminate(string_last, buffer_last)) {
                        return 1;
                }

                struct LoadedLibrary *lib = &zwei_app_library;
                lib->dlhandle = 0;
                lib->file_mtime = 0;
        }

        auto refresh_zwei_app = [&zwei_app_library, &accept_mime_message,
                                 &parse_zoe_mailstore_path, debug_mode_on]() {
                struct LoadedLibrary *lib = &zwei_app_library;
                bool was_loaded = !!lib->dlhandle;
                if (refresh_library(lib)) {
                        if (was_loaded) {
                                trace_print("reloaded library!");
                        }

                        InitAppFn *init_app;
                        init_app = reinterpret_cast<decltype(init_app)>(
                            dlsym(lib->dlhandle, "init_app"));
                        zw_assert(init_app, "expected symbol init_app");

                        accept_mime_message =
                            reinterpret_cast<decltype(accept_mime_message)>(
                                dlsym(lib->dlhandle, "accept_mime_message"));
                        zw_assert(accept_mime_message,
                                  "expected symbol check_mime_message");

                        parse_zoe_mailstore_path = reinterpret_cast<decltype(
                            parse_zoe_mailstore_path)>(
                            dlsym(lib->dlhandle, "parse_zoe_mailstore_path"));
                        zw_assert(parse_zoe_mailstore_path,
                                  "expected symbol parse_zoe_mailstore_path");

                        Platform platform;
                        platform.spdr = global_spdr;

                        init_app(platform,
                                 debug_mode_on ? ZWEI_DEBUG_MODE_FLAG : 0);

                        return true;
                }

                return false;
        };

        refresh_zwei_app();

        // TODO(nicolas): traces should feature a timestamp for
        // performance and auditing. i.e. the platform layer should
        // offer a logging service

        // FEATURE(nicolas): show content of any directory recursively
        // FEATURE(nicolas): show sha-1 digests of all files inside a directory
        if (!root_dir_path) {
                error_print("you must pass a root directory with --root-dir");
        } else {
                struct MemoryArena dc_arena = memory_arena(
                    transient_storage, transient_storage_memory_size / 2);

                // NOTE(nicolas) we could process files in parallel
                // .. but how much parallelism can a HDD/SDD/Raid
                // device support?

                struct FileList *all_files = directory_query_all_files(
                    &dc_arena, root_dir_path, directory_listing_on);

                if (!all_files) {
                        error_print("could not query all files");
                        return 1;
                }

                {
                        MemoryArena temp_arena = dc_arena;
                        TextOutputGroup traceg = {};
                        allocate(traceg, &temp_arena, KILOBYTES(1));
                        push_back_cstr(traceg, "bytes used after query: ");
                        push_back_formatted(traceg, "%lld", (&dc_arena)->used);
                        trace(traceg);
                }

                size_t total_bytes_read =
                    0; // TODO(nicolas) technically could be larger than size_t
                // TODO(nicolas) always measure bytes/sec or sec/MB and
                // print out total bytes
                SPDR_BEGIN(global_spdr, "main", "processing_files");
                for (size_t file_index = 0; file_index < all_files->count;
                     file_index++) {
                        auto const filename = all_files->filenames[file_index];
                        auto const filepath = all_files->paths[file_index];
                        auto const file_attributes =
                            all_files->attributes[file_index];

                        MemoryArena message_arena_memory =
                            memory_arena(dc_arena.base + dc_arena.size,
                                         (uint8_t *)transient_storage +
                                             transient_storage_memory_size -
                                             dc_arena.base + dc_arena.size);
                        MemoryArena *message_arena = &message_arena_memory;
                        TextOutputGroup traceg = {};
                        allocate(traceg, message_arena, KILOBYTES(1));
                        {
                                push_back_cstr(traceg, "FILE");
                                push_back_formatted(traceg, "%lld", file_index);
                                push_back_cstr(traceg, " ");
                                push_back_cstr(traceg, filepath);
                                push_back_cstr(traceg, ":");
                                trace(traceg);
                        }
                        SPDR_SCOPE1(global_spdr, "main", "process_file",
                                    SPDR_STR("filepath", filepath));
                        SPDR_COUNTER1(
                            global_spdr, "variables", "message_arena",
                            SPDR_INT("used", int(message_arena->used)));

                        // Slurp entire file in memory
                        // NOTE(nicolas): this ensure we don't do I/O in the
                        // parsing
                        // part of the application.
                        // TODO(nicolas): what if the message is too large?
                        uint8_t *full_message;
                        uint8_t *full_message_end;
                        {
                                SPDR_SCOPE(global_spdr, "main",
                                           "read_entire_file");
                                struct BufferRange file_content;
                                inputstream_on_filepath(
                                    message_arena, &file_content, filepath);

                                // TODO(nicolas): @copypaste
                                // cfec80a4708df2e90e45023f0d87af7f4eb54a46
                                full_message =
                                    (uint8_t *)push_bytes(message_arena, 0);
                                full_message_end = full_message;

                                while (file_content.error == BR_NoError) {
                                        zw_assert(
                                            full_message_end ==
                                                push_bytes(message_arena, 0),
                                            "non contiguous");
                                        full_message_end = algos::copy(
                                            file_content.start,
                                            file_content.end,
                                            (uint8_t *)push_bytes(
                                                message_arena,
                                                file_content.end -
                                                    file_content.start));

                                        file_content.next(&file_content);
                                }
                                inputstream_finish(&file_content);
                                SPDR_EVENT1(
                                    global_spdr, "main", "read_entire_file",
                                    SPDR_INT("size", int(full_message_end -
                                                         full_message)));
                        }

                        push_back_cstr(traceg, "SHA1");
                        push_back_cstr(traceg, "\t");

                        zw_assert(uint64_t(full_message_end - full_message) ==
                                      file_attributes.size,
                                  "surprising file size");

                        struct BufferRange file_content;
                        stream_on_memory(&file_content, full_message,
                                         full_message_end - full_message);
                        auto sha1_result = sha1(&file_content);
                        inputstream_finish(&file_content);

                        if (failed(sha1_result)) {
                                push_back_cstr(traceg,
                                               "<failed to compute result>");
                        } else {
                                zw_assert(file_content.error == BR_ReadPastEnd,
                                          "must read until end");

                                auto sha1_value = just(sha1_result);
                                char byteToHexChar[] = {
                                    '0',
                                    '1',
                                    '2',
                                    '3',
                                    '4',
                                    '5',
                                    '6',
                                    '7',
                                    '8',
                                    '9',
                                    'a',
                                    'b',
                                    'c',
                                    'd',
                                    'e',
                                    'f',
                                };
                                for (size_t byteIndex = 0;
                                     byteIndex < NCOUNT(sha1_value.digest);
                                     byteIndex++) {
                                        uint8_t const byte =
                                            sha1_value.digest[byteIndex];
                                        push_back_formatted(
                                            traceg, "%c%c",
                                            byteToHexChar[byte >> 4],
                                            byteToHexChar[byte & 0xF]);
                                }
                        }
                        trace(traceg);

                        struct ZoeMailStoreFile zoefile;
                        auto zoefile_errorcode =
                            parse_zoe_mailstore_path(&zoefile, filename);

                        if (0 == zoefile_errorcode ||
                            cstr_endswith(filename, ".eml")) {
                                // FEATURE(nicolas): print filing timestamp
                                push_back_cstr(traceg, "PATH");
                                push_back_cstr(traceg, "\t");
                                push_back_cstr(traceg, filepath);
                                trace(traceg);

                                push_back_cstr(traceg, "SIZE");
                                push_back_cstr(traceg, "\t");
                                push_back_u64(traceg, file_attributes.size);
                                push_back_cstr(traceg, "\t");
                                trace(traceg);

                                push_back_cstr(traceg, "ZOE_REL_URL");
                                push_back_cstr(traceg, "\t");
                                {
                                        // find predecessor, until you reach
                                        // boundary
                                        auto pred = [](char const *const first,
                                                       char const *const pos) {
                                                if (pos != first) {
                                                        return algos::
                                                            predecessor(pos);
                                                }
                                                return pos;
                                        };
                                        // skip day, month, year
                                        auto first =
                                            cstr_find_last(filepath, '/');
                                        first = algos::find_backward(
                                            filepath, pred(filepath, first),
                                            '/');
                                        first = algos::find_backward(
                                            filepath, pred(filepath, first),
                                            '/');
                                        first = algos::find_backward(
                                            filepath, pred(filepath, first),
                                            '/');
                                        first = algos::successor(first);
                                        auto last = cstr_find_last(first, '.');
                                        push_back_extent(traceg,
                                                         (uint8_t *)first,
                                                         size_t(last - first));
                                }
                                trace(traceg);

                                if (0 == zoefile_errorcode) {
                                        push_back_cstr(traceg, "UUID");
                                        push_back_cstr(traceg, "\t");
                                        for (size_t i = 0;
                                             i < NCOUNT(zoefile.uuid); i++) {
                                                push_back_formatted(
                                                    traceg, "%x",
                                                    zoefile.uuid[i]);
                                        }
                                        trace(traceg);

                                        push_back_cstr(traceg,
                                                       "UNIX TIMESTAMP");
                                        push_back_cstr(traceg, "\t");
                                        push_back_formatted(
                                            traceg, "%llu",
                                            zoefile.unix_epoch_millis);
                                        trace(traceg);
                                }

                                do {
                                        SPDR_BEGIN(global_spdr, "app",
                                                   "accept_mime_message");
                                        accept_mime_message(
                                            full_message, full_message_end,
                                            zoefile_errorcode == 0 ? &zoefile
                                                                   : nullptr,
                                            message_arena);
                                        SPDR_END(global_spdr, "app",
                                                 "accept_mime_message");
                                } while (refresh_zwei_app());
                        } else {
                                trace_print("ignored file");
                        }
                        total_bytes_read += full_message_end - full_message;
                        SPDR_COUNTER1(
                            global_spdr, "variables", "read",
                            SPDR_INT("bytes", int(total_bytes_read / 1000)));
                        SPDR_COUNTER1(
                            global_spdr, "variables", "message_arena",
                            SPDR_INT("used", int(message_arena->used)));
                }
        }
        SPDR_END(global_spdr, "main", "processing_files");

        void text_output_group_print(int filedesc,
                                     TextOutputGroup const &group);

        auto program_path_first = argv[0];
        auto program_path_last = cstr_find_last(argv[0], '/');
        char trace_file[4096];
        auto trace_file_last = algos::begin(trace_file);
        cstr_cat(trace_file_last, algos::end(trace_file), program_path_first,
                   program_path_last);
        cstr_cat(trace_file_last, algos::end(trace_file), "/trace.json");
        if (!cstr_terminate(trace_file_last, algos::end(trace_file))) {
                zw_assert(false, "uh");
                return 1;
        }
        {
                MemoryArena temp_arena = permanent_arena;
                auto tog = TextOutputGroup{};
                allocate(tog, &temp_arena, 512);
                push_back_cstr(tog, "reporting traces to file: ");
                push_back_cstr(tog, (char *)trace_file);
                push_back_cstr(tog, "\n");
                trace(tog);
        }
        int fd = open((char *)trace_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        spdr_report(
            global_spdr,
            SPDR_CHROME_REPORT, [](char const *string, void *user_data) {
                    char buffer[4096];
                    MemoryArena stack = memory_arena(buffer, sizeof buffer);
                    int *fd_ptr = static_cast<int *>(user_data);
                    auto tog = TextOutputGroup{};
                    allocate(tog, &stack, 64);
                    push_back_cstr(tog, string);
                    text_output_group_print(*fd_ptr, tog);
            }, &fd);
        close(fd);
        spdr_deinit(&global_spdr);

        return 0;
}

#include "zwei_iobuffer.cpp"
#include "zwei_osx_logging.cpp"

#include "zwei_inlines.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_logging.hpp"

#include "zwei_app.hpp"

// <BUFFER-CENTRIC IO

zw_internal enum BufferRangeErrorCode next_zeros(struct BufferRange *range)
{
        zw_local_persist uint8_t const zeros[256] = {0};

        range->start = (uint8_t *)zeros;
        range->cursor = (uint8_t *)zeros;
        range->end = (uint8_t *)zeros + sizeof(zeros);

        return range->error;
}

zw_internal enum BufferRangeErrorCode fail(struct BufferRange *range,
                                           enum BufferRangeErrorCode error)
{
        range->error = error;
        range->next = next_zeros;

        return range->next(range);
}

zw_internal enum BufferRangeErrorCode
next_on_memory_buffer(struct BufferRange *range)
{
        return fail(range, BR_ReadPastEnd);
}

void stream_on_memory(struct BufferRange *range,
                      uint8_t *mem,
                      size_t const size)
{
        range->start = mem;
        range->cursor = mem;
        range->end = range->start + size;
        range->error = BR_NoError;
        range->next = next_on_memory_buffer;
}

// ..BUFFER-CENTRIC IO>

// <STRING CONSTRUCTION W/ BUFFER

#include <cstdarg>
#include <cstdio>

/// try to concatenate the given c string when possible, always truncate
void string_cat(struct BufferRange *range, char const *cstring)
{
        uint8_t *byte = (uint8_t *)cstring;
        while (range->error == BR_NoError && range->cursor < range->end) {
                *(range->cursor) = *byte;

                byte++;
                range->cursor++;

                if (!*byte)
                        break;
                if (range->cursor == range->end) {
                        range->next(range);
                }
        }
}
/// try to concatenate the given c string region when possible, always truncate
void string_cat(struct BufferRange *range,
                char const *cstring,
                char const *cstring_end)
{
        uint8_t *byte = (uint8_t *)cstring;
        uint8_t *end_byte = (uint8_t *)cstring_end;
        while (range->error == BR_NoError && range->cursor < range->end) {
                *(range->cursor) = *byte;

                byte++;
                range->cursor++;

                if (byte == end_byte)
                        break;
                if (range->cursor == range->end) {
                        range->next(range);
                }
        }
}

void string_cat_formatted(struct BufferRange *range, char const *pattern, ...)
{
        size_t needed_size = 0;
        va_list original_args;
        va_start(original_args, pattern);
        DEFER(va_end(original_args));
        {
                va_list args;
                va_copy(args, original_args);
                DEFER(va_end(args));

                needed_size = vsnprintf(0, 0, pattern, args);
                assert(needed_size > 0, "invalid format string");
        }

        char buffer[needed_size + 1];
        vsnprintf(buffer, sizeof buffer, pattern, original_args);
        string_cat(range, buffer);
}

void string_cat_uint32(struct BufferRange *range, uint32_t value)
{
        string_cat_formatted(range, "%u", value);
}

bool string_terminate(struct BufferRange *range)
{
        if (range->error == BR_NoError && range->cursor < range->end) {
                *range->cursor = 0;
                return true;
        }

        return false;
}

// ..STRING CONSTRUCTION W/ BUFFERS>

#include <CommonCrypto/CommonDigest.h>
#include <limits>

struct Sha1 {
        uint8_t digest[20];
};

MayFail<Sha1> sha1(struct BufferRange *range)
{
        MayFail<Sha1> result;

        size_t const MAX_CHUNK_SIZE = std::numeric_limits<CC_LONG>::max();
        auto chunk_size = [MAX_CHUNK_SIZE](size_t size) {
                return static_cast<CC_LONG>(
                    size < MAX_CHUNK_SIZE ? size : MAX_CHUNK_SIZE);
        };

        CC_SHA1_CTX ctx;
        assert(1 == CC_SHA1_Init(&ctx), "CC_SHA1_Init");

        while (range->error == BR_NoError) {
                while (range->cursor < range->end) {
                        auto size = chunk_size(range->end - range->cursor);
                        assert(1 == CC_SHA1_Update(&ctx, range->cursor, size),
                               "CC_SHA1_Update");
                        range->cursor += size;
                }
                range->next(range);
        }

        if (range->error == BR_ReadPastEnd) {
                assert(1 == CC_SHA1_Final(result.value.digest, &ctx),
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
                uint8_t buffer[4 * 4096] = {};
        };

        auto next_on_streamed_file = [](struct BufferRange *range) {
                struct StreamedFile *file =
                    reinterpret_cast<struct StreamedFile *>(
                        range->start - offsetof(struct StreamedFile, buffer));

                if (BR_NoError == range->error && !file->is_opened) {
                        int filedesc = open(file->path, O_RDONLY, 0);
                        fcntl(filedesc, F_NOCACHE, 1);
                        fcntl(filedesc, F_RDAHEAD, 1);

                        file->filedesc = filedesc;
                        if (file->filedesc < 0) {
                                return fail(range, BR_IOError);
                        }
                        file->is_opened = true;
                }

                ssize_t size_read =
                    read(file->filedesc, &file->buffer, sizeof file->buffer);
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
};

#include <sys/attr.h>
#include <cerrno>

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

        // TODO(nicolas) performance: we're nowhere near
        // saturating the I/O there, especially when querying
        // a network shared drive.

        // TODO(nicolas) compare performance w/ CIFS and AFP network shares
        // TODO(nicolas) how to minimize seeking of a NAS/mounted disk?

        struct FSEntry;

        struct State {
                struct FSEntry *free_entry = nullptr;
                struct FSEntry *directories = nullptr;
                struct FSEntry *files = nullptr;
        };

        struct State *state = push_struct(arena, struct State);
        struct FSEntry {
                char const *path;
                struct FSEntry *next;
                uint64_t physical_offset; // for files
        };

        auto push_entry = [&state, arena]() {
                struct FSEntry *result;

                if (!state->free_entry) {
                        result = push_struct(arena, struct FSEntry);
                } else {
                        result = state->free_entry;
                        state->free_entry = state->free_entry->next;
                }

                return result;
        };

        auto push_directory = [&state, push_entry]() {
                struct FSEntry *result = push_entry();

                result->physical_offset = 0;
                result->next = state->directories;
                state->directories = result;

                return result;
        };

        auto pop_directory = [&state]() {
                struct FSEntry *result = state->directories;
                if (result) {
                        state->directories = result->next;
                }

                return result;
        };

        auto push_file = [&state, push_entry]() {
                struct FSEntry *result = push_entry();

                result->next = state->files;
                state->files = result;

                return result;
        };

        auto discard_entry = [&state](struct FSEntry *entry) {
                entry->next = state->free_entry;
                state->free_entry = entry;
        };

        struct FSEntry *entry = push_directory();
        entry->path = root_dir_path;

        // TODO(nicolas) is this a thing? stdout is actually a stream so
        // you should not have to build a line unless you're
        // converting from some other datatype (uint's etc...)  or
        // doing some type conversion/formatting (dec to hexa etc...)
        class TextLine
        {
              public:
                struct BufferRange linebuffer;
                char *memory;
                void (*print)(char const *);
                bool trace_on;

                TextLine(void (*print)(char const *),
                         char *memory,
                         size_t memory_size,
                         bool trace_on)
                    : memory(memory), print(print), trace_on(trace_on)
                {
                        stream_on_memory(&linebuffer, (uint8_t *)memory,
                                         memory_size);
                }

                ~TextLine()
                {
                        if (string_terminate(&linebuffer)) {
                                if (trace_on) {
                                        print(memory);
                                }
                        }
                }

                // TODO(nicolas) make and use a generic string_cat here

                TextLine &operator<<(char const *str)
                {
                        string_cat(&linebuffer, str);
                        return *this;
                }

                TextLine &operator<<(uint8_t c)
                {
                        string_cat_uint32(&linebuffer, c);
                        return *this;
                }

                TextLine &operator<<(long long ll)
                {
                        string_cat_formatted(&linebuffer, "%lld", ll);
                        return *this;
                }

                TextLine &operator<<(unsigned long ll)
                {
                        string_cat_formatted(&linebuffer, "%lu", ll);
                        return *this;
                }

                TextLine &operator<<(uint32_t i)
                {
                        string_cat_uint32(&linebuffer, i);
                        return *this;
                }

                TextLine &operator<<(uint64_t ii)
                {
                        string_cat_formatted(&linebuffer, "%llu", ii);
                        return *this;
                }
        };

        char linebuffer_memory[256];
#define ERROR                                                                  \
        (TextLine(error_print, linebuffer_memory, sizeof linebuffer_memory,    \
                  trace_on))
#define TRACE                                                                  \
        (TextLine(trace_print, linebuffer_memory, sizeof linebuffer_memory,    \
                  trace_on))

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
        auto record_directory_entry = [&linebuffer_memory, arena, push_file,
                                       push_directory, trace_on](
            char const *dir_path, struct DirEntryAttributes const *entry) {
                char const *name = (char *)((uint8_t *)&entry->nameinfo) +
                                   entry->nameinfo.attr_dataoffset;

                auto path_size = cstr_len(dir_path) + 1 + cstr_len(name) + 1;
                uint8_t *path = push_array_rvalue(arena, path, path_size);
                {
                        struct BufferRange path_buffer;
                        stream_on_memory(&path_buffer, path, path_size);
                        string_cat(&path_buffer, dir_path);
                        string_cat(&path_buffer, "/");
                        string_cat(&path_buffer, name);
                        assert(string_terminate(&path_buffer),
                               "unexpected size");
                }
                char *path_cstr = (char *)path;

                uint64_t physical_offset = 0;
                {
                        auto trace = TRACE;

                        trace << name;
                        if (entry->obj_type == VREG) {
                                trace << "\t[f]";
                        } else if (entry->obj_type == VDIR) {
                                trace << "\t[d]";
                        } else {
                                // not a file, not a directory
                                string_cat_formatted(&trace.linebuffer,
                                                     "\t[%d]", entry->obj_type);
                        }

                        trace << "\t";

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
                                trace << "NFS";
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_HFS) {
                                trace << "HFS";
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_AFP) {
                                trace << "AFP";
                                mounted_share = true;
                        } else if (entry->obj_tag == VT_CIFS) {
                                trace << "CIFS";
                                mounted_share = true;
                        } else {
                                trace << entry->obj_tag;
                        }

                        if (mounted_share) {
                                ERROR << "path '" << path_cstr
                                      << "' is a mounted share.";
                        }

                        if (entry->obj_type == VREG) {
                                trace << "\t" << entry->file_ioblocksize;

                                trace << "\t" << entry->file_totalsize;

                                if (!supports_semicolon) {
                                        auto contains_colon =
                                            [](char const *cstr) {
                                                    while (*cstr != ':' &&
                                                           *cstr != 0)
                                                            cstr++;
                                                    return *cstr == ':';
                                            };
                                        if (contains_colon(path_cstr)) {
                                                ERROR
                                                    << "path: '" << path_cstr
                                                    << "'  contains a colon, "
                                                       "which is not supported "
                                                       "by vnode tag type";
                                        }
                                }

                                int const fd = open(path_cstr, O_RDONLY, 0);
                                // TODO(nicolas) check that we have
                                // permissions to open that file
                                // at all
                                assert(fd >= 0, "open file");
                                DEFER(close(fd));

                                if (entry->file_totalsize > 0) {

                                        struct log2phys filephys;
                                        int fcntl_result =
                                            fcntl(fd, F_LOG2PHYS, &filephys);
                                        assert(fcntl_result >= 0,
                                               "could not get "
                                               "phys");
                                        if (fcntl_result < 0) {
                                                error_print("could not "
                                                            "get phys");
                                        } else {
                                                physical_offset =
                                                    filephys.l2p_devoffset;
                                                trace << "\t"
                                                      << filephys.l2p_devoffset;
                                        }
                                }
                        }
                }

                if (entry->obj_type == VREG) {
                        struct FSEntry *file_entry = push_file();
                        file_entry->path = path_cstr;
                        file_entry->physical_offset = physical_offset;
                } else if (entry->obj_type == VDIR) {
                        struct FSEntry *dir_entry = push_directory();
                        dir_entry->path = path_cstr;
                }

        };

        struct FSEntry *dir_entry;
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
                                ERROR
                                    << "could not find directory: " << dir_path;
                                return nullptr;
                        }
                }
                DEFER({ close(dir_fd); });

                TRACE << "\nlisting directory: " << dir_path;

                uint32_t entrycount = 0;
                TRACE << "name"
                      << "\t"
                      << "type"
                      << "\t"
                      << "tag"
                      << "\t"
                      << "ioblocksize"
                      << "\t"
                      << "size"
                      << "\t"
                      << "physical location";

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

        assert(state->directories == nullptr, "no more directories");

        size_t entry_array_count = 0;
        struct FSEntry *entry_array = nullptr;
        {
                size_t file_count = 0;
                for (struct FSEntry *fs_entry = state->files; fs_entry;
                     fs_entry = fs_entry->next) {
                        file_count++;
                }
                TRACE << "found " << file_count << " files";

                entry_array_count = file_count;
                entry_array =
                    push_array_rvalue(arena, entry_array, entry_array_count);

                for (struct FSEntry *fs_entry = state->files,
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
                        struct FSEntry fs_entry_value;
                        struct FSEntryInBucket *next;
                };

                struct FSEntryInBucket *bucket_entries =
                    push_array_rvalue(arena, bucket_entries, entry_array_count);

                // circular linked-list per bucket, this bucket is pointing
                // to the last element
                struct {
                        struct FSEntryInBucket *last;
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

                                struct FSEntryInBucket *bucket_entry =
                                    &bucket_entries[bucket_entry_index++];
                                bucket_entry->fs_entry_value = *fs_entry;

                                struct FSEntryInBucket *current_last_entry =
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

                        struct FSEntry *fs_entry = entry_array;
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
                        assert(this_offset >= previous_offset,
                               "file entries should be monotonic");
                        previous_offset = this_offset;
                }
#endif

                for (size_t i = 0; i < entry_array_count; i++) {
                        TRACE << "A" << i << "\t"
                              << entry_array[i].physical_offset << "\t"
                              << entry_array[i].path;
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
                for (size_t i = 0; i < count; i++) {
                        result->paths[i] = entry_array[i].path;
                        result->filenames[i] =
                            1 + cstr_last_occurrence(entry_array[i].path, '/');
                }
        }

        return result;

#undef ERROR
#undef TRACE
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
                        assert(0 == dlclose(library->dlhandle),
                               "could not close library");
                }
                void *dlhandle =
                    dlopen(library->file_path, RTLD_NOW | RTLD_LOCAL);
                assert(dlhandle, "could not open libzwei.dylib");
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
        assert(argc > 0, "unexpected argc");
        DEFER(trace_print("done"));

        CheckMimeMessageFn *check_mime_message;
        ParseZoeMailstorePathFn *parse_zoe_mailstore_path;

        struct LoadedLibrary zwei_app_library = {};
        {
                struct BufferRange buffer;
                stream_on_memory(&buffer, (uint8_t *)zwei_app_library.file_path,
                                 sizeof zwei_app_library.file_path);
                string_cat(&buffer, argv[0],
                           cstr_last_occurrence(argv[0], '/'));
                string_cat(&buffer, "/libzwei.dylib");
                if (!string_terminate(&buffer)) {
                        return 1;
                }

                struct LoadedLibrary *lib = &zwei_app_library;
                lib->dlhandle = 0;
                lib->file_mtime = 0;
        }

        auto refresh_zwei_app = [&zwei_app_library, &check_mime_message,
                                  &parse_zoe_mailstore_path]() {
                struct LoadedLibrary *lib = &zwei_app_library;
                bool was_loaded = !!lib->dlhandle;
                if (refresh_library(lib)) {
                        if (was_loaded) {
                                trace_print("reloaded library!");
                        }
                        check_mime_message =
                            reinterpret_cast<decltype(check_mime_message)>(
                                dlsym(lib->dlhandle, "check_mime_message"));
                        assert(check_mime_message,
                               "expected symbol check_mime_message");

                        parse_zoe_mailstore_path = reinterpret_cast<decltype(
                            parse_zoe_mailstore_path)>(
                            dlsym(lib->dlhandle, "parse_zoe_mailstore_path"));
                        assert(parse_zoe_mailstore_path,
                               "expected symbol parse_zoe_mailstore_path");

                        return true;
                }

                return false;
        };

        refresh_zwei_app();

        char const *root_dir_path = nullptr;

        auto USAGE = "<program> [--help|--ls] --root-dir <root-dir>";

        auto directory_listing_on = false;
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
                        // FEATURE(nicolas): Optionally prints all files in <root-dir> using `--ls`
                        directory_listing_on = true;
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
                        char linebuffer_memory[256];
                        struct BufferRange linebuffer;
                        stream_on_memory(&linebuffer,
                                         (uint8_t *)linebuffer_memory,
                                         sizeof linebuffer_memory);
                        string_cat(&linebuffer, "bytes used after query: ");
                        string_cat_formatted(&linebuffer, "%lld",
                                             (&dc_arena)->used);
                        if (string_terminate(&linebuffer)) {
                                trace_print(linebuffer_memory);
                        }
                }

                // TODO(nicolas) always measure bytes/sec or sec/MB and
                // print out total bytes
                char linebuffer_memory[256];
                for (size_t i = 0; i < all_files->count; i++) {
                        struct BufferRange line;
                        stream_on_memory(&line, (uint8_t *)linebuffer_memory,
                                         sizeof linebuffer_memory);
                        string_cat(&line, "A");
                        string_cat_formatted(&line, "%lld", i);
                        string_cat(&line, " ");
                        string_cat(&line, all_files->paths[i]);
                        string_cat(&line, ":");
                        if (string_terminate(&line)) {
                                trace_print(linebuffer_memory);
                        }

                        stream_on_memory(&line, (uint8_t *)linebuffer_memory,
                                         sizeof linebuffer_memory);
                        string_cat(&line, "SHA1");
                        string_cat(&line, "\t");

                        // arena for our file streaming

                        // TODO(nicolas) a dedicated
                        // datastructure to store all our
                        // streams, able to discard them on
                        // exit, allocate more than one buffer
                        // at a time, and reuse unused ones.
                        //
                        // TODO(nicolas) a task queue that goes with it,
                        // to compute values on a series of
                        // blocks.
                        struct BufferRange file_content;

                        struct MemoryArena stream_arena = memory_arena(
                            dc_arena.base + dc_arena.size, KILOBYTES(32));

                        inputstream_on_filepath(&stream_arena, &file_content,
                                                all_files->paths[i]);

                        auto sha1_result = sha1(&file_content);
                        inputstream_finish(&file_content);

                        if (failed(sha1_result)) {
                                string_cat(&line, "<failed to compute result>");
                        } else {
                                assert(file_content.error == BR_ReadPastEnd,
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
                                        string_cat_formatted(
                                            &line, "%c%c",
                                            byteToHexChar[byte >> 4],
                                            byteToHexChar[byte & 0xF]);
                                }
                        }
                        if (string_terminate(&line)) {
                                trace_print(linebuffer_memory);
                        }

                        auto const filename = all_files->filenames[i];
                        auto const filepath = all_files->paths[i];

                        struct ZoeMailStoreFile zoefile;
                        auto zoefile_errorcode =
                            parse_zoe_mailstore_path(&zoefile, filename);

                        if (0 == zoefile_errorcode ||
                            cstr_endswith(filename, ".eml")) {
                                // FEATURE(nicolas): print filing timestamp
                                struct BufferRange line;

                                stream_on_memory(&line,
                                                 (uint8_t *)linebuffer_memory,
                                                 sizeof linebuffer_memory);
                                string_cat(&line, "PATH");
                                string_cat(&line, "\t");
                                string_cat(&line, filepath);
                                if (string_terminate(&line)) {
                                        trace_print(linebuffer_memory);
                                }

                                if (0 == zoefile_errorcode) {
                                        stream_on_memory(
                                            &line, (uint8_t *)linebuffer_memory,
                                            sizeof linebuffer_memory);
                                        string_cat(&line, "UUID");
                                        string_cat(&line, "\t");
                                        for (size_t i = 0;
                                             i < NCOUNT(zoefile.uuid); i++) {
                                                string_cat_formatted(
                                                    &line, "%x",
                                                    zoefile.uuid[i]);
                                        }
                                        if (string_terminate(&line)) {
                                                trace_print(linebuffer_memory);
                                        }

                                        stream_on_memory(
                                            &line, (uint8_t *)linebuffer_memory,
                                            sizeof linebuffer_memory);
                                        string_cat(&line, "UNIX TIMESTAMP");
                                        string_cat(&line, "\t");
                                        string_cat_formatted(
                                            &line, "%llu",
                                            zoefile.unix_epoch_millis);
                                        if (string_terminate(&line)) {
                                                trace_print(linebuffer_memory);
                                        }
                                }

                                do {
                                        struct MemoryArena stream_arena =
                                            memory_arena(dc_arena.base +
                                                             dc_arena.size,
                                                         KILOBYTES(32));

                                        inputstream_on_filepath(&stream_arena,
                                                                &file_content,
                                                                filepath);
                                        check_mime_message(&file_content);
                                        inputstream_finish(&file_content);
                                } while (refresh_zwei_app());
                        } else {
                                trace_print("ignored file");
                        }
                }
        }

        return 0;
}

#include "zwei_osx_logging.cpp"

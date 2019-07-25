static constexpr auto USAGE =
    "<program> "
    "[--help|--ls|--can-ignore|--debug|--date-first "
    "<YYYY-MM-DD>] --root-dir <root-dir>"
    "\n"
    "root-dir: [<filepath>|sftp:<username>@<host>[:<port>]]";

/**
   NOTE(nicolas)

   We want to read from a root directory a series of small files in
   the fastest possible way.

   These files may either be stored locally or on a remote drive.

   We extract features from these files in memory.

   TODO(nicolas): FEATURE on OSX, use open command or similar to open a mail in
   Mail.app
   TODO(nicolas): control parallelism using sysctl to obtain
   informations about local machine.
   TODO(nicolas): NEXT review zwei_files API in light of sftp file loading
   TODO(nicolas): support sftp root directories
*/

#include "zwei_inlines.hpp"

#include "zwei_files.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_iobuffer_inlines.hpp"
#include "zwei_logging.hpp"
#include "zwei_text.hpp"
#include "zwei_textapp.cpp"
#include "zwei_types.hpp"

#include "zwei_app.hpp"

#include "algos.hpp"

#define SFTP_API [[gnu::unused]] static
#include "../modules/uu.spdr/include/spdr/spdr.hh"
#include "sftp.cpp"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>

zw_global SPDR_Context *global_spdr = nullptr;
zw_global bool global_can_ignore_file = false;
#include <mach/mach.h> // for vm_allocate
#include <sys/vnode.h> // for enum vtype

#include <cerrno>
#include <sys/attr.h>

void assign_at(PlatformFileList *list,
               size_t file_index,
               char const *input_path,
               size_t input_path_size,
               size_t filesize,
               MemoryArena *result_arena)
{
        ByteCountedRange path = {
            (uint8_t *)push_bytes(result_arena, 1 + input_path_size),
            input_path_size,
        };
        *algos::copy_n(input_path, input_path_size, path.first) = 0;

        uint8_t *filename =
            algos::find_backward(path.first, path.first + path.count, '/');
        // TODO(nicolas): is this skipping a pattern?
        filename = *filename == '/' ? filename + 1 : filename;

        fatal_ifnot(file_index < list->entries_size, "capacity");
        auto &entry = list->entries_first[file_index];
        entry.path = (char const *)path.first;
        entry.filename = (char const *)filename;
        entry.filesize = filesize;
}

/**
   Query a directory and its sub-directories for all their files,
   sorted to maximize throughput when streaming their content.

   @return null or a valid file list allocated in the given arena

   // TODO(nicolas): this seems like it should be part of a platform API
*/
zw_internal PLATFORM_QUERY_ALL_FILES(directory_query_all_files)
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

        // TODO(nicolas) TAG(performance) we're nowhere near
        // saturating the I/O there, especially when querying
        // a network shared drive.

        // TODO(nicolas) compare performance w/ CIFS and AFP network shares
        // TODO(nicolas) how to minimize seeking of a NAS/mounted disk?

        // NOTE(nicolas): getattrlistbulk is only supported on MacOSX >= 10.10

        struct FSEntry;

        struct State {
                FSEntry *free_entry = nullptr;
                FSEntry *directories = nullptr;
                FSEntry *files = nullptr;
        };

        struct State *state = push_typed(&work_arena, State);
        struct FSEntry {
                struct FSEntry *next;
                char const *path;
                uint64_t physical_offset; // for files
                uint64_t size;
        };

        auto push_entry = [&state, &work_arena]() {
                FSEntry *result;

                if (!state->free_entry) {
                        result = push_typed(&work_arena, FSEntry);
                } else {
                        result = state->free_entry;
                        state->free_entry = state->free_entry->next;
                }

                return result;
        };

        auto discard_entry = [&state](FSEntry *entry) {
                entry->next = state->free_entry;
                state->free_entry = entry;
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

        FSEntry *entry = push_directory();
        entry->path = root_dir_path;

        auto trace_output = textoutputgroup_allocate(&work_arena, KILOBYTES(1));

        auto trace_optionally = [&trace_output, trace_on]() {
                if (trace_on) {
                        trace(trace_output);
                } else {
                        clear(trace_output);
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
        auto record_directory_entry = [&trace_output, trace_optionally,
                                       &work_arena, push_file, push_directory](
                                          char const *dir_path,
                                          DirEntryAttributes const *entry) {
                char const *name = (char *)((uint8_t *)&entry->nameinfo) +
                                   entry->nameinfo.attr_dataoffset;

                auto path_size = cstr_len(dir_path) + 1 + cstr_len(name) + 1;
                char *path = push_array_rvalue(&work_arena, path, path_size);
                {
                        auto const buffer_last = path + path_size;
                        char *string_last = path;
                        cstr_cat(string_last, buffer_last, dir_path);
                        cstr_cat(string_last, buffer_last, "/");
                        cstr_cat(string_last, buffer_last, name);
                        zw_assert(cstr_terminate(string_last, buffer_last),
                                  "unexpected size");
                }
                bool ignore_file = false;

                uint64_t physical_offset = 0;
                {
                        push_cstr(trace_output, name);
                        push_tab(trace_output);
                        if (entry->obj_type == VREG) {
                                push_cstr(trace_output, "[f]");
                        } else if (entry->obj_type == VDIR) {
                                push_cstr(trace_output, "[d]");
                        } else {
                                // not a file, not a directory
                                push_formatted(trace_output, "[%d]",
                                               entry->obj_type);
                        }
                        push_tab(trace_output);

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
                                push_cstr(trace_output, "NFS");
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_HFS) {
                                push_cstr(trace_output, "HFS");
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_AFP) {
                                push_cstr(trace_output, "AFP");
                                mounted_share = true;
                        } else if (entry->obj_tag == VT_CIFS) {
                                push_cstr(trace_output, "CIFS");
                                mounted_share = true;
                        } else {
                                push_u32(trace_output, entry->obj_tag);
                        }

                        if (mounted_share) {
                                MemoryArena temp_arena = work_arena;
                                auto errorg = textoutputgroup_allocate(
                                    &temp_arena, KILOBYTES(1));
                                push_cstr(errorg, "path '");
                                push_cstr(errorg, path);
                                push_cstr(errorg, "' is a mounted share.");
                                error(errorg);
                        }

                        if (entry->obj_type == VREG) {
                                push_tab(trace_output);
                                push_u32(trace_output, entry->file_ioblocksize);
                                push_tab(trace_output);
                                push_u64(trace_output, entry->file_totalsize);

                                if (!supports_semicolon &&
                                    global_can_ignore_file) {
                                        if (':' == *cstr_find(path, ':')) {
                                                MemoryArena temp_arena =
                                                    work_arena;
                                                auto errorg =
                                                    textoutputgroup_allocate(
                                                        &temp_arena,
                                                        KILOBYTES(1));
                                                push_cstr(errorg, "path: '");
                                                push_cstr(errorg, path);
                                                push_cstr(
                                                    errorg,
                                                    "'  contains a colon, "
                                                    "which is not supported "
                                                    "by vnode tag type");
                                                error(errorg);
                                                ignore_file = true;
                                        }
                                }

                                int const fd = open(path, O_RDONLY, 0);
                                if (fd < 0) {
                                        if (errno == ENOENT) {
                                                // the file actually does not
                                                // actually exist. This happened
                                                // on SMB shares for file
                                                // entries
                                                // with invalid MAC filenames.
                                                ignore_file = true;
                                        }
                                }
                                // TODO(nicolas) permissions to open
                                // that file at all?
                                zw_assert(ignore_file || fd >= 0, "open file");
                                DEFER(fd >= 0 && close(fd));

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
                                                push_cstr(trace_output, "\t");
                                                push_u64(
                                                    trace_output,
                                                    filephys.l2p_devoffset);
                                        }
                                }
                        }
                        trace_optionally();
                }

                if (entry->obj_type == VREG && !ignore_file) {
                        FSEntry *file_entry = push_file();
                        file_entry->path = path;
                        file_entry->physical_offset = physical_offset;
                        file_entry->size = entry->file_totalsize;
                } else if (entry->obj_type == VDIR) {
                        FSEntry *dir_entry = push_directory();
                        dir_entry->path = path;
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
                                MemoryArena temp_arena = work_arena;
                                auto errorg = textoutputgroup_allocate(
                                    &temp_arena, KILOBYTES(1));
                                push_cstr(errorg, "could not find directory: ");
                                push_cstr(errorg, dir_path);
                                error(errorg);
                                return nullptr;
                        }
                }
                DEFER({ close(dir_fd); });

                push_newline(trace_output);
                push_cstr(trace_output, "listing directory: ");
                push_cstr(trace_output, dir_path);
                trace_optionally();

                // TODO(nicolas): push sep by "\t"
                push_cstr(trace_output, "name");
                push_tab(trace_output);
                push_cstr(trace_output, "type");
                push_tab(trace_output);
                push_cstr(trace_output, "tag");
                push_tab(trace_output);
                push_cstr(trace_output, "ioblocksize");
                push_tab(trace_output);
                push_cstr(trace_output, "size");
                push_tab(trace_output);
                push_cstr(trace_output, "physical location");
                trace_optionally();

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

        zw_assert(state->directories == nullptr,
                  "we should visit all directories");

        size_t entry_array_count = 0;
        FSEntry *entry_array = nullptr;
        {
                size_t file_count = 0;
                for (FSEntry *fs_entry = state->files; fs_entry;
                     fs_entry = fs_entry->next) {
                        file_count++;
                }

                push_cstr(trace_output, "found ");
                push_u64(trace_output, file_count);
                push_cstr(trace_output, " files");
                trace_optionally();

                entry_array_count = file_count;
                entry_array = push_array_rvalue(&work_arena, entry_array,
                                                entry_array_count);

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

                FSEntryInBucket *bucket_entries = push_array_rvalue(
                    &work_arena, bucket_entries, entry_array_count);

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
        }

        for (size_t i = 0; i < entry_array_count; i++) {
                push_cstr(trace_output, "FILE");
                push_u64(trace_output, i);
                push_tab(trace_output);
                push_u64(trace_output, entry_array[i].physical_offset);
                push_tab(trace_output);
                push_cstr(trace_output, entry_array[i].path);
                trace_optionally();
        }

        // construct file list from array
        PlatformFileList *result = push_pointer_rvalue(result_arena, result);
        *result = {};
        result->entries_size = entry_array_count;

        if (result->entries_size) {
                result->entries_first = push_array_rvalue(
                    result_arena, result->entries_first, result->entries_size);
                for (size_t i = 0; i < result->entries_size; i++) {
                        char const *input_path = entry_array[i].path;
                        size_t input_path_n = cstr_len(input_path);
                        assign_at(result, i, input_path, input_path_n,
                                  entry_array[i].size, result_arena);
                }
        }

        return result;
}

struct SFTPHostData {
        char const *username_first;
        size_t username_size;
        char const *domain_name_first;
        size_t domain_name_size;
        uint16_t port = 22;

        char const *error_msg;
};

// scan [first,last) and returns where to read username, domain and port
zw_internal char const *parse_sftp_host_data(char const *first,
                                             char const *last,
                                             SFTPHostData *d_result)
{
        auto &result = *d_result;
        result = {};
        auto username_last = algos::find_if(first, last, [](char const x) {
                auto valid = (x >= 0x30 && x < 0x40) || (x >= 0x41 && x < 0x7f);
                return !valid;
        });
        if (username_last == first) {
                result.error_msg = "Expected username";
                return first;
        }
        result.username_first = first;
        result.username_size = username_last - first;

        first = username_last;
        if (first == last || *first != '@') {
                result.error_msg = "Expected @ after username";
                return first;
        }
        ++first;

        auto const parse_many_dtext = [](char const *first, char const *last) {
                auto const dtext_char = [](char const x) {
                        return (x >= 33 && x < 47) || (x >= 48 && x < 58) ||
                               (x >= 59 && x < 90) || (x >= 94 && x < 126);
                };
                return algos::find_if(first, last, [dtext_char](char const x) {
                        return !dtext_char(x);
                });
        };

        auto domain_name_first = first;
        auto dtext_last = parse_many_dtext(first, last);
        if (dtext_last == first) {
                result.error_msg = "Expected domain name part";
                return first;
        }
        first = dtext_last;
        while (first != last && *first == '.') {
                ++first;
                dtext_last = parse_many_dtext(first, last);
                if (dtext_last == first) {
                        result.error_msg = "expected domain fragment after .";
                }
                first = dtext_last;
        }
        result.domain_name_first = domain_name_first;
        result.domain_name_size = dtext_last - domain_name_first;

        if (first != last && *first == ':') {
                ++first;
                auto port_last = algos::find_if(first, last, [](char const x) {
                        return !(x >= 48 && x < 58);
                });
                if (first == port_last) {
                        result.error_msg = "Expected port number";
                        return first;
                }
                result.port = 0;
                while (first != port_last) {
                        result.port *= 10;
                        result.port += *first - 48;
                        ++first;
                }
        }
        return first;
}

zw_internal void sftp_client_open(sftp::SFTPClient *sftp_client,
                                  SFTPHostData const &host_data)
{
        sftp_client->username = host_data.username_first;
        sftp_client->username_size = host_data.username_size;
        sftp_client->hostname = host_data.domain_name_first;
        sftp_client->hostname_size = host_data.domain_name_size;
        sftp_client->port = host_data.port;
        SFTPClientOpen(sftp_client);
}

// TODO(nicolas): querying files via sftp isn't as fast as doing it in bulk on a
// local file system. So the trade-off I chose to first get all the files then
// do the loading isn't a good one in that scenario.
zw_internal PLATFORM_QUERY_ALL_FILES(sftp_query_all_files)
{
        SFTPHostData host_data;
        auto root_dir_first = root_dir_path;
        auto root_dir_last = root_dir_path + cstr_len(root_dir_path);
        root_dir_first =
            parse_sftp_host_data(root_dir_first, root_dir_last, &host_data);
        if (host_data.error_msg) {
                error_print(host_data.error_msg);
                error_print(root_dir_first); // where this happened
                return nullptr;
        }
        if (root_dir_first == root_dir_last) {
                error_print("missing root dir");
                error_print(root_dir_first);
                return nullptr;
        }
        if (*root_dir_first != '/') {
                error_print("root dir must start with /");
                return nullptr;
        }

        sftp::SFTPClient sftp_client = {};
        sftp_client_open(&sftp_client, host_data);
        DEFER(SFTPClientClose(&sftp_client));
        DEFER(auto x = sftp_client.error_msg; if (x) error_print(x));
        if (sftp_client.error != sftp::SFTPClient::Error_None) {
                error_print("could not open sftp session");
                if (auto x = sftp_client.error_msg)
                        error_print(x);
        }

        struct Directory {
                char const *path_first;
                size_t path_size;
                Directory *next;
        };

        struct File {
                char const *path_first;
                size_t path_size;
                size_t filesize;
                File *next;
        };

        auto const directory_next = [](Directory const *x) { return x->next; };
        auto const file_next = [](File const *x) { return x->next; };

        auto const directory_insert_after = [](Directory *needle,
                                               Directory *a) {
                a->next = needle->next;
                needle->next = a;
        };
        auto const file_insert_after = [](File *needle, File *a) {
                a->next = needle->next;
                needle->next = a;
        };

        Directory directories_sentinel = {};
        auto directories_to_visit_last = &directories_sentinel;
        Directory root_dir = {root_dir_first,
                              size_t(root_dir_last - root_dir_first),
                              directories_to_visit_last};
        auto directories_to_visit_first = &root_dir;

        File files_sentinel = {};
        files_sentinel.next = &files_sentinel;
        auto files_last = &files_sentinel;
        size_t files_size = 0;

        while (directories_to_visit_first != directories_to_visit_last) {
                auto const &directory = *directories_to_visit_first;
                fprintf(stdout, "querying remote directory %.*s\n",
                        int(directory.path_size), directory.path_first);
                sftp::SFTPClientListFiles list_files = {};
                list_files.path = directory.path_first;
                list_files.path_size = directory.path_size;
                SFTPClientListFilesOpen(&list_files, &sftp_client);
                DEFER(
                    auto x = list_files.error;
                    if (x != sftp::SFTPClientListFiles::Error_None &&
                        x != sftp::SFTPClientListFiles::Error_NoMoreListEntry) {
                            error_print(list_files.error_msg);
                    });
                DEFER(SFTPClientListFilesClose(&list_files));

                char filename[1024];
                size_t filename_size = 0;
                char long_list_name[4096];
                size_t long_list_name_size = 0;
                {
                        auto d = &list_files.d_entry;
                        d->filename_buffer = filename;
                        d->filename_buffer_size = sizeof filename;
                        d->filename_size = &filename_size;
                        d->long_list_name_buffer = long_list_name;
                        d->long_list_name_buffer_size = sizeof long_list_name;
                        d->long_list_name_size = &long_list_name_size;
                }

                while (SFTPClientListFilesNext(&list_files) ==
                       sftp::SFTPClientListFiles::Error_None) {
                        auto const &entry = list_files.d_entry;
                        if (filename[0] == '.')
                                continue;

                        switch (entry.entry_type) {
                        case sftp::SFTPClientListFiles::EntryType_Directory: {
                                size_t path_size = list_files.path_size +
                                                   filename_size + 1 + 1;
                                char *path = push_array_rvalue(
                                    &work_arena, path, path_size + 1);
                                auto d_last = path + path_size + 1;
                                auto d = path;
                                cstr_cat(d, d_last, list_files.path,
                                         list_files.path +
                                             list_files.path_size);
                                cstr_cat(d, d_last, "/");
                                cstr_cat(d, d_last, filename,
                                         filename + filename_size);
                                cstr_cat(d, d_last, "/");
                                if (cstr_terminate(d, d_last)) {
                                        auto directory =
                                            push_typed(&work_arena, Directory);
                                        directory->path_first = path;
                                        directory->path_size = path_size;
                                        directory_insert_after(
                                            directories_to_visit_first,
                                            directory);
                                }
                                break;
                        }
                        case sftp::SFTPClientListFiles::EntryType_File: {
                                size_t path_size = list_files.path_size +
                                                   filename_size + 1 + 1;
                                char *path = push_array_rvalue(&work_arena,
                                                               path, path_size);
                                auto d_last = path + path_size;
                                auto d = path;
                                cstr_cat(d, d_last, list_files.path,
                                         list_files.path +
                                             list_files.path_size);
                                cstr_cat(d, d_last, "/");
                                cstr_cat(d, d_last, filename,
                                         filename + filename_size);
                                if (cstr_terminate(d, d_last)) {
                                        auto file =
                                            push_typed(&work_arena, File);
                                        file->path_first = path;
                                        file->path_size = path_size;
                                        file->filesize =
                                            list_files.d_entry.file_size;
                                        file_insert_after(files_last, file);
                                        ++files_size;
                                }
                        }
                        default:
                                break;
                        }

                        std::printf("[%d] %.*s (%llu)\n",
                                    list_files.d_entry.entry_type,
                                    int(long_list_name_size), long_list_name,
                                    list_files.d_entry.file_size);
                }
                directories_to_visit_first =
                    directory_next(directories_to_visit_first);
        }
        auto files_first = file_next(files_last);
        std::printf("found %zu files\n", files_size);

        PlatformFileList *result = push_pointer_rvalue(result_arena, result);
        *result = {};
        result->entries_size = files_size;
        result->entries_first = push_array_rvalue(
            result_arena, result->entries_first, result->entries_size);
        {
                size_t file_index = 0;
                for (auto file = files_first; file != files_last;
                     file = file_next(file)) {
                        assign_at(result, file_index++, file->path_first,
                                  file->path_size, file->filesize,
                                  result_arena);
                }
        }
        return result;
}

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

static int digits_count(char const *cp)
{
        int n = 0;
        while (*cp) {
                auto delta = ((unsigned)*cp) - (unsigned)'0';
                if (delta > 9) {
                        break;
                }
                ++n;
                ++cp;
        }
        return n;
}

template <typename I0, typename I1> I0 ascii_to_integer(I0 first, I1 *dest)
{
        int d10_n = digits_count(first);
        if (d10_n == 0 || d10_n > std::numeric_limits<I1>::digits10) {
                return first; // error
        }
        int result = 0;
        while (d10_n--) {
                auto delta = ((unsigned)*first) - (unsigned)'0';
                if (delta > 9) {
                        break;
                }
                result *= 10;
                result += delta;
                ++first;
        }
        *dest = result;
        return first;
}

static char const *parse_date(char const *cp, CivilDateTime *result_)
{
        auto &result = *result_;
        cp = ascii_to_integer(cp, &result.year);
        if (*cp == '-') {
                ++cp;
                cp = ascii_to_integer(cp, &result.month_count);
                if (*cp == '-') {
                        ++cp;
                        cp = ascii_to_integer(cp, &result.day_count);
                }
        }
        return cp;
}

static uint64_t unix_epoch_millis_from_civil_date(CivilDateTime civil_date)
{
        uint64_t result = 0;
        if (civil_date.year >= 1970) {
                std::tm tm_value = {};
                tm_value.tm_mday = civil_date.day_count;
                tm_value.tm_mon = civil_date.month_count - 1;
                tm_value.tm_year = civil_date.year - 1900;
                result = 1000 * mktime(&tm_value);
        }
        return result;
}

int main(int argc, char **argv)
{
        char trace_buffer[16384];
        MemoryArena trace_arena =
            memory_arena(trace_buffer, sizeof trace_buffer);
        auto trace_output =
            textoutputgroup_allocate(&trace_arena, KILOBYTES(1));
        zw_assert(argc > 0, "unexpected argc");

        DEFER(trace_print("done"));
        char const *root_dir_path = nullptr;
        auto directory_listing_on = false;
        auto debug_mode_on = false;
        auto current_arg = 1;
        CivilDateTime date_first = {};
        CivilDateTime date_last = {};
        date_last.year = 9999;
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
                } else if (cstr_equals(argv[current_arg], "--date-first")) {
                        // FEATURE(nicolas): only process messages filed after
                        // the given date.
                        current_arg++;
                        if (current_arg == argc) {
                                error_print("expected date");
                                fprintf(stderr, USAGE);
                                return 1;
                        }
                        auto date_string =
                            const_cast<char const *>(argv[current_arg]);
                        ++current_arg;

                        auto cp = date_string;
                        cp = parse_date(cp, &date_first);
                        CivilDateTime null_time = {};
                        if (*cp || date_first == null_time) {
                                error_print("expected date at: ");
                                error_print(date_string);
                                fprintf(stderr, USAGE);
                                return 1;
                        }
                        cp = parse_date(cp, &date_last);
                } else {
                        error_print("unexpected argument");
                        fprintf(stderr, USAGE);
                        return 1;
                        current_arg++;
                }
        }

        push_formatted(
            trace_output, "Selected date range <%d-%02d-%02d> <%d-%02d-%02d>",
            date_first.year, date_first.month_count, date_first.day_count,
            date_last.year, date_last.month_count, date_last.day_count);
        trace(trace_output);

        void *permanent_storage = nullptr;
        auto permanent_storage_memory_size = GIGABYTES(1);
        void *transient_storage = nullptr;
        auto transient_storage_memory_size = MEGABYTES(512);
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

        ProgramResources program_resources = {};
        program_resources.permanent_arena =
            memory_arena(permanent_storage, permanent_storage_memory_size);
        program_resources.transient_arena =
            memory_arena(transient_storage, transient_storage_memory_size);

        MemoryArena *permanent_arena = &program_resources.permanent_arena;
        MemoryArena *transient_arena = &program_resources.transient_arena;

        if (spdr_init(&global_spdr, push_bytes(permanent_arena, MEGABYTES(96)),
                      MEGABYTES(96))) {
                error_print("could not initialize memory for tracing");
                return 1;
        }
        spdr_enable_trace(global_spdr, 1);
        SPDR_METADATA1(global_spdr, "thread_name", SPDR_STR("name", "main"));

        Zwei zwei = {};
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

        auto refresh_zwei_app = [&]() {
                struct LoadedLibrary *lib = &zwei_app_library;
                bool was_loaded = !!lib->dlhandle;
                if (refresh_library(lib)) {
                        if (was_loaded) {
                                trace_print("reloaded library!");
                        }

#define GRAB_FN_SYM(lvalue, symname, dlhandle)                                 \
        {                                                                      \
                lvalue = reinterpret_cast<decltype(lvalue)>(                   \
                    dlsym(dlhandle, symname));                                 \
                zw_assert(lvalue, "expected symbol " symname);                 \
        }

                        InitAppFn init_app;
                        GRAB_FN_SYM(init_app, "init_app", lib->dlhandle);
                        GRAB_FN_SYM(zwei.get_message_summary,
                                    "get_message_summary", lib->dlhandle);
                        GRAB_FN_SYM(zwei.parse_zoe_mailstore_filename,
                                    "parse_zoe_mailstore_filename",
                                    lib->dlhandle);

#undef GRAB_FN_SYM

                        Platform platform;
                        platform.spdr = global_spdr;

                        init_app(platform,
                                 debug_mode_on ? ZWEI_DEBUG_MODE_FLAG : 0,
                                 &program_resources);

                        return true;
                }

                return false;
        };

        refresh_zwei_app();

        // FEATURE(nicolas): show content of any directory recursively
        // FEATURE(nicolas): show sha-1 digests of all files inside a directory
        if (!root_dir_path) {
                error_print("you must pass a root directory with --root-dir");
        } else {
                auto sftp_root_dir_path =
                    cstr_find_prefix(root_dir_path, "sftp:");

                // NOTE(nicolas) we could process files in parallel
                // .. but how much parallelism can a HDD/SDD/Raid
                // device support?

                struct PlatformFileList *all_files = {};
                struct PlatformFileList *sftp_all_files = {};
                SFTPHostData sftp_host_data = {};
                {
                        MemoryArena dc_arena =
                            push_sub_arena(*transient_arena,
                                           transient_storage_memory_size / 2);
                        if (*sftp_root_dir_path) {
                                parse_sftp_host_data(
                                    sftp_root_dir_path,
                                    sftp_root_dir_path +
                                        cstr_len(sftp_root_dir_path),
                                    &sftp_host_data);
                                sftp_all_files = sftp_query_all_files(
                                    sftp_root_dir_path, directory_listing_on,
                                    *transient_arena, &dc_arena);
                        } else {
                                all_files = directory_query_all_files(
                                    root_dir_path, directory_listing_on,
                                    *transient_arena, &dc_arena);
                        }
                        pop_unused(*transient_arena, dc_arena);
                        {
                                MemoryArena temp_arena = *transient_arena;
                                auto traceg = textoutputgroup_allocate(
                                    &temp_arena, KILOBYTES(1));
                                push_cstr(traceg, "Bytes used after query: ");
                                push_formatted(traceg, "%lld",
                                               transient_arena->used);
                                trace(traceg);
                        }
                }

                if (!sftp_all_files && !all_files) {
                        error_print("could not query all files");
                        return 1;
                }

                MemoryArena result_arena =
                    push_sub_arena(*permanent_arena, MEGABYTES(512));
                // TODO(nicolas) always measure bytes/sec or sec/MB and
                // print out total bytes
                SPDR_BEGIN(global_spdr, "main", "processing_files");

                // Is a file_loader structure necessary? Since I/O
                // doesn't have many resources, this could be made
                // purely functional and use a global variable. At
                // least from an API point of view.
                //
                // That's less true if we're taking the data from the network?

                size_t file_size_limit = MEGABYTES(128);
                auto const detect_exclude_unwanted_files =
                    [zwei, date_first, date_last](PlatformFileList *all_files,
                                                  size_t file_size_limit) {
                            auto f = all_files->entries_first;
                            auto const l = all_files->entries_first +
                                           all_files->entries_size;
                            auto const below_limit =
                                [file_size_limit](
                                    PlatformFileList::Entry const &x) {
                                        return x.filesize < file_size_limit;
                                };

                            uint64_t filing_date_first =
                                unix_epoch_millis_from_civil_date(date_first);
                            uint64_t filing_date_last =
                                unix_epoch_millis_from_civil_date(date_last);

                            auto const wanted =
                                [zwei, filing_date_first, filing_date_last](
                                    PlatformFileList::Entry const &x) {
                                        return !should_skip_message_file(
                                            zwei, x.filename, x.path,
                                            filing_date_first,
                                            filing_date_last);
                                };
                            auto wanted_last =
                                std::stable_partition(f, l, wanted);
                            auto below_limit_last = std::stable_partition(
                                f, wanted_last, below_limit);

                            if (!global_can_ignore_file &&
                                below_limit_last != wanted_last) {
                                    zw_assert(
                                        false,
                                        "ignored too large file"); // inspect
                                    // below_limit_last
                            }
                            all_files->entries_size = below_limit_last - f;
                    };
                if (all_files)
                        detect_exclude_unwanted_files(all_files,
                                                      file_size_limit);

                std::size_t const sftp_file_size_limit = MEGABYTES(1);
                if (sftp_all_files)
                        detect_exclude_unwanted_files(sftp_all_files,
                                                      sftp_file_size_limit);
                size_t local_file_count =
                    all_files ? all_files->entries_size : 0;
                auto file_loader_arena = push_sub_arena(
                    *transient_arena, get_file_loader_allocation_size(
                                          local_file_count, file_size_limit));
                auto &files_loader =
                    create_file_loader(local_file_count, file_loader_arena.base,
                                       file_loader_arena.size);
                size_t file_index = 0;
                if (all_files) {
                        for (; file_index < all_files->entries_size;
                             ++file_index) {
                                auto const &entry =
                                    all_files->entries_first[file_index];
                                push_file(files_loader, entry.path,
                                          entry.filesize, (uint32_t)file_index);
                        }
                }

                // iterates over every file in the stream. the loop
                // can never reach the end if you don't release files
                // and memory is too constrained to load them all in
                // memory.
                //
                // the stream has to block whenever new loaded files
                // haven't appeared yet (and we haven't exhausted the
                // original list)
                //
                // Blocking may not be so nice depending on what you
                // want to do as a user.. (say show some more stuff,
                // do some other work etc...)

                struct MessageWorkTask {
                        FileLoader *files_loader;
                        MemoryArena result_arena;
                        FileLoaderHandle file_handle;
                        ProcessedMessage result;
                };

                struct Task {
                        MemoryArena arena;
                        Zwei zwei;
                        bool done;
                        enum { NONE, MESSAGE } type;
                        MessageWorkTask message_task;
                };

                Task tasks[8 * 2];
                size_t tasks_capacity = countof(tasks);
                size_t tasks_count = 0;
                for (auto &task : tasks) {
                        // TODO(nicolas): might not be enough if you
                        // want to do allocations proportional to the
                        // input file.
                        //
                        // There might be a point in doing first a
                        // pre-indexing with low-memory and only
                        // taking into account the first 1M of a
                        // message, then a full indexing in some other
                        // form later.
                        //
                        // No need to spend resources on spam for instance!
                        //
                        // Normally parsing is not memory
                        // intensive. The memory intensive part in our
                        // pipeline is the MacRoman bug workaround,
                        // that is super specific to my corpus.
                        //
                        // If I pre-applied it to my files, I wouldn't
                        // need it in this program.
                        task.arena =
                            push_sub_arena(*transient_arena, MEGABYTES(2));
                        task.done = false;
                        task.type = Task::NONE;
                }

                auto push_message_task =
                    [&zwei, &tasks, &tasks_count, &files_loader, &result_arena,
                     tasks_capacity](FileLoaderHandle const &file_handle) {
                            zw_assert(1 + tasks_count <= tasks_capacity,
                                      "overallocating");

                            auto &task = tasks[tasks_count];
                            task.done = false;
                            task.type = Task::MESSAGE;
                            task.zwei = zwei;
                            task.message_task.files_loader = &files_loader;
                            task.message_task.result_arena =
                                push_sub_arena(result_arena, KILOBYTES(32));
                            task.message_task.file_handle = file_handle;
                            task.message_task.result = {};

                            tasks_count += 1;
                    };

                auto task_run = [](void *tasks_ptr, size_t task_index) {
                        Task *tasks = static_cast<Task *>(tasks_ptr);
                        Task &task = tasks[task_index];

                        if (task.type != Task::MESSAGE)
                                return;
                        auto &message_task = task.message_task;

                        auto &zwei = task.zwei;
                        auto file = message_task.file_handle;
                        auto &files_loader = *message_task.files_loader;

                        char const *filepath = get_filepath(files_loader, file);
                        char const *filename = get_filename(files_loader, file);
                        auto message_content = get_content(files_loader, file);
                        process_message(
                            zwei, filename, filepath, begin(message_content),
                            end(message_content), task.arena,
                            message_task.result_arena, message_task.result);
                        message_task.result.fileindex =
                            get_tag(files_loader, file);
                        release_content(
                            files_loader,
                            file); // releases the file from the loader
                        task.done = true;
                };

                auto process_and_print = [&task_run, transient_arena](
                                             Task *first, Task *last) {
                        dispatch_queue_t queue = dispatch_get_global_queue(
                            DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

                        dispatch_apply_f(last - first, queue, first, task_run);

                        algos::for_each(
                            first, last, [transient_arena](Task const &x) {
                                    if (x.type != Task::MESSAGE)
                                            return;
                                    if (!x.done)
                                            return;
                                    if (!x.message_task.result.success)
                                            return;
                                    print_processed_message(
                                        x.message_task.result,
                                        *transient_arena);
                            });
                };

                auto sftp_file_first =
                    sftp_all_files ? sftp_all_files->entries_first : nullptr;
                auto sftp_file_last =
                    sftp_file_first +
                    (sftp_all_files ? sftp_all_files->entries_size : 0);
                sftp::SFTPClient sftp_client = {};
                if (sftp_file_first != sftp_file_last) {
                        sftp_client_open(&sftp_client, sftp_host_data);
                }
                DEFER(SFTPClientClose(&sftp_client));
                while (count_pending_files(files_loader) > 0 ||
                       sftp_file_first != sftp_file_last) {
                        wait_for_available_files(files_loader);
                        for (auto &file : available_files(files_loader)) {
                                if (tasks_count == tasks_capacity) {
                                        process_and_print(tasks,
                                                          tasks + tasks_count);
                                        tasks_count = 0;
                                }

                                accept(files_loader, file);
                                push_message_task(file);
                        }
                        if (sftp_file_first != sftp_file_last) {
                                MemoryArena sftp_file_arena =
                                    push_sub_arena(*transient_arena,
                                                   sftp_file_size_limit + 8192);
                                auto const &file = *sftp_file_first;
                                size_t d_size = file.filesize + 4096;
                                uint8_t *d = push_array_rvalue(&sftp_file_arena,
                                                               d, d_size);
                                sftp::SFTPClientStreamReader s = {};
                                {
                                        s.path = file.path;
                                        s.path_size = cstr_len(file.path);
                                }
                                SFTPClientStreamReaderOpen(&s, &sftp_client);
                                DEFER(SFTPClientStreamReaderClose(&s));
                                s.d_buffer = d;
                                s.d_buffer_size = d_size;
                                while (
                                    SFTPClientStreamReaderNext(&s) ==
                                    sftp::SFTPClientStreamReader::Error_None) {
                                        s.d_buffer += s.d_bytes_read;
                                        s.d_buffer_size -= s.d_bytes_read;
                                };
                                auto d_read_last = s.d_buffer;
                                auto io_success = s.error ==
                                                  sftp::SFTPClientStreamReader::
                                                      Error_NoMoreBytes;
                                if (io_success) {
                                        ProcessedMessage result = {};
                                        result.fileindex = file_index++;
                                        auto msg_arena = push_sub_arena(
                                            result_arena, KILOBYTES(8));
                                        auto temp_arena = push_sub_arena(
                                            *transient_arena, MEGABYTES(2));
                                        // TODO(nicolas): TAG(memory_leak) are
                                        // we leaking?
                                        process_message(zwei, file.filename,
                                                        file.path, d,
                                                        d_read_last, temp_arena,
                                                        msg_arena, result);
                                        if (result.success) {
                                                print_processed_message(
                                                    result, *transient_arena);
                                        }
                                        pop_unused(*transient_arena,
                                                   temp_arena);
                                        pop_unused(result_arena, msg_arena);
                                } else {
                                        error_print(s.path);
                                        if (s.error_msg)
                                                error_print(s.error_msg);
                                        if (s.client) {
                                                printf("status: %d\n",
                                                       s.client->sftp_status);
                                                if (s.client->sftp_status_msg)
                                                        error_print(
                                                            s.client
                                                                ->sftp_status_msg);
                                        }
                                }
                                pop_unused(*transient_arena, sftp_file_arena);
                                ++sftp_file_first;
                        }
                        SPDR_COUNTER1(global_spdr, "main", "process_task",
                                      SPDR_INT("value", int(tasks_count)));
                }
                process_and_print(tasks, tasks + tasks_count);
                tasks_count = 0;

                {
                        auto error = load_error_files(files_loader);
                        zw_assert(algos::range_empty(begin(error), end(error)),
                                  "expected no loading error");

                        // TODO(nicolas): check that no file in error
                        // remains. Was the error transient? Should I
                        // retry? How many times?
                }

                SPDR_END(global_spdr, "main", "processing_files");
        }

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
                MemoryArena temp_arena = *permanent_arena;
                auto tog = textoutputgroup_allocate(&temp_arena, 512);
                push_cstr(tog, "reporting traces to file: ");
                push_cstr(tog, trace_file);
                push_newline(tog);
                trace(tog);
        }
        int fd = open(trace_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        spdr_report(
            global_spdr, SPDR_CHROME_REPORT,
            [](char const *string, void *user_data) {
                    char buffer[4096];
                    MemoryArena stack = memory_arena(buffer, sizeof buffer);
                    int *fd_ptr = static_cast<int *>(user_data);
                    auto tog = textoutputgroup_allocate(&stack, 64);
                    push_cstr(tog, string);
                    text_output_group_print(*fd_ptr, *tog);
            },
            &fd);
        close(fd);
        spdr_deinit(&global_spdr);

        return 0;
}

#if 0 * ZWEI_SLOW
#include "zwei_files_unix_synchronous.cpp"
#else
#include "zwei_files_osx_async.cpp"
#endif

#include "zwei_iobuffer.cpp"
#include "zwei_osx_logging.cpp"
#include "zwei_text.cpp"
#define ZWEI_TEXTAPP_IMPLEMENTATION
#include "secure_hash_standard.cpp"
#include "zwei_textapp.cpp"

#define SFTP_IMPLEMENTATION
#include "sftp.cpp"

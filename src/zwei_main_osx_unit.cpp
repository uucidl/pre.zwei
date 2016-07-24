/**
   NOTE(nicolas)

   We want to read from a root directory a series of small files in
   the fastest possible way.

   These files may either be stored locally or on a remote drive.

   We extract features from these files in memory.

   TODO(nicolas): FEATURE search timeline ranges [date-start,date-end)
   TODO(nicolas): FEATURE on OSX, use open command or similar to open a mail in
   Mail.app
   TODO(nicolas): control parallelism using sysctl to obtain
   informations about local machine.
*/

#include "zwei_inlines.hpp"

#include "zwei_files.hpp"
#include "zwei_iobuffer.hpp"
#include "zwei_iobuffer_inlines.hpp"
#include "zwei_logging.hpp"
#include "zwei_types.hpp"

#include "zwei_app.hpp"

#include "algos.hpp"

#include "../modules/uu.spdr/include/spdr/spdr.hh"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>

zw_global SPDR_Context *global_spdr = nullptr;
zw_global bool global_can_ignore_file = false;

#include <CommonCrypto/CommonDigest.h>
#include <limits>

struct Sha1 {
        uint8_t digest[20];
};

MayFail<Sha1> sha1(IOBufferIterator *iobuffer)
{
        using Result = MayFail<Sha1>;
        Sha1 result;

        size_t const MAX_CHUNK_SIZE = std::numeric_limits<CC_LONG>::max();
        auto chunk_size = [MAX_CHUNK_SIZE](size_t size) {
                return static_cast<CC_LONG>(
                    size < MAX_CHUNK_SIZE ? size : MAX_CHUNK_SIZE);
        };

        CC_SHA1_CTX ctx;
        zw_assert(1 == CC_SHA1_Init(&ctx), "CC_SHA1_Init");

        while (iobuffer->error == IOBufferIteratorError_NoError) {
                while (iobuffer->cursor < iobuffer->end) {
                        auto size =
                            chunk_size(iobuffer->end - iobuffer->cursor);
                        zw_assert(
                            1 == CC_SHA1_Update(&ctx, iobuffer->cursor, size),
                            "CC_SHA1_Update");
                        iobuffer->cursor += size;
                }
                refill_iobuffer(iobuffer);
        }

        if (iobuffer->error == IOBufferIteratorError_ReadPastEnd) {
                zw_assert(1 == CC_SHA1_Final(result.digest, &ctx),
                          "CC_SHA1_Final");
        } else {
                return Result::failure(1);
        }

        return result;
}

#include <mach/mach.h> // for vm_allocate
#include <sys/vnode.h> // for enum vtype

struct FileList {
        size_t count;
        char const **paths;
        char const **filenames;
        struct {
                uint64_t size;
        } * attributes;
};

#include <cerrno>
#include <sys/attr.h>

/**
   Query a directory and its sub-directories for all their files,
   sorted to maximize throughput when streaming their content.

   @return null or a valid file list allocated in the given arena
*/
zw_internal struct FileList *
directory_query_all_files(char const *root_dir_path,
                          bool trace_on,
                          MemoryArena work_arena,
                          MemoryArena *result_arena)
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

        TextOutputGroup trace_output = {};
        allocate(trace_output, &work_arena, KILOBYTES(1));

        auto trace_optionally = [&trace_output, trace_on]() {
                if (trace_on) {
                        trace(trace_output);
                } else {
                        clear(trace_output);
                }
        };

        // what we are asking getattrlistbulk
        struct attrlist query_attributes = {
            ATTR_BIT_MAP_COUNT, 0,
            (ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_ERROR | ATTR_CMN_NAME |
             ATTR_CMN_OBJTYPE | ATTR_CMN_OBJTAG),
            0, 0, ATTR_FILE_TOTALSIZE | ATTR_FILE_IOBLOCKSIZE, 0,
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
                                       &work_arena, push_file, push_directory,
                                       trace_on](
            char const *dir_path, DirEntryAttributes const *entry) {
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
                        push_back_cstr(trace_output, name);
                        if (entry->obj_type == VREG) {
                                push_back_cstr(trace_output, "\t[f]");
                        } else if (entry->obj_type == VDIR) {
                                push_back_cstr(trace_output, "\t[d]");
                        } else {
                                // not a file, not a directory
                                push_back_formatted(trace_output, "\t[%d]",
                                                    entry->obj_type);
                        }
                        push_back_cstr(trace_output, "\t");

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
                                push_back_cstr(trace_output, "NFS");
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_HFS) {
                                push_back_cstr(trace_output, "HFS");
                                supports_semicolon = true;
                        } else if (entry->obj_tag == VT_AFP) {
                                push_back_cstr(trace_output, "AFP");
                                mounted_share = true;
                        } else if (entry->obj_tag == VT_CIFS) {
                                push_back_cstr(trace_output, "CIFS");
                                mounted_share = true;
                        } else {
                                push_back_u32(trace_output, entry->obj_tag);
                        }

                        if (mounted_share) {
                                MemoryArena temp_arena = work_arena;
                                auto errorg = TextOutputGroup{};
                                allocate(errorg, &temp_arena, KILOBYTES(1));
                                push_back_cstr(errorg, "path '");
                                push_back_cstr(errorg, path);
                                push_back_cstr(errorg, "' is a mounted share.");
                                error(errorg);
                        }

                        if (entry->obj_type == VREG) {
                                push_back_cstr(trace_output, "\t");
                                push_back_u32(trace_output,
                                              entry->file_ioblocksize);
                                push_back_cstr(trace_output, "\t");
                                push_back_u64(trace_output,
                                              entry->file_totalsize);

                                if (!supports_semicolon &&
                                    global_can_ignore_file) {
                                        if (':' == *cstr_find(path, ':')) {
                                                MemoryArena temp_arena =
                                                    work_arena;
                                                auto errorg = TextOutputGroup{};
                                                allocate(errorg, &temp_arena,
                                                         KILOBYTES(1));
                                                push_back_cstr(errorg,
                                                               "path: '");
                                                push_back_cstr(errorg, path);
                                                push_back_cstr(
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
                                                push_back_cstr(trace_output,
                                                               "\t");
                                                push_back_u64(
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

                push_back_cstr(trace_output, "\nlisting directory: ");
                push_back_cstr(trace_output, dir_path);
                trace_optionally();

                // TODO(nicolas): push sep by "\t"
                push_back_cstr(trace_output, "name");
                push_back_cstr(trace_output, "\t");
                push_back_cstr(trace_output, "type");
                push_back_cstr(trace_output, "\t");
                push_back_cstr(trace_output, "tag");
                push_back_cstr(trace_output, "\t");
                push_back_cstr(trace_output, "ioblocksize");
                push_back_cstr(trace_output, "\t");
                push_back_cstr(trace_output, "size");
                push_back_cstr(trace_output, "\t");
                push_back_cstr(trace_output, "physical location");
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

        zw_assert(state->directories == nullptr, "no more directories");

        size_t entry_array_count = 0;
        FSEntry *entry_array = nullptr;
        {
                size_t file_count = 0;
                for (FSEntry *fs_entry = state->files; fs_entry;
                     fs_entry = fs_entry->next) {
                        file_count++;
                }

                push_back_cstr(trace_output, "found ");
                push_back_u64(trace_output, file_count);
                push_back_cstr(trace_output, " files");
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
                push_back_cstr(trace_output, "FILE");
                push_back_u64(trace_output, i);
                push_back_cstr(trace_output, "\t");
                push_back_u64(trace_output, entry_array[i].physical_offset);
                push_back_cstr(trace_output, "\t");
                push_back_cstr(trace_output, entry_array[i].path);
                trace_optionally();
        }

        // construct file list from array
        FileList *result = push_pointer_rvalue(result_arena, result);
        *result = {};
        result->count = entry_array_count;

        if (result->count) {
                size_t count = result->count;
                result->paths =
                    push_array_rvalue(result_arena, result->paths, count);
                result->filenames =
                    push_array_rvalue(result_arena, result->filenames, count);
                result->attributes =
                    push_array_rvalue(result_arena, result->attributes, count);
                for (size_t i = 0; i < count; i++) {
                        char const *input_path = entry_array[i].path;
                        size_t input_path_n = cstr_len(input_path);

                        ByteCountedRange path = {
                            (uint8_t *)push_bytes(result_arena,
                                                  1 + input_path_n),
                            input_path_n,
                        };
                        *algos::copy_n(input_path, input_path_n, path.first) =
                            0;

                        uint8_t *filename = algos::find_backward(
                            path.first, path.first + path.count, '/');
                        // TODO(nicolas): is this skipping a pattern?
                        filename = *filename == '/' ? filename + 1 : filename;

                        result->paths[i] = (char const *)path.first;
                        result->filenames[i] = (char const *)filename;
                        result->attributes[i].size = entry_array[i].size;
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

zw_internal void print_message_summary(MessageSummary const &message_summary,
                                       MemoryArena transient_arena)
{
        // FEATURE(nicolas): print raw content of From: To: CC
        // Sender: Message-Id: In-Reply-To: Subject:
        TextOutputGroup text_output_group = {};
        allocate(text_output_group, &transient_arena, KILOBYTES(16));

        // TODO(nicolas): having a delimited table printing abstraction
        // would help with these (and the app)

        auto print_field = [&text_output_group](const char *name,
                                                struct ByteCountedRange value) {
                if (value.first) {
                        push_back_cstr(text_output_group, "\t");
                        push_back_cstr(text_output_group, name);
                        push_back_cstr(text_output_group, "\t");
                        push_back_extent(text_output_group, value.first,
                                         value.count);
                        trace(text_output_group);
                }
        };
        auto print_bytes_list_field = [&text_output_group](
            const char *name, struct ByteCountedRange *values, size_t count) {
                if (count == 0) {
                        return;
                }

                push_back_cstr(text_output_group, "\t");
                push_back_cstr(text_output_group, name);
                push_back_cstr(text_output_group, "\t");
                algos::for_each_n(
                    values, count, [&](const ByteCountedRange &range) {
                            push_back_extent(text_output_group, range.first,
                                             range.count);
                    });
                trace(text_output_group);
        };
        // TODO(nicolas): mailboxes could be stored sorted,
        // since there is not a lot of semantic attached to them
        // however, what about groups?
        // The advantage of sorting mailboxes is that we could
        // compare groups of recipients for instance and name
        // them somehow
        auto print_mailbox_list_field = [&text_output_group](
            const char *name, RawMailbox const *mailboxes,
            size_t mailboxes_count) {
                if (mailboxes_count == 0) {
                        return;
                }
                push_back_cstr(text_output_group, "\t");
                push_back_cstr(text_output_group, name);
                push_back_cstr(text_output_group, "\t");

                auto push_back_mailbox = [](TextOutputGroup &text_output_group,
                                            RawMailbox const &mailbox) {
                        push_back_cstr(text_output_group, "<mailbox ");
                        if (mailbox.display_name_bytes.count > 0) {
                                push_back_cstr(text_output_group, "\"");
                                push_back_extent(
                                    text_output_group,
                                    mailbox.display_name_bytes.first,
                                    mailbox.display_name_bytes.count);
                                push_back_cstr(text_output_group, "\" ");
                        }
                        push_back_extent(text_output_group,
                                         mailbox.local_part_bytes.first,
                                         mailbox.local_part_bytes.count);
                        push_back_cstr(text_output_group, "@");
                        push_back_extent(text_output_group,
                                         mailbox.domain_bytes.first,
                                         mailbox.domain_bytes.count);
                        push_back_cstr(text_output_group, ">");
                };

                if (mailboxes_count > 0) {
                        push_back_mailbox(text_output_group, *mailboxes);
                        ++mailboxes;
                        --mailboxes_count;
                }

                algos::for_each_n(
                    mailboxes, mailboxes_count, [&](const RawMailbox &mailbox) {
                            push_back_cstr(text_output_group, ", ");
                            push_back_mailbox(text_output_group, mailbox);
                    });

                trace(text_output_group);
        };

        if (!message_summary.valid_rfc5322) {
                trace_print("\tRFC5322\tfalse");
        }

        print_field("MESSAGE-ID", message_summary.message_id_bytes);
        print_mailbox_list_field(
            "SENDER", &message_summary.sender_mailbox,
            message_summary.sender_mailbox.domain_bytes.count > 0 ? 1 : 0);
        print_mailbox_list_field("FROM", message_summary.from_mailboxes,
                                 message_summary.from_mailboxes_count);
        print_mailbox_list_field("TO", message_summary.to_mailboxes,
                                 message_summary.to_mailboxes_count);
        print_mailbox_list_field("CC", message_summary.cc_mailboxes,
                                 message_summary.cc_mailboxes_count);
        print_bytes_list_field("IN_REPLY_TO",
                               message_summary.in_reply_to_msg_ids,
                               message_summary.in_reply_to_msg_ids_count);
        print_field("SUBJECT", message_summary.subject_field_bytes);
        {
                push_back_cstr(text_output_group, "\t");
                push_back_cstr(text_output_group, "CONTENT_TRANSFER_ENCODING");
                push_back_cstr(text_output_group, "\t");
                push_back_u32(text_output_group,
                              message_summary.content_transfer_encoding);
                trace(text_output_group);
        }
        print_field("FIRST_LINE", message_summary.first_line_bytes);
}

struct ProcessedMessage {
        bool success;
        Sha1 sha1;
        size_t fileindex;
        char const *filepath;
        char const *filename;
        uint64_t filesize;
        ZoeMailStoreFile zoefile;
        ByteCountedRange zoe_rel_url;
        MessageSummary message_summary;
};

struct Zwei {
        AcceptMimeMessageFn *accept_mime_message;
        ParseZoeMailstoreFilenameFn *parse_zoe_mailstore_filename;
};

void inputstream_finish(IOBufferIterator *iobuffer)
{
        while (iobuffer->error == IOBufferIteratorError_NoError) {
                refill_iobuffer(iobuffer);
        }
}

zw_internal void process_message(Zwei const &zwei,
                                 char const *filename,
                                 char const *filepath,
                                 uint8_t const *full_message,
                                 uint8_t const *full_message_last,
                                 MemoryArena transient_arena,
                                 MemoryArena &result_arena,
                                 ProcessedMessage &destination)
{
        SPDR_SCOPE1(global_spdr, "app", "process_message",
                    SPDR_STR("filepath", filepath));
        IOBufferIterator file_content;
        stream_on_memory(&file_content, const_cast<uint8_t *>(full_message),
                         full_message_last - full_message);
        auto sha1_result = sha1(&file_content);
        inputstream_finish(&file_content);

        if (!sha1_result) {
                return;
        }

        destination.sha1 = just(sha1_result);

        destination.filename = filename;
        destination.filepath = filepath;
        destination.filesize = full_message_last - full_message;

        auto zoefile_errorcode =
            zwei.parse_zoe_mailstore_filename(&destination.zoefile, filename);
        if (0 != zoefile_errorcode && !cstr_endswith(filepath, ".eml")) {
                return;
        }

        // TODO(nicolas): @platform_specific here we have encoded the
        // path separator, which makes this code non portable
        if (0 == zoefile_errorcode) {
                // find predecessor, until you reach
                // boundary
                auto pred = [](char const *const first, char const *const pos) {
                        if (pos != first) {
                                return algos::predecessor(pos);
                        }
                        return pos;
                };
                // skip day, month, year
                auto first = cstr_find_last(filepath, '/');
                first = *first != '/'
                            ? first
                            : algos::find_backward(filepath,
                                                   pred(filepath, first), '/');
                first = *first != '/'
                            ? first
                            : algos::find_backward(filepath,
                                                   pred(filepath, first), '/');
                first = *first != '/'
                            ? first
                            : algos::find_backward(filepath,
                                                   pred(filepath, first), '/');
                first = *first != '/' ? first : algos::successor(first);
                auto last = cstr_find_last(first, '.');
                destination.zoe_rel_url = {(uint8_t *)first,
                                           size_t(last - first)};
        }

        SPDR_BEGIN(global_spdr, "app", "accept_mime_message");
        auto errorcode = zwei.accept_mime_message(
            full_message, full_message_last,
            zoefile_errorcode == 0 ? &destination.zoefile : nullptr,
            &transient_arena, &result_arena, &destination.message_summary);
        zw_assert(errorcode == 0, "unexpected errorcode");
        SPDR_END(global_spdr, "app", "accept_mime_message");

        destination.success = true;
}

zw_internal void
print_processed_message(ProcessedMessage const &processed_message,
                        MemoryArena transient_arena)
{
        SPDR_SCOPE2(global_spdr, "main", __FUNCTION__,
                    SPDR_STR("filepath", processed_message.filepath),
                    SPDR_INT("fileindex", int(processed_message.fileindex)));
        char const *filepath = processed_message.filepath;
        TextOutputGroup traceg = {};
        allocate(traceg, &transient_arena, KILOBYTES(1));
        {
                push_back_cstr(traceg, "FILE");
                push_back_formatted(traceg, "%lld",
                                    processed_message.fileindex);
                push_back_cstr(traceg, " ");
                push_back_cstr(traceg, filepath);
                push_back_cstr(traceg, ":");
                trace(traceg);
        }
        push_back_cstr(traceg, "SHA1");
        push_back_cstr(traceg, "\t");

        auto sha1_value = processed_message.sha1;
        char byteToHexChar[] = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
        };
        for (size_t byteIndex = 0; byteIndex < countof(sha1_value.digest);
             byteIndex++) {
                uint8_t const byte = sha1_value.digest[byteIndex];
                push_back_formatted(traceg, "%c%c", byteToHexChar[byte >> 4],
                                    byteToHexChar[byte & 0xF]);
        }
        trace(traceg);

        if (processed_message.success) {
                // FEATURE(nicolas): print filing timestamp
                push_back_cstr(traceg, "PATH");
                push_back_cstr(traceg, "\t");
                push_back_cstr(traceg, filepath);
                trace(traceg);

                push_back_cstr(traceg, "SIZE");
                push_back_cstr(traceg, "\t");
                push_back_u64(traceg, processed_message.filesize);
                push_back_cstr(traceg, "\t");
                trace(traceg);

                push_back_cstr(traceg, "ZOE_REL_URL");
                push_back_cstr(traceg, "\t");
                push_back_extent(traceg, processed_message.zoe_rel_url.first,
                                 processed_message.zoe_rel_url.count);
                trace(traceg);

                push_back_cstr(traceg, "UUID");
                push_back_cstr(traceg, "\t");
                for (size_t i = 0; i < countof(processed_message.zoefile.uuid);
                     i++) {
                        push_back_formatted(traceg, "%x",
                                            processed_message.zoefile.uuid[i]);
                }
                trace(traceg);

                push_back_cstr(traceg, "UNIX TIMESTAMP");
                push_back_cstr(traceg, "\t");
                push_back_formatted(
                    traceg, "%llu",
                    processed_message.zoefile.unix_epoch_millis);
                trace(traceg);

                print_message_summary(processed_message.message_summary,
                                      transient_arena);
        } else {
                trace_print("ignored file");
        }
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

        struct MemoryArena permanent_arena =
            memory_arena(permanent_storage, permanent_storage_memory_size);
        MemoryArena transient_arena =
            memory_arena(transient_storage, transient_storage_memory_size);

        if (spdr_init(&global_spdr, push_bytes(&permanent_arena, MEGABYTES(96)),
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

        auto refresh_zwei_app = [&zwei_app_library, &zwei, debug_mode_on]() {
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

                        InitAppFn *init_app;
                        GRAB_FN_SYM(init_app, "init_app", lib->dlhandle);
                        GRAB_FN_SYM(zwei.accept_mime_message,
                                    "accept_mime_message", lib->dlhandle);
                        GRAB_FN_SYM(zwei.parse_zoe_mailstore_filename,
                                    "parse_zoe_mailstore_filename",
                                    lib->dlhandle);

#undef GRAB_FN_SYM

                        Platform platform;
                        platform.spdr = global_spdr;

                        init_app(platform,
                                 debug_mode_on ? ZWEI_DEBUG_MODE_FLAG : 0);

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
                // NOTE(nicolas) we could process files in parallel
                // .. but how much parallelism can a HDD/SDD/Raid
                // device support?

                struct FileList *all_files;
                {
                        MemoryArena dc_arena = push_sub_arena(
                            transient_arena, transient_storage_memory_size / 2);
                        all_files = directory_query_all_files(
                            root_dir_path, directory_listing_on,
                            transient_arena, &dc_arena);
                        pop_unused(transient_arena, dc_arena);
                        {
                                MemoryArena temp_arena = transient_arena;
                                TextOutputGroup traceg = {};
                                allocate(traceg, &temp_arena, KILOBYTES(1));
                                push_back_cstr(traceg,
                                               "Bytes used after query: ");
                                push_back_formatted(traceg, "%lld",
                                                    transient_arena.used);
                                trace(traceg);
                        }
                }

                if (!all_files) {
                        error_print("could not query all files");
                        return 1;
                }

                MemoryArena result_arena =
                    push_sub_arena(permanent_arena, MEGABYTES(512));
                // TODO(nicolas) always measure bytes/sec or sec/MB and
                // print out total bytes
                SPDR_BEGIN(global_spdr, "main", "processing_files");

                // Is a file_loader structure necessary? Since I/O
                // doesn't have many resources, this could be made
                // purely functional and use a global variable. At
                // least from an API point of view.

                size_t file_size_limit = MEGABYTES(128);
                auto file_loader_arena = push_sub_arena(
                    transient_arena, get_file_loader_allocation_size(
                                         all_files->count, file_size_limit));
                auto &files_loader =
                    create_file_loader(all_files->count, file_loader_arena.base,
                                       file_loader_arena.size);
                for (size_t file_index = 0; file_index < all_files->count;
                     ++file_index) {
                        size_t size = all_files->attributes[file_index].size;
                        bool ignore = false;
                        if (size > file_size_limit) {
                                // TODO(nicolas): should error be
                                // dealt with here or inside the
                                // loader? Inside the loader would
                                // centralize error handling.
                                error_print(
                                    "Ignoring Exceptionally Large File");
                                if (global_can_ignore_file) {
                                        ignore = true;
                                } else {
                                        zw_assert(false, "large file");
                                }
                        }

                        if (!ignore) {
                                push_file(files_loader,
                                          all_files->paths[file_index], size,
                                          (uint32_t)file_index);
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
                            push_sub_arena(transient_arena, MEGABYTES(2));
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
                                push_sub_arena(result_arena, KILOBYTES(8));
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
                                        x.message_task.result, transient_arena);
                            });
                };

                while (count_pending_files(files_loader) > 0) {
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
                        SPDR_COUNTER1(global_spdr, "main", "process_task",
                                      SPDR_INT("value", int(tasks_count)));
                }
                process_and_print(tasks, tasks + tasks_count);
                tasks_count = 0;

                {
                        auto error = load_error_files(files_loader);
                        zw_assert(algos::empty(begin(error), end(error)),
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
                MemoryArena temp_arena = permanent_arena;
                auto tog = TextOutputGroup{};
                allocate(tog, &temp_arena, 512);
                push_back_cstr(tog, "reporting traces to file: ");
                push_back_cstr(tog, trace_file);
                push_back_cstr(tog, "\n");
                trace(tog);
        }
        int fd = open(trace_file, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        spdr_report(global_spdr, SPDR_CHROME_REPORT,
                    [](char const *string, void *user_data) {
                            char buffer[4096];
                            MemoryArena stack =
                                memory_arena(buffer, sizeof buffer);
                            int *fd_ptr = static_cast<int *>(user_data);
                            auto tog = TextOutputGroup{};
                            allocate(tog, &stack, 64);
                            push_back_cstr(tog, string);
                            text_output_group_print(*fd_ptr, tog);
                    },
                    &fd);
        close(fd);
        spdr_deinit(&global_spdr);

        return 0;
}

// TODO(nicolas): NEXT serialize data on disk so we can compare the
// results of both methods and see that nothing wrong is happening.
#if 0 * ZWEI_SLOW
#include "zwei_files_osx_synchronous.cpp"
#else
#include "zwei_files_osx_async.cpp"
#endif

#include "zwei_iobuffer.cpp"
#include "zwei_osx_logging.cpp"
#include "zwei_textoutputgroup.cpp"

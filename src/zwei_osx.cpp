#include <cstdint>

void error_print(char const *);

#if ZWEI_SLOW
#define assert(condition, message)                                             \
        do {                                                                   \
                if (!(condition)) {                                            \
                        error_print(message);                                  \
                        asm("int3");                                           \
                }                                                              \
        } while (0)
#else
#define assert(condition, message)
#endif

#define NCOUNT(array) (sizeof array) / (sizeof array[0])

#define TOKEN_PASTE_inner(x, y) x##y
#define TOKEN_PASTE(x, y) TOKEN_PASTE_inner(x, y)

#define GIGABYTES(x) (MEGABYTES(x) * 1024LL)
#define MEGABYTES(x) (KILOBYTES(x) * 1024LL)
#define KILOBYTES(x) (x * 1024LL)

/// use it to mark symbols which should not escape the compilation unit
#define zw_internal static

/// local variables that persist across calls
#define zw_local_persist static

// <BASIC integer types

typedef uint32_t bool32;

// ..BASIC integer types>

// <Error handling

template <typename ValueType> struct MayFail {
        ValueType value;
        uint32_t errorcode = 0;
};

template <typename ValueType> bool failed(MayFail<ValueType> const result)
{
        return result.errorcode != 0;
}

template <typename ValueType> ValueType just(MayFail<ValueType> const result)
{
        assert(result.errorcode == 0, "valid result");
        return result.value;
}

// ..Error handling>

// <DEFER implementation

template <class Lambda> class AtScopeExit
{
        Lambda &action;

      public:
        AtScopeExit(Lambda &action) : action(action) {}
        ~AtScopeExit() { action(); }
};

#define DEFER_inner2(lambda_name, instance_name, ...)                          \
        auto lambda_name = [&]() { __VA_ARGS__; };                             \
        AtScopeExit<decltype(lambda_name)> instance_name(lambda_name);

#define DEFER_inner(counter, ...)                                              \
        DEFER_inner2(TOKEN_PASTE(Defer_lambda, counter),                       \
                     TOKEN_PASTE(Defer_instance, counter), __VA_ARGS__)

#define DEFER(...) DEFER_inner(__COUNTER__, __VA_ARGS__)

// ..DEFER implementation>

// <CSTRINGS

inline bool cstr_equals(char const *s1, char const *s2)
{
        assert(s1, "s1 should not be null");
        assert(s2, "s2 should not be null");
        while (s1[0] != 0 && s1[0] == s2[0]) {
                s1++;
                s2++;
        }

        return s1[0] == 0 && s2[0] == 0;
}

inline uint64_t cstr_len(char const *s)
{
        uint64_t size = 0;

        while (s[size]) {
                size++;
        };

        return size;
}

// ..CSTRINGS>

// <BUFFER-CENTRIC IO

#include <cstddef>

enum BufferRangeErrorCode {
        /// no error has occured
        BR_NoError,
        /// the consumer attempted to read past the end
        BR_ReadPastEnd,
        /// unrecoverable IO error
        BR_IOError,
};

void stream_on_memory(struct BufferRange *range, uint8_t *mem, size_t size);

struct BufferRange {
        /**
         * start of buffer.
         */
        uint8_t *start;

        /**
         * one byte past the end of buffer.
         *
         * so that:
         * - size = end - start
         * - end >= start
         */
        uint8_t *end;

        /**
         * interesting point in the buffer
         *
         * start <= cursor <= end
         */
        uint8_t *cursor;

        enum BufferRangeErrorCode error;

        /**
         * Refill function, called when more data is needed by the writer/reader
         *
         * - pre-condition: cursor == end
         * - post-condition: start == cursor < end
         */
        enum BufferRangeErrorCode (*next)(struct BufferRange *);
};

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

// <MEMORY ARENAS

/**
   only meant to work in one thread and one thread only.
 */

struct MemoryArena {
        uint8_t *base;
        size_t used;
        size_t size;
};

struct MemoryArena memory_arena(void *base, size_t size)
{
        return {(uint8_t *)base, 0, size};
}

inline void *push_bytes(struct MemoryArena *arena, size_t bytes)
{
        assert(arena->used + bytes <= arena->size, "overallocating");
        uint8_t *result = arena->base + arena->used;

        arena->used += bytes;

        return result;
}

#define push_struct(arena_pointer, type)                                       \
        (type *) push_bytes(arena_pointer, sizeof(type))

#define push_pointer_rvalue(arena_pointer, lvalue)                             \
        static_cast<decltype(lvalue)>(push_bytes(arena_pointer, sizeof *lvalue))

#define push_array_rvalue(arena_pointer, lvalue, count)                        \
        static_cast<decltype(lvalue)>(                                         \
            push_bytes(arena_pointer, count * sizeof *lvalue))

// ..MEMORY ARENAS>

#include <unistd.h>
#include <sys/attr.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

inline void sync_print(int filedesc, char const *message)
{
        auto message_size = cstr_len(message);
        struct iovec iovecs[] = {
            {(void *)message, message_size}, {(void *)"\n", 1},
        };
        writev(filedesc, iovecs, NCOUNT(iovecs));
}

// TODO(nicolas) print functions that work on streams rather than strings

inline void error_print(char const *message) { sync_print(2, message); }

inline void trace_print(char const *message) { sync_print(1, message); }

#include <sys/vnode.h> // for enum vtype
#include <mach/mach.h> // for vm_allocate

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

/*

time ./builds/zwei --root-dir /Volumes/Documents/Projects\ \(Nicolas\)/Music/

real	1m14.414s
user	0m0.179s
sys	0m2.439s

w/ CIFS
total used after scan: 1122699
done

real	0m37.408s
user	0m0.148s
sys	0m1.168s

Also when checking iopattern / iosnoop I can see that the access pattern for
directory listing is random.. blocks all over the place.

TODO(nicolas) how to minimize seeking of a NAS/mounted disk?
*/

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

struct FileList {
        size_t count;
        char const **paths;
        struct BufferRange *buffers;
};

/// @return null or a valid file list allocated in the given arena
zw_internal struct FileList *
directory_query_all_files(struct MemoryArena *arena, char const *root_dir_path)
{
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

        // TODO(nil) is this a thing? stdout is actually a stream so
        // you should not have to build a line unless you're
        // converting from some other datatype (uint's etc...)  or
        // doing some type conversion/formatting (dec to hexa etc...)
        class TextLine
        {
              public:
                struct BufferRange linebuffer;
                char *memory;
                void (*print)(char const *);

                TextLine(void (*print)(char const *),
                         char *memory,
                         size_t memory_size)
                    : memory(memory), print(print)
                {
                        stream_on_memory(&linebuffer, (uint8_t *)memory,
                                         memory_size);
                }

                ~TextLine()
                {
                        if (string_terminate(&linebuffer)) {
                                print(memory);
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
        (TextLine(error_print, linebuffer_memory, sizeof linebuffer_memory))
#define TRACE                                                                  \
        (TextLine(trace_print, linebuffer_memory, sizeof linebuffer_memory))

        struct FSEntry *dir_entry;
        while ((dir_entry = pop_directory())) {
                char const *dir_path;
                {
                        dir_path = dir_entry->path;
                        discard_entry(dir_entry);
                        dir_entry = nullptr;
                }

                int dir_fd;
                off_t dir_devoffset = 0;
                {
                        dir_fd = open(dir_path, O_RDONLY, 0);
                        if (dir_fd < 0) {
                                ERROR
                                    << "could not find directory: " << dir_path;
                                return nullptr;
                        }
                }
                DEFER({ close(dir_fd); });

                TRACE << "\nlisting directory: " << dir_path
                      << " block: " << dir_devoffset;

                struct attrlist attributes = {
                    ATTR_BIT_MAP_COUNT,
                    0,
                    (ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_ERROR | ATTR_CMN_NAME |
                     ATTR_CMN_OBJTYPE | ATTR_CMN_OBJTAG),
                    0,
                    0,
                    ATTR_FILE_TOTALSIZE | ATTR_FILE_IOBLOCKSIZE,
                    0,
                };
                uint64_t options = FSOPT_NOFOLLOW | FSOPT_REPORT_FULLSIZE |
                                   FSOPT_PACK_INVAL_ATTRS;

                struct ResultAttributes {
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
                            dir_fd, &attributes, &result_batch,
                            sizeof result_batch, options)) > 0) {
                        auto batch_entry_count = result;

                        uint8_t *data = (uint8_t *)result_batch;
                        for (int entry_index = 0;
                             entry_index < batch_entry_count; entry_index++) {
                                struct ResultAttributes *entry =
                                    (struct ResultAttributes *)data;
                                char const *name =
                                    (char *)((uint8_t *)&entry->nameinfo) +
                                    entry->nameinfo.attr_dataoffset;

                                auto path_size =
                                    cstr_len(dir_path) + 1 + cstr_len(name) + 1;
                                uint8_t *path =
                                    push_array_rvalue(arena, path, path_size);
                                {
                                        struct BufferRange path_buffer;
                                        stream_on_memory(&path_buffer, path,
                                                         path_size);
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
                                                // not a file, not a
                                                // directory
                                                string_cat_formatted(
                                                    &trace.linebuffer, "\t[%d]",
                                                    entry->obj_type);
                                        }

                                        trace << "\t";

                                        if (entry->obj_tag == VT_HFS) {
                                                trace << "HFS";
                                        } else if (entry->obj_tag == VT_AFP) {
                                                trace << "AFP";
                                        } else if (entry->obj_tag == VT_CIFS) {
                                                trace << "CIFS";
                                        } else {
                                                trace << entry->obj_tag;
                                        }

                                        if (entry->obj_type == VREG) {
                                                trace
                                                    << "\t"
                                                    << entry->file_ioblocksize;

                                                trace << "\t"
                                                      << entry->file_totalsize;

                                                int const fd = open(
                                                    path_cstr, O_RDONLY, 0);
                                                // TODO(nil) check that we have
                                                // permissions to open that file
                                                // at all
                                                assert(fd >= 0, "open file");
                                                DEFER(close(fd));

                                                if (entry->file_totalsize > 0) {

                                                        struct log2phys
                                                            filephys;
                                                        int fcntl_result =
                                                            fcntl(fd,
                                                                  F_LOG2PHYS,
                                                                  &filephys);
                                                        assert(fcntl_result >=
                                                                   0,
                                                               "could not get "
                                                               "phys");
                                                        if (fcntl_result < 0) {
                                                                error_print(
                                                                    "could not "
                                                                    "get phys");
                                                        } else {
                                                                physical_offset =
                                                                    filephys
                                                                        .l2p_devoffset;
                                                                trace
                                                                    << "\t"
                                                                    << filephys
                                                                           .l2p_devoffset;
                                                        }
                                                }
                                        }
                                }

                                if (entry->obj_type == VREG) {
                                        struct FSEntry *file_entry =
                                            push_file();
                                        file_entry->path = path_cstr;
                                        file_entry->physical_offset =
                                            physical_offset;
                                } else if (entry->obj_type == VDIR) {
                                        struct FSEntry *dir_entry =
                                            push_directory();
                                        dir_entry->path = path_cstr;
                                }

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
        {
                struct BufferRange linebuffer;
                stream_on_memory(&linebuffer, (uint8_t *)linebuffer_memory,
                                 sizeof linebuffer_memory);
                string_cat(&linebuffer, "total used after scan: ");
                string_cat_formatted(&linebuffer, "%lld", arena->used);
                if (string_terminate(&linebuffer)) {
                        trace_print(linebuffer_memory);
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

        // NOTE(nil) we sort our files by physical offset to reduce
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

        struct StreamedFile {
                int filedesc;
                char const *path;
                bool32 is_opened;

                // TODO(nil) what about io advice received before?
                uint8_t buffer[4 * 4096];
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

        if (result->count) {
                size_t count = result->count;
                result->paths = push_array_rvalue(arena, result->paths, count);
                result->buffers =
                    push_array_rvalue(arena, result->buffers, count);
                for (size_t i = 0; i < count; i++) {
                        result->paths[i] = entry_array[i].path;

                        struct StreamedFile *file =
                            push_pointer_rvalue(arena, file);
                        file->filedesc = -1;
                        file->path = result->paths[i];

                        stream_on_file(&result->buffers[i], file);
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

// this is the first version
int main(int argc, char **argv)
{
        assert(argc > 0, "unexpected argc");
        DEFER(trace_print("done"));

        char const *root_dir_path = nullptr;

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
                } else {
                        error_print("unexpected argument");
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

                struct FileList *all_files =
                    directory_query_all_files(&dc_arena, root_dir_path);
                if (all_files) {
                        // TODO(nil) always measure bytes/sec or sec/MB
                        char linebuffer_memory[256];
                        for (size_t i = 0; i < all_files->count; i++) {
                                struct BufferRange line;
                                stream_on_memory(&line,
                                                 (uint8_t *)linebuffer_memory,
                                                 sizeof linebuffer_memory);
                                string_cat(&line, "sha1 of ");
                                string_cat(&line, all_files->paths[i]);
                                string_cat(&line, " is ");

                                // TODO(nil) how about allocating the
                                // stream only here, with user's
                                // recommendations and in its own
                                // memory?
                                struct BufferRange *content =
                                    &all_files->buffers[i];

                                auto sha1_result = sha1(content);
                                if (failed(sha1_result)) {
                                        string_cat(
                                            &line,
                                            "<failed to compute result>");
                                } else {
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
                                             byteIndex <
                                                 NCOUNT(sha1_value.digest);
                                             byteIndex++) {
                                                uint8_t const byte =
                                                    sha1_value
                                                        .digest[byteIndex];
                                                string_cat_formatted(
                                                    &line, "%c%c",
                                                    byteToHexChar[byte >> 4],
                                                    byteToHexChar[byte & 0xF]);
                                        }
                                        if (string_terminate(&line)) {
                                                trace_print(linebuffer_memory);
                                        }
                                }
                        }
                }
        }

        return 0;
}

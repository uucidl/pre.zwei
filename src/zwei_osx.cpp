#include <cstdint>

#define assert(condition, message)                                             \
        do {                                                                   \
                if (!(condition)) {                                            \
                        asm("int3");                                           \
                }                                                              \
        } while (0)

#define NCOUNT(array) (sizeof array) / (sizeof array[0])

#define TOKEN_PASTE_inner(x, y) x##y
#define TOKEN_PASTE(x, y) TOKEN_PASTE_inner(x, y)

/// use it to mark symbols which should not escape the compilation unit
#define internal static

/// local variables that persist across calls
#define local_persist static

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

inline uint64_t cstr_size(char const *s)
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
};

void stream_on_memory(struct BufferRange *range, uint8_t const *mem,
                      size_t size);

struct BufferRange
{
        /**
         * start of buffer.
         */
        uint8_t const *start;

        /**
         * one byte past the end of buffer.
         *
         * so that:
         * - size = end - start
         * - end >= start
         */
        uint8_t const *end;

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

internal enum BufferRangeErrorCode next_zeros(struct BufferRange *range)
{
        local_persist uint8_t const zeros[256] = {0};

        range->start = zeros;
        range->cursor = (uint8_t *)zeros;
        range->end = zeros + sizeof(zeros);

        return range->error;
}

internal enum BufferRangeErrorCode fail(struct BufferRange *range,
                                        enum BufferRangeErrorCode error)
{
        range->error = error;
        range->next = next_zeros;

        return range->next(range);
}

internal enum BufferRangeErrorCode
next_on_memory_buffer(struct BufferRange *range)
{
        return fail(range, BR_ReadPastEnd);
}

void stream_on_memory(struct BufferRange *range, uint8_t *mem,
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

#include <unistd.h>
#include <sys/attr.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

inline void sync_print(int filedesc, char const *message)
{
        auto message_size = cstr_size(message);
        struct iovec iovecs[] = {
            {(void *)message, message_size}, {(void *)"\n", 1},
        };
        writev(filedesc, iovecs, NCOUNT(iovecs));
}

inline void error_print(char const *message) { sync_print(2, message); }

inline void trace_print(char const *message) { sync_print(1, message); }

#include <sys/vnode.h> // for enum vtype

/**
   NOTE(nicolas)

   We want to read from a root directory a series of small files in
   the fastest possible way.

   These files may either be stored locally or on a remote drive.

   We extract features from these files in memory.

   TODO(nicolas) find out if it's possible to read files locally and
   remotely while minimizing seeking. look for F_LOG2PHYS
*/

// this is the first version
int main(int argc, char **argv)
{
        assert(argc > 0, "unexpected argc");
        DEFER(trace_print("done"));

        char const *root_dir = nullptr;

        auto current_arg = 1;
        while (current_arg < argc) {
                if (cstr_equals(argv[current_arg], "--root-dir")) {
                        current_arg++;
                        if (current_arg >= argc) {
                                error_print(
                                    "expected argument after --root-dir");
                        }
                        root_dir = argv[current_arg];
                        current_arg++;
                } else {
                        error_print("unexpected argument");
                        return 1;
                        current_arg++;
                }
        }

        // FEATURE(nicolas): show content of any directory
        {
                int root_dir_fd = open(root_dir, O_RDONLY, 0);
                if (root_dir_fd < 0) {
                        error_print("could not find root directory");
                        return 1;
                }
                DEFER({ close(root_dir_fd); });

                struct attrlist attributes = {
                    ATTR_BIT_MAP_COUNT, 0,
                    (ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_ERROR | ATTR_CMN_NAME |
                     ATTR_CMN_OBJTYPE | ATTR_CMN_OBJTAG),
                    0, 0, ATTR_FILE_IOBLOCKSIZE, 0,
                };
                uint64_t options = FSOPT_NOFOLLOW | FSOPT_REPORT_FULLSIZE |
                                   FSOPT_PACK_INVAL_ATTRS;

                struct ResultAttributes
                {
                        uint32_t size;
                        attribute_set_t attributes;
                        uint32_t error;
                        attrreference nameinfo;
                        fsobj_type_t obj_type;
                        fsobj_tag_t obj_tag;
                        union
                        {
                                uint32_t file_ioblocksize;
                        };
                } __attribute__((aligned(4), packed));

                uint32_t result_entries[16 * 256];
                int result;

                uint32_t entrycount = 0;
                char linebuffer_memory[256];
                {
                        struct BufferRange linebuffer;
                        stream_on_memory(&linebuffer,
                                         (uint8_t *)linebuffer_memory,
                                         sizeof linebuffer_memory);
                        string_cat(&linebuffer, "name");
                        string_cat(&linebuffer, "\t");
                        string_cat(&linebuffer, "type");
                        string_cat(&linebuffer, "\t");
                        string_cat(&linebuffer, "tag");
                        string_cat(&linebuffer, "\t");
                        string_cat(&linebuffer, "ioblocksize");
                        if (string_terminate(&linebuffer)) {
                                trace_print(linebuffer_memory);
                        }
                }

                while ((result = getattrlistbulk(
                            root_dir_fd, &attributes, &result_entries,
                            sizeof result_entries, options)) > 0) {
                        auto batch_entry_count = result;

                        uint8_t *data = (uint8_t *)result_entries;
                        for (int entry_index = 0;
                             entry_index < batch_entry_count; entry_index++) {
                                struct ResultAttributes *entry =
                                    (struct ResultAttributes *)data;
                                char const *name =
                                    (char *)((uint8_t *)&entry->nameinfo) +
                                    entry->nameinfo.attr_dataoffset;

                                {
                                        struct BufferRange linebuffer;
                                        stream_on_memory(
                                            &linebuffer,
                                            (uint8_t *)linebuffer_memory,
                                            sizeof linebuffer_memory);
                                        string_cat(&linebuffer, name);
                                        if (entry->obj_type == VREG) {
                                                string_cat(&linebuffer,
                                                           "\t[f]");
                                        } else if (entry->obj_type == VDIR) {
                                                string_cat(&linebuffer,
                                                           "\t[d]");
                                        } else {
                                                // not a file, not a directory
                                                string_cat_formatted(
                                                    &linebuffer, "\t[%d]",
                                                    entry->obj_type);
                                        }

                                        string_cat(&linebuffer, "\t");

                                        if (entry->obj_tag == VT_HFS) {
                                                string_cat(&linebuffer, "HFS");
                                        } else if (entry->obj_tag == VT_AFP) {
                                                string_cat(&linebuffer, "AFP");
                                        } else {
                                                string_cat_uint32(
                                                    &linebuffer,
                                                    entry->obj_tag);
                                        }

                                        if (entry->obj_type == VREG) {
                                                string_cat(&linebuffer, "\t");
                                                string_cat_uint32(
                                                    &linebuffer,
                                                    entry->file_ioblocksize);
                                        }

                                        if (string_terminate(&linebuffer)) {
                                                trace_print(linebuffer_memory);
                                        }
                                }

                                data += entry->size;
                        }
                        entrycount += batch_entry_count;
                }
                if (result < 0) {
                        if (EBADF == errno || ENOTDIR == errno) {
                                error_print("not a readable directory");
                                return 1;
                        }
                        perror("getattrlistbulk");
                        return 1;
                }
        }

        return 0;
}

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

inline bool cstr_equals(char const *s1, char const *s2)
{
        assert(s1, "s1 should not be null");
        assert(s2, "s2 should not be null");
        while (s1[0] != 0 && s1[0] == s2[0]) {
                s1++;
                s2++;
        }

        return s1[0] == 0;
}

inline uint64_t cstr_size(char const *s)
{
        uint64_t size = 0;

        while (s[size]) {
                size++;
        };

        return size;
}

#include <unistd.h>
#include <sys/attr.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

inline void sync_print(int filedesc, char const *message)
{
        assert(flock(filedesc, LOCK_EX) == 0, "locking");
        auto message_size = cstr_size(message);
        struct iovec iovecs[] = {
            {(void *)message, message_size}, {(void *)"\n", 1},
        };
        writev(filedesc, iovecs, NCOUNT(iovecs));
        assert(flock(filedesc, LOCK_UN) == 0, "unlocking");
}

inline void error_print(char const *message) { sync_print(2, message); }

inline void trace_print(char const *message) { sync_print(1, message); }

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

        // SHOW CONTENT OF THE DIRECTORY
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
                     ATTR_CMN_OBJTYPE),
                    0, 0, 0, 0,
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
                } __attribute__((aligned(4), packed));

                uint32_t result_entries[16 * 256];
                int result;

                uint32_t entrycount = 0;
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
                                trace_print(name);
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

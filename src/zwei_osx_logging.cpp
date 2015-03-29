#include "zwei_logging.hpp"

#include <sys/uio.h>

inline void sync_print(int filedesc, char const *message, size_t message_size)
{
        struct iovec iovecs[] = {
            {(void *)message, message_size}, {(void *)"\n", 1},
        };
        writev(filedesc, iovecs, NCOUNT(iovecs));
}

// TODO(nicolas) print functions that work on streams rather than strings

void error_print(char const *message)
{
        auto message_size = cstr_len(message);
        sync_print(2, message, message_size);
}

void trace_print(char const *message)
{
        auto message_size = cstr_len(message);
        sync_print(1, message, message_size);
}

void trace_print(char const *message, size_t message_len)
{
        sync_print(1, message, message_len);
}

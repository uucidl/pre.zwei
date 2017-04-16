#include "zwei_logging.hpp"

// TODO(nicolas): trace/error should feature a timestamp for
// auditing.

#include "zwei_textoutputgroup.hpp"

#include "algos.hpp"

#include <sys/uio.h>

zw_internal void sync_print(int filedesc,
                            char const *prefix,
                            size_t prefix_size,
                            char const *message,
                            size_t message_size)
{
        struct iovec iovecs[] = {
            {(void *)prefix, prefix_size},
            {(void *)message, message_size},
            {(void *)"\n", 1},
        };
        writev(filedesc, iovecs, countof(iovecs));
}

#define IOVECS_CAPACITY (128 >= IOV_MAX ? IOV_MAX : 128)

zw_internal void
iovecs_write(iovec **iovecs_next_ptr, iovec *iovecs, int filedesc)
{
        if (*iovecs_next_ptr != iovecs) {
                writev(filedesc, iovecs, int(*iovecs_next_ptr - iovecs));
                *iovecs_next_ptr = iovecs;
        }
}

zw_internal iovec &iovecs_push(iovec **iovecs_next_ptr,
                               iovec *iovecs,
                               iovec *iovecs_last,
                               int filedesc)
{
        if (*iovecs_next_ptr == iovecs_last) {
                iovecs_write(iovecs_next_ptr, iovecs, filedesc);
        }

        iovec *entry = *iovecs_next_ptr;
        ++(*iovecs_next_ptr);
        return *entry;
};

zw_internal void text_output_group_print(int filedesc,
                                         iovec *iovecs,
                                         iovec *iovecs_last,
                                         TextOutputGroup const &group)
{
        auto iovecs_next = iovecs;

        algos::for_each(
            group.first, group.last, [&](TextOutputGroupEntry const &x) {
                    auto &iovec = iovecs_push(&iovecs_next, iovecs, iovecs_last,
                                              filedesc);
                    iovec.iov_base = (void *)(x.first);
                    iovec.iov_len = x.count;
            });

        if (group.truncated) {
                auto &iovec =
                    iovecs_push(&iovecs_next, iovecs, iovecs_last, filedesc);
                iovec.iov_base = (void *)"…";
                iovec.iov_len = sizeof("…") - 1;
        }

        auto &iovec = iovecs_push(&iovecs_next, iovecs, iovecs_last, filedesc);
        iovec.iov_base = (void *)"\n";
        iovec.iov_len = 1;
        iovecs_write(&iovecs_next, iovecs, filedesc);
}

void text_output_group_print(int filedesc, TextOutputGroup const &group)
{
        struct iovec iovecs[IOVECS_CAPACITY];
        auto const iovecs_last = iovecs + IOVECS_CAPACITY;
        text_output_group_print(filedesc, iovecs, iovecs_last, group);
}

void error_print(char const *message)
{
        auto message_size = cstr_len(message);
        char const *prefix = "ERROR: ";
        sync_print(2, prefix, cstr_len(prefix), message, message_size);
}

void trace_print(char const *message)
{
        auto message_size = cstr_len(message);
        char const *prefix = "TRACE: ";
        sync_print(1, prefix, cstr_len(prefix), message, message_size);
}

void trace(TextOutputGroup *group)
{
        struct iovec iovecs[IOVECS_CAPACITY];
        auto const iovecs_last = iovecs + IOVECS_CAPACITY;

        char const *prefix = "TRACE: ";
        iovecs[0].iov_base = (void *)prefix;
        iovecs[0].iov_len = cstr_len(prefix);

        text_output_group_print(1, iovecs + 1, iovecs_last, *group);
        clear(group);
}

void error(TextOutputGroup *group)
{
        struct iovec iovecs[IOVECS_CAPACITY];
        auto const iovecs_last = iovecs + IOVECS_CAPACITY;

        char const *prefix = "ERROR: ";
        iovecs[0].iov_base = (void *)prefix;
        iovecs[0].iov_len = cstr_len(prefix);

        text_output_group_print(2, iovecs + 1, iovecs_last, *group);
        clear(group);
}

#undef IOVECS_CAPACITY

#include "zwei_logging.hpp"
#include "zwei_textoutputgroup.hpp"

#include "algos.hpp"

#include <sys/uio.h>

inline void sync_print(int filedesc, char const *message, size_t message_size)
{
        struct iovec iovecs[] = {
            {(void *)message, message_size}, {(void *)"\n", 1},
        };
        writev(filedesc, iovecs, countof(iovecs));
}

#define IOVECS_CAPACITY 64

void text_output_group_print(int filedesc, TextOutputGroup const &group)
{
        struct iovec iovecs[IOVECS_CAPACITY];
        auto iovecs_next = iovecs;
        auto const iovecs_last = iovecs + IOVECS_CAPACITY;

        auto const write_iovecs = [&iovecs_next, &iovecs, filedesc]() {
                if (iovecs_next != iovecs) {
                        writev(filedesc, iovecs, int(iovecs_next - iovecs));
                        iovecs_next = iovecs;
                }
        };

        auto next_iovec = [&iovecs_next, iovecs_last,
                           write_iovecs]() -> iovec & {
                if (iovecs_next == iovecs_last) {
                        write_iovecs();
                }

                iovec *entry = iovecs_next;
                ++iovecs_next;
                return *entry;
        };

        auto first_entry = group.first;
        auto const last_entry = group.last;
        algos::for_each(first_entry, last_entry,
                        [next_iovec](TextOutputGroupEntry const &x) {
                                auto &iovec = next_iovec();
                                iovec.iov_base = (void *)(x.first);
                                iovec.iov_len = x.count;
                        });

        if (group.truncated) {
                auto &iovec = next_iovec();
                iovec.iov_base = (void *)"…";
                iovec.iov_len = sizeof("…") - 1;
        }

        auto &iovec = next_iovec();
        iovec.iov_base = (void *)"\n";
        iovec.iov_len = 1;
        write_iovecs();
}

#undef IOVECS_CAPACITY

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

void trace_print_n(char const *message, size_t message_len)
{
        sync_print(1, message, message_len);
}

void trace(TextOutputGroup *group)
{
        text_output_group_print(1, *group);
        clear(group);
}

void error(TextOutputGroup *group)
{
        text_output_group_print(2, *group);
        clear(group);
}

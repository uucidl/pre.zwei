#include "zwei_logging.hpp"
#include "zwei_textoutputgroup.hpp"

#include "algos.hpp"

#include "../modules/uu.spdr/include/spdr/spdr.hh"

#include <sys/uio.h>

inline void sync_print(int filedesc, char const *message, size_t message_size)
{
        struct iovec iovecs[] = {
            {(void *)message, message_size}, {(void *)"\n", 1},
        };
        writev(filedesc, iovecs, countof(iovecs));
}

extern zw_global SPDR_Context *global_spdr;

void text_output_group_print(int filedesc, TextOutputGroup const &group)
{
        SPDR_SCOPE1(global_spdr, "logging", __FUNCTION__,
                    SPDR_INT("filedesc", filedesc));
        SPDR_EVENT3(global_spdr, "variables", "TextOutputGroup",
                    SPDR_INT("entries-count", int(group.last - group.first)),
                    SPDR_INT("entries-bytes", int(sizeof *group.first *
                                                  (group.last - group.first))),
                    SPDR_INT("bytes", int(group.bytes_arena.used)));

#define IOVECS_CAPACITY 64
        struct iovec iovecs[IOVECS_CAPACITY];
        uint8_t iovecs_n = 0;

        // TODO(nicolas): what is this pattern? we have an operation
        // that we wish to do (writev) which takes a number of iovec
        // we want to call this thing every now and then so we create
        // a buffer of iovec that we write to
        //
        // If I had a buffered output iterator that would
        // automatically flush the buffer to get to the next buffer as
        // things get copied to it, then we could use a copy
        // algorithm.
        auto next_iovec = [filedesc, &iovecs, &iovecs_n]() -> iovec & {
                if (iovecs_n >= IOVECS_CAPACITY) {
                        writev(filedesc, iovecs, iovecs_n);
                        iovecs_n = 0;
                }
                iovec *entry = iovecs + iovecs_n;
                ++iovecs_n;
                return *entry;
        };

        auto first_entry = group.first;
        auto const last_entry = group.last;
        algos::for_each(first_entry, last_entry,
                        [next_iovec](TextOutputGroupEntry &x) {
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
        SPDR_BEGIN(global_spdr, "logging", "writev");
        writev(filedesc, iovecs, iovecs_n);
        SPDR_END(global_spdr, "logging", "writev");
}

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

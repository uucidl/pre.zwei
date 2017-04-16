#include "zwei_logging.hpp"

#include "zwei_inlines.hpp"
#include "zwei_textoutputgroup.hpp"

#include "algos.hpp"

#include <sys/uio.h>
#include <time.h>

#define IOVECS_CAPACITY (128 >= IOV_MAX ? IOV_MAX : 128)

zw_internal char *isodate_now(char *isodate_buffer, size_t isodate_buffer_size)
{
        auto clock = time(NULL);
        struct tm instant_tm;
        if (gmtime_r(&clock, &instant_tm)) {
                auto res = strftime(isodate_buffer, isodate_buffer_size,
                                    "%Y-%m-%dT%H:%M%z", &instant_tm);
                return isodate_buffer + res;
        }
        return isodate_buffer;
}

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

zw_internal iovec *text_output_group_print(int filedesc,
                                           iovec *iovecs,
                                           iovec *iovecs_next,
                                           iovec *iovecs_last,
                                           TextOutputGroup const &group)
{
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
        iovecs_write(&iovecs_next, iovecs, filedesc);
        return iovecs_next;
}

void text_output_group_print(int filedesc, TextOutputGroup const &group)
{
        struct iovec iovecs[IOVECS_CAPACITY];
        auto const iovecs_last = iovecs + IOVECS_CAPACITY;
        text_output_group_print(filedesc, iovecs, iovecs, iovecs_last, group);
}

zw_internal TextOutputGroup *
trace_prefix(char const *category, char *buffer, size_t buffer_size)
{
        auto stack_arena = memory_arena(buffer, buffer_size);
        auto tog = textoutputgroup_allocate(
            &stack_arena, sizeof "TRACE:YYYY-MM-DDTHH:MM+zz:yy");
        push_cstr(tog, "TRACE:");
        auto isodate_buffer_size = 1 + sizeof "YYYY-MM-DDTHH:MM+zz:yy";
        auto isodate_buffer =
            textoutputgroup_allocate_str(tog, isodate_buffer_size);
        auto isodate_last = isodate_now(isodate_buffer, isodate_buffer_size);
        push_extent(tog, reinterpret_cast<uint8_t *>(isodate_buffer),
                    isodate_last - isodate_buffer);
        push_cstr(tog, ":");
        return tog;
}

zw_internal void
trace_print(int filedesc, char const *category, char const *message)
{
        struct iovec iovecs[IOVECS_CAPACITY];
        auto iovecs_first = algos::begin(iovecs);
        auto const iovecs_last = algos::end(iovecs);

        char buffer[4096];
        auto tog = trace_prefix(category, buffer, sizeof buffer);
        push_cstr(tog, message);
        push_newline(tog);
        iovecs_first = text_output_group_print(filedesc, iovecs, iovecs_first,
                                               iovecs_last, *tog);
        writev(2, iovecs, int(iovecs_first - iovecs));
}

void error_print(char const *message) { trace_print(2, "ERROR:", message); }

void trace_print(char const *message) { trace_print(1, "TRACE:", message); }

zw_internal void
trace(int filedesc, char const *category, TextOutputGroup *group)
{
        struct iovec iovecs[IOVECS_CAPACITY];
        auto iovecs_first = algos::begin(iovecs);
        auto const iovecs_last = algos::end(iovecs);

        char buffer[8192];
        auto tog = trace_prefix(category, buffer, sizeof buffer);
        iovecs_first = text_output_group_print(filedesc, iovecs, iovecs_first,
                                               iovecs_last, *tog);

        push_newline(group);
        iovecs_first = text_output_group_print(filedesc, iovecs, iovecs_first,
                                               iovecs_last, *group);
        clear(group);
}

void trace(TextOutputGroup *group) { trace(1, "TRACE:", group); }

void error(TextOutputGroup *group) { trace(2, "ERROR:", group); }

#undef IOVECS_CAPACITY

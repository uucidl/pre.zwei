#include "zwei_logging.hpp"

#include <cstdarg>
#include <cstdio>

void allocate(TextOutputGroup &group, MemoryArena *arena, size_t reference_size)
{
        size_t memory_size =
            (9 * reference_size + 1) * (sizeof(TextOutputGroupEntry)) / 10;
        uint8_t *memory =
            static_cast<uint8_t *>(push_bytes(arena, memory_size));
        size_t entries_size = 9 * memory_size / 10;

        group.first = reinterpret_cast<TextOutputGroupEntry *>(memory);
        group.last = group.first;
        group.entries_capacity = entries_size / sizeof(TextOutputGroupEntry);

        memory = memory + entries_size;
        memory_size -= entries_size;
        group.bytes_arena = memory_arena(memory, memory_size);
}

void clear(TextOutputGroup &group)
{
        group.last = group.first;
        group.bytes_arena.used = 0;
}

// unsafe in the sense that it lets control characters through
static void unsafe_push_back_permanent_byte(TextOutputGroup &group,
                                            uint8_t const *x)
{
        if (group.last == group.first + group.entries_capacity) {
                group.truncated = true;
                return;
        }
        auto *entry = group.last;
        entry->first = x;
        entry->count = 1;
        group.last = entry + 1;
}

void push_back_tab(TextOutputGroup &group)
{
        static uint8_t tab = '\t';
        unsafe_push_back_permanent_byte(group, &tab);
}

void push_back_newline(TextOutputGroup &group)
{
        static uint8_t nl = '\n'; // TODO(nicolas): TAG(portability): some
                                  // platforms have special nl
        unsafe_push_back_permanent_byte(group, &nl);
}

void push_back_extent(TextOutputGroup &group, uint8_t const *first, size_t n)
{
        auto const is_control_char = [](uint8_t x) { return (x >> 5) == 0; };
        while (n) {
                auto a = algos::find_if_n(
                    first, n, [=](uint8_t x) { return !is_control_char(x); });
                auto b = algos::find_if_n(a.first, a.second, is_control_char);
                {
                        // Escape control characters
                        uint8_t const *control_char_first = first;
                        uint8_t const *const control_char_last = a.first;
                        size_t const dest_size =
                            2 * (control_char_last -
                                 control_char_first); // we prepend ^
                        uint8_t *const dest = push_array_rvalue(
                            &group.bytes_arena, dest, dest_size);
                        if (!dest) {
                                return; // NOTE(nicolas): out of memory
                        }
                        uint8_t *dest_first = dest;
                        uint8_t *const dest_last = dest + dest_size;
                        for (; dest_first != dest_last &&
                               control_char_first != control_char_last;
                             ++control_char_first) {
                                *dest_first = '^';
                                ++dest_first;
                                *dest_first = *control_char_first | (2 << 5);
                                ++dest_first;
                        }
                        push_back_extent(group, dest, dest_size);
                }

                if (group.last == group.first + group.entries_capacity) {
                        group.truncated = true;
                        return;
                }
                auto *entry = group.last;
                entry->first = a.first;
                entry->count = b.first - a.first;
                group.last = entry + 1;

                first = b.first;
                n = b.second;
        }
}

void push_back_cstr(TextOutputGroup &group, char const *cstr)
{
        push_back_extent(group, (uint8_t *)cstr, cstr_len(cstr));
}

void push_back_formatted(TextOutputGroup &group, char const *fmt, ...)
{
        size_t non_null_count = 0;
        va_list original_args;
        va_start(original_args, fmt);
        DEFER(va_end(original_args));
        {
                va_list args;
                va_copy(args, original_args);
                DEFER(va_end(args));

                non_null_count = vsnprintf(0, 0, fmt, args);
                zw_assert(non_null_count > 0, "invalid format string");
        }

        size_t buffer_size = 1 + non_null_count;
        char *buffer =
            push_array_rvalue(&group.bytes_arena, buffer, buffer_size);
        if (!buffer) {
                return;
        }

        std::vsnprintf(buffer, buffer_size, fmt, original_args);
        push_back_extent(group, (uint8_t *)buffer, non_null_count);
}

void push_back_u32(TextOutputGroup &group, uint32_t x)
{
        push_back_formatted(group, "%u", x);
}

void push_back_u64(TextOutputGroup &group, uint64_t x)
{
        push_back_formatted(group, "%llu", x);
}

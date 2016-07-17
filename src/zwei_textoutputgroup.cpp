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

TextOutputGroupEntry *
push_back_extent(TextOutputGroup &group, uint8_t const *first, size_t n)
{
        if (group.last == group.first + group.entries_capacity) {
                group.truncated = true;
                return nullptr;
        }

        auto *entry = group.last;
        entry->first = first;
        entry->count = n;
        group.last = entry + 1;

        return entry;
}

TextOutputGroupEntry *push_back_cstr(TextOutputGroup &group, char const *cstr)
{
        return push_back_extent(group, (uint8_t *)cstr, cstr_len(cstr));
}

TextOutputGroupEntry *
push_back_formatted(TextOutputGroup &group, char const *fmt, ...)
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
                return nullptr;
        }

        std::vsnprintf(buffer, buffer_size, fmt, original_args);
        return push_back_extent(group, (uint8_t *)buffer, non_null_count);
}

TextOutputGroupEntry *push_back_u32(TextOutputGroup &group, uint32_t x)
{
        return push_back_formatted(group, "%u", x);
}

TextOutputGroupEntry *push_back_u64(TextOutputGroup &group, uint64_t x)
{
        return push_back_formatted(group, "%llu", x);
}

#pragma once

#include "zwei_inlines.hpp"

// TODO(nicolas): whenever a segment of string is smaller than this 16
// bytes, then it's worth actually storing it in place as bytes
// directly
struct TextOutputGroupEntry {
        uint8_t const *first;
        size_t count;
};

/**
 * Container of commands for outputting (formatted) text.
 */
struct TextOutputGroup {
        TextOutputGroupEntry *first;
        TextOutputGroupEntry *last;
        size_t entries_capacity;
        bool truncated;
        MemoryArena bytes_arena;
};

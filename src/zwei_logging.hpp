#pragma once

// TODO(nicolas): question and critique this notion of text output group.
// What is the problem that it solves? Does it solve it well?

// TODO(nicolas): traces should feature a timestamp for
// performance and auditing. i.e. the platform layer should
// offer a logging service.

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

/**
 * Allocate the group so that it's at least capable of receiving about
 * `reference_size` bytes.
 *
 * Pushing back onto a full group will mark it as truncated.
 */
void allocate(TextOutputGroup &group,
              MemoryArena *arena,
              size_t reference_size);

void clear(TextOutputGroup &group);
TextOutputGroupEntry *
push_back_extent(TextOutputGroup &group, uint8_t const *first, size_t n);
TextOutputGroupEntry *push_back_cstr(TextOutputGroup &group, char const *cstr);
TextOutputGroupEntry *
push_back_formatted(TextOutputGroup &group, char const *fmt, ...);
TextOutputGroupEntry *push_back_u32(TextOutputGroup &group, uint32_t x);
TextOutputGroupEntry *push_back_u64(TextOutputGroup &group, uint64_t x);

void text_output_group_print(int filedesc, TextOutputGroup const &group);

/** traces and clears the group */
void trace(TextOutputGroup &group);

/** traces and clears the group */
void error(TextOutputGroup &group);

void error_print(char const *message);
void trace_print(char const *message);
void trace_print_n(char const *message, size_t message_len);

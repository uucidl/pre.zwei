#pragma once

#include <cstddef>
#include <cstdint>

// TODO(nicolas): question and critique this notion of text output group.
// What is the problem that it solves? Does it solve it well? It seems
// that it creates more useability problems than help. Most users of a
// logging facility want a fire&forget facility.

struct MemoryArena;

/**
 * Container of commands for outputting (formatted) text.
 *
 * Character data is in general only lazily copied, that is, it must be kept
 * alive by the user.
 */
struct TextOutputGroup;

/**
 * Allocate the group so that it's at least capable of receiving about
 * `reference_size` bytes.
 *
 * Pushing back onto a full group will mark it as truncated.
 */
TextOutputGroup *textoutputgroup_allocate(MemoryArena *arena,
                                          size_t reference_size);

// TODO(nicolas): give this more unique names

void clear(TextOutputGroup *group);

// terminal control characters
void push_tab(TextOutputGroup *group);
void push_newline(TextOutputGroup *group);

// these functions filter out control characters
void push_extent(TextOutputGroup *group, uint8_t const *first, size_t n);
void push_cstr(TextOutputGroup *group, char const *cstr);
void push_formatted(TextOutputGroup *group, char const *fmt, ...);
void push_u32(TextOutputGroup *group, uint32_t x);
void push_u64(TextOutputGroup *group, uint64_t x);

// allocate space for a string
char *textoutputgroup_allocate_str(TextOutputGroup *group, size_t size);

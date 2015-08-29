#pragma once

#include <cstdint>
#include <cstdio>

#define EXPORT __attribute__((visibility("default")))

#if ZWEI_SLOW
#define zw_assert(condition, message)                                          \
        do {                                                                   \
                if (!(condition)) {                                            \
                        fputs(message, stderr);                                \
                        asm("int3");                                           \
                }                                                              \
        } while (0)
#else
#define zw_assert(condition, message) (void)(condition)
#endif

#define NCOUNT(array) (sizeof array) / (sizeof array[0])

#define TOKEN_PASTE_inner(x, y) x##y
#define TOKEN_PASTE(x, y) TOKEN_PASTE_inner(x, y)

#define GIGABYTES(x) (MEGABYTES(x) * 1024LL)
#define MEGABYTES(x) (KILOBYTES(x) * 1024LL)
#define KILOBYTES(x) (x * 1024LL)

/// use it to mark symbols which should not escape the compilation unit
#define zw_internal static

/// local variables that persist across calls
#define zw_local_persist static

// <BASIC integer types

typedef uint32_t bool32;

// ..BASIC integer types>

// <Error handling

template <typename ValueType> struct MayFail {
        ValueType value;
        uint32_t errorcode = 0;
};

template <typename ValueType> bool failed(MayFail<ValueType> const result)
{
        return result.errorcode != 0;
}

template <typename ValueType> ValueType just(MayFail<ValueType> const result)
{
        zw_assert(result.errorcode == 0, "valid result");
        return result.value;
}

// ..Error handling>
// <DEFER implementation

template <class Lambda> class AtScopeExit
{
        Lambda &action;

      public:
        AtScopeExit(Lambda &action) : action(action) {}
        ~AtScopeExit() { action(); }
};

#define DEFER_inner2(lambda_name, instance_name, ...)                          \
        auto lambda_name = [&]() { __VA_ARGS__; };                             \
        AtScopeExit<decltype(lambda_name)> instance_name(lambda_name);

#define DEFER_inner(counter, ...)                                              \
        DEFER_inner2(TOKEN_PASTE(Defer_lambda, counter),                       \
                     TOKEN_PASTE(Defer_instance, counter), __VA_ARGS__)

#define DEFER(...) DEFER_inner(__COUNTER__, __VA_ARGS__)

// ..DEFER implementation>

// <MEMORY ARENAS

/**
   only meant to work in one thread and one thread only.
 */

struct MemoryArena {
        uint8_t *base;
        size_t used;
        size_t size;
};

struct MemoryArena memory_arena(void *base, size_t size)
{
        return {(uint8_t *)base, 0, size};
}

inline void *push_bytes(struct MemoryArena *arena, size_t bytes)
{
        zw_assert(arena->used + bytes <= arena->size, "overallocating");
        if (arena->used + bytes > arena->size) {
                return nullptr;
        }
        uint8_t *result = arena->base + arena->used;

        arena->used += bytes;

        return result;
}

#define push_struct(arena_pointer, type)                                       \
        (type *) push_bytes(arena_pointer, sizeof(type))

#define push_pointer_rvalue(arena_pointer, lvalue)                             \
        static_cast<decltype(lvalue)>(push_bytes(arena_pointer, sizeof *lvalue))

#define push_array_rvalue(arena_pointer, lvalue, count)                        \
        static_cast<decltype(lvalue)>(                                         \
            push_bytes(arena_pointer, (count) * sizeof *lvalue))

// ..MEMORY ARENAS>

// <CSTRINGS

inline bool cstr_equals(char const *s1, char const *s2)
{
        zw_assert(s1, "s1 should not be null");
        zw_assert(s2, "s2 should not be null");
        while (s1[0] != 0 && s1[0] == s2[0]) {
                s1++;
                s2++;
        }

        return s1[0] == 0 && s2[0] == 0;
}

inline uint64_t cstr_len(char const *s)
{
        uint64_t size = 0;

        while (s[size]) {
                size++;
        };

        return size;
}

inline bool cstr_endswith(char const *s, char const *terminator)
{
        auto terminator_len = cstr_len(terminator);

        if (terminator_len == 0) {
                return false;
        }

        auto s_len = cstr_len(s);

        if (s_len < terminator_len) {
                return false;
        }

        return cstr_equals(&s[s_len - terminator_len], terminator);
}

inline char const *
cstr_walk_backwards_until(char const *start, char const *end, char character)
{
        zw_assert(end >= start, "incorrect start/end order");

        while (*end != character && end != start) {
                end--;
        }

        return end;
}

inline char const *cstr_last_occurrence(char const *s, char character)
{
        auto len = cstr_len(s);
        return cstr_walk_backwards_until(s, &s[len], character);
}

// ..CSTRINGS>

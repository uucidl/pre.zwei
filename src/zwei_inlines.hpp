#pragma once

#include "algos.hpp"
#include "algos_concepts.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>

#define EXPORT __attribute__((visibility("default")))

#if ZWEI_SLOW
#define zw_assert(condition, message)                                          \
        do {                                                                   \
                if (!(condition)) {                                            \
                        fputs(message, stderr);                                \
                        fputs("\n", stderr);                                   \
                        asm("int3");                                           \
                }                                                              \
        } while (0)
#else
#define zw_assert(condition, message) (void)(condition)
#endif

#define ZWEI_UNIT_TESTS (ZWEI_SLOW && ZWEI_INTERNAL)

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

/// global variables
#define zw_global

// <BASIC integer types

// TODO(nicolas): uint8_t -> u8 etc... s8 ; float4/float8 ; etc.. but think
// about semantic difference.  Marking the type with its size is nice
// and cool but we should be able to tell at a quick glance that a
// float is not an integer

typedef uint32_t bool32;

// ..BASIC integer types>

// <Error handling

// TODO(nicolas): look at std::optional and model some of it after it
template <typename ValueType> struct MayFail {
        ValueType value;
        uint32_t errorcode;

        MayFail() {}
        MayFail(ValueType x) : value(std::move(x)), errorcode(0) {}

        static MayFail failure(uint32_t errorcode)
        {
                MayFail result;
                result.errorcode = errorcode;
                return result;
        }

        operator bool() const { return errorcode == 0; }
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

MemoryArena memory_arena(void *base, size_t size)
{
        return {(uint8_t *)base, 0, size};
}

inline void *push_bytes(struct MemoryArena *arena, size_t bytes)
{
        zw_assert(arena->used + bytes <= arena->size, "overallocating");
        if (arena->used + bytes > arena->size) {
                // TODO(nicolas) @security this basically is meant to be fatal
                // in every build
                return nullptr;
        }
        uint8_t *result = arena->base + arena->used;

        arena->used += bytes;

        return result;
}

MemoryArena push_sub_arena(MemoryArena &arena, size_t size)
{
        void *bytes = push_bytes(&arena, size);
        return memory_arena(bytes, size);
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

inline bool is_cstr_char(char const &x) { return x; }

inline bool cstr_equals(char const *s1, char const *s2)
{
        auto result = algos::find_mismatch_unguarded(s1, s2, is_cstr_char,
                                                     std::equal_to<char>());
        return *result.first == 0 && *result.second == 0;
}

inline uint64_t cstr_len(char const *s)
{
        return algos::count_unguarded(s, is_cstr_char);
}

inline char const *cstr_find(char const *s, char character)
{
        return algos::find_unguarded(s, is_cstr_char, character);
}

inline char const *cstr_find_last(char const *s, char character)
{
        return algos::find_last_unguarded(s, is_cstr_char, character);
}

inline bool cstr_endswith(char const *s, char const *suffix)
{
        if (!is_cstr_char(*suffix)) {
                return false;
        }
        return cstr_equals(cstr_find_last(s, *suffix), suffix);
}

/// try to concatenate the given c string when possible, always truncate
void cstr_cat(char *&string_last, char *buffer_last, char const *cstring)
{
        string_last =
            algos::copy_bounded_unguarded(cstring, is_cstr_char, string_last,
                                          buffer_last).second;
}

/// try to concatenate the given c string region when possible, always truncate
void cstr_cat(char *&string_last,
              char *buffer_last,
              char const *cstring,
              char const *cstring_end)
{
        string_last = algos::copy_bounded(cstring, cstring_end, string_last,
                                          buffer_last).second;
}

bool cstr_terminate(char *&string_last, char *buffer_last)
{
        using algos::sink;
        using algos::successor;

        if (string_last != buffer_last) {
                sink(string_last) = 0;
                string_last = successor(string_last);
                return true;
        }

        return false;
}

// ..CSTRINGS>

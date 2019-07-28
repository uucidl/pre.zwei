#pragma once

#include "zwei_config.hpp"

#if 0
#include "algos.hpp"
#include "algos_concepts.hpp"
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>

// tag switch cases which do fallthrough explicitely.
#if defined(__GNUC__) and __GNUC__ >= 7
#define TAG_FALLTHROUGH [[gnu::fallthrough]]
#else
#define TAG_FALLTHROUGH
#endif

// TODO(nicolas): TAG(incomplete)
#if defined(__clang__)              // TAG(clang)
#define zw_debugbreak() asm("int3") // TAG(x86)
#elif defined(__GNUC__)             // TAG(gcc)
#define zw_debugbreak() asm("int3") // TAG(x86)
#else
#define zw_debugbreak() __debugbreak() // TAG(msvc)
#endif

#define zw_assert(__condition, __message)                                      \
        do {                                                                   \
                if (!(__condition)) {                                          \
                        fputs(__message, stderr);                              \
                        fputs("\n", stderr);                                   \
                        zw_debugbreak();                                       \
                }                                                              \
        } while (0)

#define fatal_if(__condition, __message)                                       \
        fatal_ifnot(!(__condition), "fatal error: " __message)

#define fatal_ifnot(__condition, __message)                                    \
        {                                                                      \
                auto fatal_ifnot_cond = __condition;                           \
                zw_assert(fatal_ifnot_cond, "fatal error: " __message);        \
                if (!(fatal_ifnot_cond))                                       \
                        std::abort();                                          \
        }

#define fatal(__message) fatal_if(true, __message)

// AKA(NCOUNT) AKA(NELEMS)
template <typename T, size_t N> constexpr size_t countof(T (&)[N]) { return N; }

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

/// use to mark symbols that should escape the compilation unit and be public
/// through dynamic linking
#if defined(__clang__) || defined(__gnuc__)
#define zw_dll_exported __attribute__((visibility("default")))
#define zw_dll_imported __attribute__((visibility("default")))
#else
#define zw_dll_exported __declspec(dllexport)
#define zw_dll_imported __declspec(dllimport)
#endif

// <BASIC integer types

// TODO(nicolas): uint8_t -> u8 etc... s8 ; float4/float8 ; etc.. but think
// about semantic difference.  Marking the type with its size is nice
// and cool but we should be able to tell at a quick glance that a
// float is not an integer

typedef uint32_t bool32;

// ..BASIC integer types>

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

inline bool can_push_bytes(MemoryArena const &arena, size_t bytes)
{
        return arena.used + bytes < arena.size;
}

inline void *push_bytes(struct MemoryArena *arena, size_t bytes)
{
        zw_assert(arena->used + bytes <= arena->size, "overallocating");
        if (arena->used + bytes > arena->size) {
                // TODO(nicolas) TAG(security) this basically is meant to be
                // fatal in every build
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

void pop_unused(MemoryArena &parent, MemoryArena &sub_arena)
{
        zw_assert(parent.base + parent.used == sub_arena.base + sub_arena.size,
                  "pop should follow push");

        auto unused = sub_arena.size - sub_arena.used;
        parent.used -= unused;
        sub_arena.size -= unused;
}

#define push_typed(arena_pointer, type)                                        \
        (type *)push_bytes(arena_pointer, sizeof(type))

#define push_pointer_rvalue(arena_pointer, lvalue)                             \
        static_cast<decltype(lvalue)>(push_bytes(arena_pointer, sizeof *lvalue))

#define push_array_rvalue(arena_pointer, lvalue, count)                        \
        static_cast<decltype(lvalue)>(                                         \
            push_bytes(arena_pointer, (count) * sizeof *lvalue))

// ..MEMORY ARENAS>

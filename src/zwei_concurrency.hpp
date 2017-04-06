#pragma once

/**
 * @file: concurrency implementation primitives.
 *
 * TAG(concurrency)
 */

#if COMPILER_LLVM
#define COMPLETE_PREVIOUS_WRITES_BEFORE_FUTURE_WRITES                          \
        asm volatile("" ::: "memory")

inline uint32_t atomic_exchange_u32(uint32_t volatile *x, uint32_t new_value)
{
        uint32_t result = __sync_lock_test_and_set(x, new_value);
        return result;
}

inline uint32_t atomic_add_u32(uint32_t volatile *x, uint32_t y)
{
        uint32_t result = __sync_fetch_and_add(x, y);
        return result;
}

#else
#error "define primitives"
#endif

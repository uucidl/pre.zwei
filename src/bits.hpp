#pragma once

#include "algos_concepts.hpp"
#include <cstdint>

/**
   @file Utilities for writing to memory locations below the char
*/

struct NibbleProxy {
        uint8_t *byte;
        uint8_t bitoffset;

        NibbleProxy(uint8_t *byte, uint8_t bitoffset)
            : byte(byte), bitoffset(bitoffset)
        {
        }

        template <Integral I> NibbleProxy &operator=(I x)
        {
                *byte =
                    (*byte & ~((0xF << bitoffset))) | (x & 0xF) << bitoffset;
                return *this;
        }
};

/**
   Writing to nibbles of an integer.
*/
struct NibbleBigEndianOutputIterator {
        using OutputIteratorMe = NibbleBigEndianOutputIterator;
        using WritableMe = NibbleBigEndianOutputIterator;

        NibbleProxy nibble;

        NibbleBigEndianOutputIterator(uint8_t *first, int nibble_offset = 0)
            : nibble(first, nibble_offset == 0 ? 4 : 0)
        {
        }

        friend OutputIteratorMe successor(const OutputIteratorMe &x)
        {
                if (x.nibble.bitoffset == 4) {
                        return NibbleBigEndianOutputIterator(x.nibble.byte, 1);
                } else {
                        return NibbleBigEndianOutputIterator(x.nibble.byte + 1,
                                                             0);
                }
        }

        friend NibbleProxy &sink(WritableMe &x) { return x.nibble; }
};

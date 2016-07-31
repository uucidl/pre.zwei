#pragma once

#include "algos_concepts.hpp"
#include <cstdint>

/**
   @file Utilities for writing to memory locations below the char
*/

struct AssignableNibble {
        uint8_t *byte;
        uint8_t bitoffset;

        AssignableNibble(uint8_t *byte, uint8_t bitoffset)
            : byte(byte), bitoffset(bitoffset)
        {
        }

        template <Integral I> AssignableNibble &operator=(I x)
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

        AssignableNibble nibble;

        NibbleBigEndianOutputIterator(uint8_t *first, int bitoffset = 4)
            : nibble(first, bitoffset)
        {
        }

        friend OutputIteratorMe successor(const OutputIteratorMe &x)
        {
                if (x.nibble.bitoffset == 0) {
                        return NibbleBigEndianOutputIterator(x.nibble.byte + 1,
                                                             4);
                } else {
                        return NibbleBigEndianOutputIterator(
                            x.nibble.byte, x.nibble.bitoffset - 4);
                }
        }

        friend AssignableNibble &sink(WritableMe &x) { return x.nibble; }
};

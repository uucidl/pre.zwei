#include "bits.hpp"

#include "zwei_inlines.hpp"

#include <cstdio>

#define fatal_ifnot_formatted(__expr, ...)                                     \
        do {                                                                   \
                char buffer[4096];                                             \
                std::snprintf(buffer, sizeof(buffer), __VA_ARGS__);            \
                zw_assert(__expr, buffer);                                     \
        } while (0)

void test_bits()
{
        uint8_t byte[] = {
            0xCC,
            0xDD,
        };
        auto first_nibble = AssignableNibble(&byte[0], 0);
        auto second_nibble = AssignableNibble(&byte[0], 4);
        first_nibble = 0x1;
        fatal_ifnot_formatted(byte[0] == 0xC1, "expected 0x1 got: 0x%x\n",
                              byte[0]);
        second_nibble = 0x2;
        fatal_ifnot_formatted(byte[0] == 0x21, "expected 0x21 got: 0x%x\n",
                              byte[0]);
        auto nibble_iterator = NibbleBigEndianOutputIterator(byte);
        sink(nibble_iterator) = 0xC;
        fatal_ifnot_formatted(byte[0] == 0xC1, "expected 0xC1 got: 0x%x\n",
                              byte[0]);
        nibble_iterator = successor(nibble_iterator);
        sink(nibble_iterator) = 0xD;
        zw_assert(byte[0] == 0xCD, "hello");
        fatal_ifnot_formatted(byte[0] == 0xCD, "expected 0xCD got: 0x%x\n",
                              byte[0]);
        nibble_iterator = successor(nibble_iterator);
        sink(nibble_iterator) = 0xA;
        fatal_ifnot_formatted(byte[1] == 0xAD, "expected 0xAD got: 0x%x\n",
                              byte[1]);
        nibble_iterator = successor(nibble_iterator);
        sink(nibble_iterator) = 0xB;
        fatal_ifnot_formatted(byte[1] == 0xAB, "expected 0xAB got: 0x%x\n",
                              byte[1]);
}

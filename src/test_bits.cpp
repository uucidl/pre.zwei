void test_bits()
{
        trace_print("Testing nibble_writable");
        uint8_t byte[] = {
            0xCC, 0xDD,
        };

        auto first_nibble = NibbleProxy(&byte[0], 0);
        auto second_nibble = NibbleProxy(&byte[0], 4);
        first_nibble = 0x1;
        if (byte[0] != 0xC1) {
                printf("expected 0x1 got: 0x%x\n", byte[0]);
                exit(1);
        }
        second_nibble = 0x2;
        if (byte[0] != 0x21) {
                printf("expected 0x21 got: 0x%x\n", byte[0]);
                exit(1);
        }

        auto nibble_iterator = NibbleBigEndianOutputIterator(byte, 0);
        sink(nibble_iterator) = 0xC;
        if (byte[0] != 0xC1) {
                printf("expected 0xC1 got: 0x%x\n", byte[0]);
                exit(1);
        }
        nibble_iterator = successor(nibble_iterator);
        sink(nibble_iterator) = 0xD;
        zw_assert(byte[0] == 0xCD, "hello");
        if (byte[0] != 0xCD) {
                printf("expected 0xCD got: 0x%x\n", byte[0]);
                exit(1);
        }
        nibble_iterator = successor(nibble_iterator);
        sink(nibble_iterator) = 0xA;
        if (byte[1] != 0xAD) {
                printf("expected 0xAD got: 0x%x\n", byte[1]);
        }
        nibble_iterator = successor(nibble_iterator);
        sink(nibble_iterator) = 0xB;
        if (byte[1] != 0xAB) {
                printf("expected 0xAB got: 0x%x\n", byte[1]);
        }
        printf("%x%x\n", byte[0], byte[1]);
}

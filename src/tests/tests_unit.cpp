#include "../zwei_inlines.hpp"

#include "../secure_hash_standard.hpp"

#include <cstring>

zw_internal void test_sha1()
{
        uint8_t test_digest[20];
        uint8_t test_message[512] = {}; // 512 zeros
        uint8_t test_message_digest[20] = {
            0x5c, 0x3e, 0xb8, 0x00, 0x66, 0x42, 0x00, 0x02, 0xbc, 0x3d,
            0xcc, 0x7c, 0xa4, 0xab, 0x6e, 0xfa, 0xd7, 0xed, 0x4a, 0xe5,
        };
        sha1(test_message, 512, test_digest);
        zw_assert(0 == memcmp(test_digest, test_message_digest, 20),
                  "sha1(0 x 512) failed");

        uint8_t test_abc[] = "abc";
        uint8_t test_abc_digest[] = {
            0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
            0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d,
        };
        sha1(test_abc, 3, test_digest);
        zw_assert(0 == memcmp(test_digest, test_abc_digest, 20),
                  "sha1(abc) failed");

        // TODO(nicolas): try with 3767 too
        uint8_t test_message3[3768] = {};
        uint8_t test_message3_digest[20] = {
            0x83, 0xa6, 0x7b, 0xa7, 0x77, 0xf4, 0x7,  0xf8, 0x21, 0xb4,
            0x4b, 0xc1, 0xbe, 0x67, 0x6c, 0xf0, 0xf8, 0x25, 0xe8, 0x6,
        };
        sha1(test_message3, 3768, test_digest);
        zw_assert(0 == memcmp(test_digest, test_message3_digest, 20),
                  "sha1(0 x 3768) failed");
}

int main()
{
        test_sha1();
        // TODO(nicolas): should run the algos' tests as well here
        return 0;
}
#include "../secure_hash_standard.cpp"

#include "secure_hash_standard.hpp"

#if defined(PLATFORM_OSX)
// <OSX implementation for sha1..
#include <CommonCrypto/CommonDigest.h>
zw_internal void
sha1(uint8_t const *bytes, size_t bytes_size, uint8_t result[20])
{
        assert(bytes_size <= std::numeric_limits<CC_LONG>::max());
        CC_SHA1(bytes, static_cast<CC_LONG>(bytes_size), result);
}
// ..OSX implementation for sha1>
#else
zw_internal void
sha1(uint8_t const *bytes, size_t const bytes_size, uint8_t result[20])
{
        if (bytes_size >= 1LL << (64 - 8)) {
                // unsupported message length
                result = {};
                return;
        }

        // TODO(nicolas): can I simplify this algorithm/loop using a
        // IOBufferIterator
        // that auto-pads the content?
        enum { BLOCK_SIZE = 512 / 8,
        };

        uint32_t K[4] = {
            0x5a827999UL, 0x6ed9eba1UL, 0x8f1bbcdcUL, 0xca62c1d6UL,
        };
        uint32_t h0 = 0x67452301UL;
        uint32_t h1 = 0xefcdab89UL;
        uint32_t h2 = 0x98badcfeUL;
        uint32_t h3 = 0x10325476UL;
        uint32_t h4 = 0xc3d2e1f0UL;

        uint64_t message_length = 8 * bytes_size;
        uint32_t a, b, c, d, e;

        uint8_t padded_block[BLOCK_SIZE];
        size_t byte_index = 0;
        size_t leftover_byte_size =
            (BLOCK_SIZE - (bytes_size % BLOCK_SIZE)) % BLOCK_SIZE;
        size_t padded_byte_size = bytes_size + leftover_byte_size +
                                  (leftover_byte_size <= 8 ? BLOCK_SIZE : 0);
        size_t first_padding_byte_index = bytes_size;
        size_t total_size_destination_index = padded_byte_size - 8;
        zw_assert(bytes_size <= padded_byte_size, "");
#define byte(word, byte_index) (((word) >> (8 * byte_index)) & 0xff)
        // process blocks
        while (byte_index < padded_byte_size) {
                size_t block_end_index = byte_index + BLOCK_SIZE;
                if (block_end_index > bytes_size) {
                        // generating padding: padding starts with 0x80, is
                        // followed by many zeros then ends with the 64bit
                        // size of the content.
                        size_t rel_zeroing_start = 0;
                        if (first_padding_byte_index - 1 >= byte_index &&
                            first_padding_byte_index < block_end_index) {
                                size_t rel_bytes_end =
                                    first_padding_byte_index - byte_index;
                                memcpy(padded_block, bytes, rel_bytes_end);
                                rel_zeroing_start = rel_bytes_end;
                        }
                        if (first_padding_byte_index >= byte_index) {
                                padded_block[rel_zeroing_start] = 0x80;
                                ++rel_zeroing_start;
                        }
                        size_t rel_zeroing_end = BLOCK_SIZE;
                        if (total_size_destination_index < block_end_index) {
                                rel_zeroing_end =
                                    total_size_destination_index - byte_index;
                                uint8_t *dest = padded_block + rel_zeroing_end;
                                size_t byte_i = 8;
                                while (byte_i) {
                                        --byte_i;
                                        *dest =
                                            (message_length >> (8 * byte_i)) &
                                            0xff;
                                        ++dest;
                                }
                        }
                        memset(padded_block + rel_zeroing_start, 0,
                               rel_zeroing_end - rel_zeroing_start);
                        bytes = padded_block;
                }
                // preparing message schedule
                uint32_t w[80];
#define word(bytes)                                                            \
        (bytes[3] | (bytes[2] << 8) | (bytes[1] << 16) | (bytes[0] << 24))
                for (int i = 0; i < 16; i++) {
                        w[i] = word(bytes);
                        bytes += 4;
                        byte_index += 4;
                }
#define rotl32_n(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define rotl32_1(x) rotl32_n((x), 1)
                for (int i = 16; i < 80; i++) {
                        w[i] = rotl32_1(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^
                                        w[i - 16]);
                }
#undef word
                a = h0;
                b = h1;
                c = h2;
                d = h3;
                e = h4;
                for (int i = 0; i < 80; i++) {
                        int segment = i / 20;
                        // function calculation
                        uint32_t y = 0;
                        switch (segment) {
                        case 0: {
                                y = (b & c) ^ (~b & d);
                        } break;
                        case 1: {
                                y = b ^ c ^ d;
                        } break;
                        case 2: {
                                y = (b & c) ^ (b & d) ^ (c & d);
                        } break;
                        case 3: {
                                y = b ^ c ^ d;
                        } break;
                        }
                        // combination step
                        uint32_t temp =
                            rotl32_n(a, 5) + y + e + K[segment] + w[i];
                        e = d;
                        d = c;
                        c = rotl32_n(b, 30);
                        b = a;
                        a = temp;
                }

                h0 = a + h0;
                h1 = b + h1;
                h2 = c + h2;
                h3 = d + h3;
                h4 = e + h4;
#undef rotl32_1
#undef rotl32_n
        }

        uint32_t hashes[5] = {h0, h1, h2, h3, h4};
        size_t hashes_count = 5;
        uint32_t *first_hash = hashes;
        while (hashes_count) {
                result[0] = byte(*first_hash, 3);
                result[1] = byte(*first_hash, 2);
                result[2] = byte(*first_hash, 1);
                result[3] = byte(*first_hash, 0);
                result += 4;
                ++first_hash;
                --hashes_count;
        }
#undef byte
}
#endif

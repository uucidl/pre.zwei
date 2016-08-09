#ifndef UTF8_DECODE_PROTOTYPES
#define UTF8_DECODE_PROTOTYPES

#include <cstdint>

#define UTF8_INTERNAL static
#if defined(UTF8_EXTERN) // by default we are internal to the translation unit
#define UTF8_DECODE_API
#else
#define UTF8_DECODE_API UTF8_INTERNAL
#endif

/// @see
/// [economical-utf8.html](http://bjoern.hoehrmann.de/utf-8/decoder/dfa/index.html)
enum Utf8DecodeResult {
        Utf8DecodeResult_Accept = 0,
        Utf8DecodeResult_Reject = 1,
};

UTF8_DECODE_API uint32_t utf8_decode(uint32_t *state,
                                     uint32_t *codep,
                                     uint8_t byte);

#endif

#ifdef UTF8_DECODE_IMPLEMENTATION
#undef UTF8_DECODE_IMPLEMENTATION

UTF8_INTERNAL uint8_t const utf8d[] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   9,   9,   9,   9,   9,   9,
    9,   9,   9,   9,   9,   9,   9,   9,   9,   9,   7,   7,   7,   7,   7,
    7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,
    7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   8,   8,   2,
    2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,
    2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   0xa,
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x4, 0x3, 0x3,
    0xb, 0x6, 0x6, 0x6, 0x5, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
    0x8, 0x0, 0x1, 0x2, 0x3, 0x5, 0x8, 0x7, 0x1, 0x1, 0x1, 0x4, 0x6, 0x1, 0x1,
    0x1, 0x1, 1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   0,   1,   1,   1,   1,   1,   0,   1,   0,   1,   1,
    1,   1,   1,   1,   1,   2,   1,   1,   1,   1,   1,   2,   1,   2,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   2,   1,   1,   1,   1,   1,   1,   1,
    2,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   3,
    1,   3,   1,   1,   1,   1,   1,   1,   1,   3,   1,   1,   1,   1,   1,
    3,   1,   3,   1,   1,   1,   1,   1,   1,   1,   3,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
};

/**
 * Decodes UTF-8 streams.
 *
 * @param state must be Utf8DecodeResult_Accept at the beginning of the stream
 * @return state \in { Utf8DecodeResult_Accept, Utf8DecodeResult_Reject, ... }
 */
UTF8_DECODE_API uint32_t utf8_decode(uint32_t *state,
                                     uint32_t *codepoint,
                                     uint8_t byte)
{
        uint32_t type = utf8d[byte];

        *codepoint = (*state != Utf8DecodeResult_Accept)
                         ? (byte & 0x3fu) | (*codepoint << 6)
                         : (0xff >> type) & (byte);

        *state = utf8d[256 + *state * 16 + type];

        return *state;
}

#endif
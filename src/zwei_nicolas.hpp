#pragma once

struct MacRomanWorkaround
{
        enum {
                RUNE_COUNT = 8,
        };

        uint32_t utf8decoder_state;
        uint32_t utf8decoder_codepoint;
        uint32_t input_block_count;

        uint32_t ucs4_codepoints_block[RUNE_COUNT];
        uint8_t macintosh_chars[RUNE_COUNT];
        uint32_t macintosh_chars_utf8decoder_state;
        uint32_t macintosh_chars_utf8decoder_codepoint;

        uint8_t utf8_output_block[3*RUNE_COUNT];
        uint8_t utf8_output_block_count;
};

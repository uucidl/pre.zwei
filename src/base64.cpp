/**
   Parsing base64 content

   TAG(Base64)

   Original source code GPL licensed, in the hammer distribution.
*/

#include "base64.hpp"

#include "hammer.hpp"
#include "hammer_utils.hpp"

// helper: return the numeric value of a parsed base64 digit
zw_internal uint8_t bsfdig_value(const HParsedToken *p)
{
        uint8_t value = 0;

        if (p && p->token_type == TT_UINT) {
                uint8_t c = p->uint;
                if (c >= 0x41 && c <= 0x5A) // A-Z
                        value = c - 0x41;
                else if (c >= 0x61 && c <= 0x7A) // a-z
                        value = c - 0x61 + 26;
                else if (c >= 0x30 && c <= 0x39) // 0-9
                        value = c - 0x30 + 52;
                else if (c == '+')
                        value = 62;
                else if (c == '/')
                        value = 63;
        }

        return value;
};

zw_internal inline uint32_t add_base64_sextet(uint32_t x,
                                              HParsedToken *digit_token)
{
        return (x << 6) | bsfdig_value(digit_token);
}

HAMMER_ACTION(act_base64)
{
#define seq_append_byte(res, b) h_seq_snoc(res, H_MAKE_UINT(b))
        assert(p->ast->token_type == TT_SEQUENCE);
        assert(p->ast->seq->used == 2);

        HParsedToken *res = H_MAKE_SEQ();

        // concatenate base64_3 blocks
        const HParsedToken *b64_3 = p->ast->seq->elements[0];
        assert(b64_3->token_type == TT_SEQUENCE);
        algos::for_each(
            begin(b64_3), end(b64_3),
            [res, p](const HParsedToken *b64_3_element) {
                    assert(b64_3_element->token_type == TT_SEQUENCE);
                    HParsedToken **digits = b64_3_element->seq->elements;
                    uint32_t x = add_base64_sextet(0, digits[0]);
                    x = add_base64_sextet(x, digits[1]);
                    x = add_base64_sextet(x, digits[2]);
                    x = add_base64_sextet(x, digits[3]);
                    seq_append_byte(res, (x >> 16) & 0xFF);
                    seq_append_byte(res, (x >> 8) & 0xFF);
                    seq_append_byte(res, x & 0xFF);
            });

        // grab and analyze b64 end block (_2 or _1)
        const HParsedToken *b64_end = p->ast->seq->elements[1];
        if (b64_end->token_type == TT_SEQUENCE) {
                if (b64_end->seq->elements[2]->uint == '=') {
                        // base64 _1 block
                        HParsedToken **digits = b64_end->seq->elements;
                        uint32_t x;
                        x = add_base64_sextet(0, digits[0]);
                        x = add_base64_sextet(x, digits[1]);
                        seq_append_byte(res, (x >> 4) & 0xFF);
                } else {
                        // base64 _2 block
                        HParsedToken **digits = b64_end->seq->elements;
                        uint32_t x;
                        x = add_base64_sextet(0, digits[0]);
                        x = add_base64_sextet(x, digits[1]);
                        x = add_base64_sextet(x, digits[2]);
                        seq_append_byte(res, (x >> 10) & 0xFF);
                        seq_append_byte(res, (x >> 2) & 0xFF);
                }
        }
        return res;
#undef seq_append_byte
};

zw_internal struct Base64 global_base64;

const Base64 &make_base64(RFC5234 const &rfc5234)
{
        HParser *plus = h_ch('+');
        HParser *slash = h_ch('/');
        HParser *equals = h_ch('=');
        HParser *bsfdig = UH_ANY(rfc5234.ALPHA, rfc5234.DIGIT, plus, slash);
        HParser *bsfdig_4bit = UH_IN("AEIMQUYcgkosw048");
        HParser *bsfdig_2bit = UH_IN("AQgw");
        HParser *base64_3 = h_repeat_n(bsfdig, 4);
        HParser *base64_2 = UH_SEQ(bsfdig, bsfdig, bsfdig_4bit, equals);
        HParser *base64_1 = UH_SEQ(bsfdig, bsfdig_2bit, equals, equals);
        HParser *base64 =
            UH_SEQ(h_many(base64_3), h_optional(UH_ANY(base64_2, base64_1)));
        global_base64.base64 = h_action(base64, act_base64, NULL);
        return global_base64;
}

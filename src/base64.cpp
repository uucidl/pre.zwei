/**
   Parsing base64 content

   Original source code GPL licensed, in the hammer distribution.
*/

#include "base64.hpp"

#include "abnf_rfc5234.hpp"

#include "hammer.hpp"
#include "hammer_defines.hpp"
#include "hammer_utils.hpp"

struct HammerUintTokenSequenceIterator {
        HParsedToken **token_in_seq;

        friend bool operator==(HammerUintTokenSequenceIterator a,
                               HammerUintTokenSequenceIterator b)
        {
                return a.token_in_seq == b.token_in_seq;
        }

        friend bool operator!=(HammerUintTokenSequenceIterator a,
                               HammerUintTokenSequenceIterator b)
        {
                return !(a == b);
        }

        friend uint64_t source(HammerUintTokenSequenceIterator x)
        {
                auto &token = *(x.token_in_seq);
                fatal_ifnot(token->token_type == TT_UINT, "not a TT_UINT");
                return token->uint;
        }

        friend HammerUintTokenSequenceIterator
        successor(HammerUintTokenSequenceIterator x)
        {
                return {x.token_in_seq + 1};
        }
};

zw_internal std::pair<uint8_t *, uint8_t>
gather_8from6bits(uint8_t *f, uint8_t *l, uint8_t *d_f)
{
        using algos::sink;
        using algos::source;
        using algos::successor;

        auto const min = [](uint8_t a, uint8_t b) { return a < b ? a : b; };

        auto const mask = [](uint8_t x, uint8_t bits) -> uint8_t {
                unsigned int m = (1 << bits) - 1;
                return x & m;
        };

        uint8_t d;
        int d_bits_f;
        int d_bits_l;

        d = 0;
        d_bits_l = 8;
        d_bits_f = 0;

        uint8_t s;
        int s_bits_f = 0;
        int s_bits_l = 0;

        while (f != l) {
                // NOTE(nicolas): might be useful for later optimizing:
                //
                // Repeating pattern:
                //
                // | size | ++d | ++s |
                // |------+-----+-----|
                // |    6 |     | s   |
                // |    2 | d   |     |
                // |    4 |     | s   |
                // |    4 | d   |     |
                // |    2 |     | s   |
                // |    6 | d   | s   |
                // |------+-----+-----|
                // |  ... | ... | ... |

                if (s_bits_f == s_bits_l) {
                        s = source(f);
                        f = successor(f);
                        s_bits_l = 6;
                        s_bits_f = 0;
                }

                auto const size = min(d_bits_l - d_bits_f, s_bits_l - s_bits_f);
                auto t = mask(s >> (s_bits_l - size), size);
                t <<= d_bits_l - size;
                d |= t;

                s_bits_l -= size;
                d_bits_l -= size;
                if (d_bits_f == d_bits_l) {
                        sink(d_f) = d;
                        d_f = successor(d_f);
                        d = 0;
                        d_bits_l = 8;
                        d_bits_f = 0;
                }
        }
        return {d_f, d_bits_l};
}

zw_internal void bsf_decode(uint8_t *first, uint8_t *last)
{
        auto const value = [](uint8_t x) -> uint8_t {
                uint8_t y;
                if (x >= 0x41 && x <= 0x5A) // A-Z
                        y = x - 0x41;
                else if (x >= 0x61 && x <= 0x7A) // a-z
                        y = x - 0x61 + 26;
                else if (x >= 0x30 && x <= 0x39) // 0-9
                        y = x - 0x30 + 52;
                else if (x == '+')
                        y = 62;
                else if (x == '/')
                        y = 63;
                else {
                        fatal("unexpected");
                        y = 0;
                }
                return y;
        };
        algos::apply_copy(first, last, first, value);
}

// encoded digits in [first, last) are turned into bytes [first, return_value)
zw_internal uint8_t *base64_pipeline(uint8_t *first, uint8_t *last)
{
        bsf_decode(first, last); // [first,last) contains sextets
        auto x = gather_8from6bits(first, last, first);
        return x.first;
}

HAMMER_ACTION(base64_action)
{
        assert(p->ast->token_type == TT_SEQUENCE);
        assert(p->ast->seq->used == 2);

        const HParsedToken *b64_3 = p->ast->seq->elements[0];
        assert(b64_3->token_type == TT_SEQUENCE);

        // max size necessary for the array is:
        size_t bytes_size = 4 * (end(b64_3) - begin(b64_3)) + 3;
        uint8_t *const bytes = (uint8_t *)h_arena_malloc(p->arena, bytes_size);
        uint8_t *const bytes_end = bytes + bytes_size;
        uint8_t *bytes_last = bytes;
        // concatenate base64_3 blocks
        for (HParsedToken const *token : b64_3) {
                fatal_ifnot(token->token_type == TT_SEQUENCE, "unexpected");
                HParsedToken **digits = token->seq->elements;
                bytes_last = algos::copy(
                    HammerUintTokenSequenceIterator{digits},
                    HammerUintTokenSequenceIterator{digits + 4}, bytes_last);
                zw_assert(bytes_last <= bytes_end, "overrun");
        }
        // concatenate b64 end block (_2 or _1) where the '=' sentinel
        // may indicate a stop
        const HParsedToken *b64_end = p->ast->seq->elements[1];
        if (b64_end->token_type == TT_SEQUENCE) {
                HParsedToken **digits = b64_end->seq->elements;
                int count = digits[2]->uint == '=' ? 2 : 3;
                bytes_last =
                    algos::copy(HammerUintTokenSequenceIterator{digits},
                                HammerUintTokenSequenceIterator{digits + count},
                                bytes_last);
                zw_assert(bytes_last <= bytes_end, "overrun");
        }
        bytes_last = base64_pipeline(bytes, bytes_last);
        return H_MAKE_BYTES(bytes, bytes_last - bytes);
};

zw_internal struct Base64 global_base64;

const Base64 &make_base64(ABNF_RFC5234 const &rfc5234)
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
        global_base64.base64 = h_action(base64, base64_action, NULL);
#if ZWEI_UNIT_TESTS
        trace_print("Testing base64 parsers:");
        auto const print_bytes = [](FILE *file, const HParsedToken *ast,
                                    int /*offset*/, int /*increment*/) {
                fatal_ifnot(ast->token_type == TT_BYTES, "expected bytes");
                fprintf(file, "bytes as ascii: ");
                algos::for_each_n(
                    ast->bytes.token, ast->bytes.len,
                    [file](uint8_t x) { fprintf(file, "%c", x); });
                fprintf(file, "\n");
        };

        trace_print("should be equal to `any carnal pleasure.`:");
        CHECK_PARSER2(global_base64.base64,
                      "YW55IGNhcm5hbCBwbGVhc3VyZS4=", print_bytes);
        CHECK_PARSER2(global_base64.base64,
                      "TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFz"
                      "b24sIGJ1dCBieSB0aGlzIHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhl"
                      "ciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2YgdGhlIG1pbmQsIHRo"
                      "YXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29u"
                      "dGludWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25v"
                      "d2xlZGdlLCBleGNlZWRzIHRoZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55"
                      "IGNhcm5hbCBwbGVhc3VyZS4=",
                      print_bytes);
#endif
        return global_base64;
}

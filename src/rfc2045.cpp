#include "rfc2045.hpp"

#include "zwei_inlines.hpp"

#include "bits.hpp"
#include "hammer_utils.hpp"
#include "rfc5234.hpp"
#include "zwei_logging.hpp"

#include "hammer.hpp"

HAMMER_ACTION(act_hex_octet)
{
        auto ast = p->ast;
        zw_assert(ast->token_type == TT_SEQUENCE, "unexpected token type");
        zw_assert(ast->seq->used == 2, "unexpected sequence");

        uint8_t result;
        algos::apply_copy_n(begin(ast), 2,
                            NibbleBigEndianOutputIterator(&result),
                            [](const HParsedToken *x) {
                                    uint8_t digit_ascii = H_CAST_UINT(x);
                                    if (digit_ascii >= 0x41) {
                                            return 10 + (digit_ascii - 0x41);
                                    }
                                    return digit_ascii - 0x30;
                            });
        return H_MAKE_UINT(result);
}

HAMMER_ACTION(act_lenient_hex_octet)
{
        auto ast = p->ast;
        zw_assert(ast->token_type == TT_SEQUENCE, "unexpected token type");
        zw_assert(ast->seq->used == 2, "unexpected sequence");

        uint8_t result;
        algos::apply_copy_n(begin(ast), 2,
                            NibbleBigEndianOutputIterator(&result),
                            [](const HParsedToken *x) {
                                    uint8_t digit_ascii = H_CAST_UINT(x);
                                    if (digit_ascii >= 0x61) {
                                            return 10 + (digit_ascii - 0x61);
                                    }
                                    if (digit_ascii >= 0x41) {
                                            return 10 + (digit_ascii - 0x41);
                                    }
                                    return digit_ascii - 0x30;
                            });
        return H_MAKE_UINT(result);
}

zw_internal RFC2045 global_rfc2045_parsers;

const RFC2045 &make_rfc2045(const RFC5234 &rfc5234)
{
        // # 6.7.  Quoted-Printable Content-Transfer-Encoding
        H_ARULE(hex_octet, h_right(h_ch('='), h_repeat_n(rfc5234.HEXDIG, 2)));

        H_RULE(lenient_hexdig, UH_ANY(rfc5234.HEXDIG, h_ch_range(0x61, 0x66)));

        H_ARULE(lenient_hex_octet,
                h_right(h_ch('='), h_repeat_n(lenient_hexdig, 2)));

        H_RULE(safe_char, UH_ANY(h_ch_range(33, 60), h_ch_range(62, 126)));

        H_RULE(ptext, UH_ANY(hex_octet, safe_char));

        H_RULE(qp_section, h_many(UH_SEQ(h_many(rfc5234.WSP), ptext)));

        H_RULE(qp_segment, UH_SEQ(qp_section, h_many(rfc5234.WSP), h_ch('=')));

        // TODO(nicolas): make it no more than 76
        H_RULE(qp_part, qp_section);

        H_RULE(qp_line,
               UH_SEQ(h_many(UH_SEQ(qp_segment, rfc5234.CRLF)), qp_part));

        H_RULE(quoted_printable,
               UH_SEQ(qp_line, h_many(UH_SEQ(rfc5234.CRLF, qp_line))));

        global_rfc2045_parsers.hex_octet = hex_octet;
        global_rfc2045_parsers.lenient_hex_octet = lenient_hex_octet;
        global_rfc2045_parsers.quoted_printable = quoted_printable;
        global_rfc2045_parsers.safe_char = safe_char;

#if ZWEI_UNIT_TESTS
        trace_print("Testing global_rfc2045_parsers parsers:");
        CHECK_PARSER(safe_char, "a");
        CHECK_PARSER(hex_octet, "=CD");
        CHECK_PARSER(hex_octet, "=15");
        CHECK_PARSER(h_many(ptext), "ab=CD=A5");
        CHECK_PARSER(h_many(UH_ANY(ptext, rfc5234.WSP)), "abcdef =CDab");
        CHECK_PARSER(qp_section, "abcdef =CDab");
        CHECK_PARSER(quoted_printable,
                     "nicolas =\r\nwant =\r\na =CAke\r\n and");
        CHECK_PARSER(quoted_printable, "a");
#endif

        return global_rfc2045_parsers;
}

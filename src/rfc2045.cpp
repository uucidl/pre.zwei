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

HAMMER_ACTION(act_content_transfer_encoding)
{
        auto value = H_CAST_UINT(p->ast);
        auto tagged_value = H_MAKE_UINT(value);
        tagged_value->token_type =
            RFC2045HammerTT(CONTENT_TRANSFER_ENCODING_FIELD);
        return tagged_value;
}

zw_internal RFC2045 global_rfc2045_parsers;

const RFC2045 &make_rfc2045(const RFC5234 &rfc5234,
                            const RFC5322_Base &rfc5322_base)
{
#define UUH_FIELD(lit, right)                                                  \
        h_middle(h_itoken((uint8_t *)lit, (sizeof lit) - 1), right,            \
                 rfc5234.CRLF)

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

        // # 6.1 Content-Transfer-Encoding Syntax
        // TODO(nicolas): There should be a general system for enums so that I
        // don't have to write the enum recognition by hand.
        H_RULE(mechanism,
               UH_ANY(UH_IENUM("7bit", ContentTransferEncoding_7BIT),
                      UH_IENUM("8bit", ContentTransferEncoding_8BIT),
                      UH_IENUM("binary", ContentTransferEncoding_BINARY),
                      UH_IENUM("quoted-printable",
                               ContentTransferEncoding_QUOTED_PRINTABLE),
                      UH_IENUM("base64", ContentTransferEncoding_BASE64)));
        H_ARULE(content_transfer_encoding,
                UUH_FIELD("Content-Transfer-Encoding:",
                          h_right(h_many(rfc5234.WSP), mechanism)));

        // TODO(nicolas): implement content-type
        // TODO(nicolas): implement content-transfer-encoding

        allocate_token_types(algos::begin(rfc2045_token_types),
                             algos::end(rfc2045_token_types));
        global_rfc2045_parsers.hex_octet = hex_octet;
        global_rfc2045_parsers.lenient_hex_octet = lenient_hex_octet;
        global_rfc2045_parsers.quoted_printable = quoted_printable;
        global_rfc2045_parsers.safe_char = safe_char;
        global_rfc2045_parsers.content_type_header_field = nullptr;
        global_rfc2045_parsers.content_transfer_encoding_header_field =
            content_transfer_encoding;

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

        auto make_checker = [](ContentTransferEncodingType type) {
                return [type](const HParsedToken *ast) {
                        auto token_check = RFC2045TokenIs(
                            ast, CONTENT_TRANSFER_ENCODING_FIELD);
                        zw_assert(token_check, "bad token type");
                        auto value_check = ast->uint == type;
                        zw_assert(value_check, "bad value");
                        return token_check && value_check;
                };
        };

        VALIDATE_PARSER(content_transfer_encoding,
                        "Content-Transfer-Encoding: 7BIT\r\n",
                        make_checker(ContentTransferEncoding_7BIT));
        VALIDATE_PARSER(content_transfer_encoding,
                        "Content-Transfer-Encoding: bAse64\r\n",
                        make_checker(ContentTransferEncoding_BASE64));
        VALIDATE_PARSER(content_transfer_encoding,
                        "Content-Transfer-Encoding: quoted-printable\r\n",
                        make_checker(ContentTransferEncoding_QUOTED_PRINTABLE));

#endif

        return global_rfc2045_parsers;
}

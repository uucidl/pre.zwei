#include "rfc2045.hpp"

#include "zwei_inlines.hpp"

#include "bits.hpp"
#include "hammer_utils.hpp"
#include "mime_types.hpp"
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

        auto CFWS = rfc5322_base.CFWS;
        auto quoted_string = rfc5322_base.quoted_string;

#define UUH_FIELD(lit, right)                                                  \
        h_middle(h_ascii_itoken((uint8_t *)lit, (sizeof lit) - 1), right,      \
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

        H_RULE(qp_part, qp_section);

        H_RULE(qp_line,
               UH_SEQ(h_many(UH_SEQ(qp_segment, rfc5234.CRLF)), qp_part));

        H_RULE(quoted_printable,
               UH_SEQ(qp_line, h_many(UH_SEQ(rfc5234.CRLF, qp_line))));

        // # 6.1 Content-Transfer-Encoding Syntax
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

        // # 5.1.  Syntax of the Content-Type Header Field
        H_RULE(tspecials, UH_IN("()<>@,;:\\\"/[]?="));
        H_RULE(
            token_char,
            h_butnot(rfc5234.CHAR, UH_ANY(rfc5234.SP, rfc5234.CTL, tspecials)));
        H_RULE(token, h_many1(token_char));
        H_RULE(attribute, h_middle(h_optional(CFWS), token, h_optional(CFWS)));
        H_RULE(value, h_middle(h_optional(CFWS), UH_ANY(token, quoted_string),
                               h_optional(CFWS)));
        H_RULE(parameter, UH_SEQ(attribute, h_ch('='), value));
        // NOTE(nicolas) I chose not to check IETF types
        H_RULE(extension_token, token);
        H_RULE(discrete_type,
               UH_ANY(UH_ITOKEN("text"), UH_ITOKEN("image"), UH_ITOKEN("audio"),
                      UH_ITOKEN("video"), UH_ITOKEN("application"),
                      extension_token));
        H_RULE(composite_type, UH_ANY(UH_ITOKEN("message"),
                                      UH_ITOKEN("multipart"), extension_token));

        H_RULE(type,
               h_middle(h_optional(CFWS), UH_ANY(discrete_type, composite_type),
                        h_optional(CFWS)));
        // NOTE(nicolas): I chose not to check IANA or extension
        // tokens
        H_RULE(subtype, h_middle(h_optional(CFWS), token, h_optional(CFWS)));
        H_RULE(content_type,
               UUH_FIELD("Content-Type:",
                         UH_SEQ(type, h_ch('/'), subtype,
                                h_many(UH_SEQ(h_ch(';'), parameter)))));

        allocate_token_types(algos::begin(rfc2045_token_types),
                             algos::end(rfc2045_token_types));
        global_rfc2045_parsers.hex_octet = hex_octet;
        global_rfc2045_parsers.lenient_hex_octet = lenient_hex_octet;
        global_rfc2045_parsers.quoted_printable = quoted_printable;
        global_rfc2045_parsers.safe_char = safe_char;
        global_rfc2045_parsers.content_type_header_field = content_type;
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

        CHECK_PARSER(content_type, "Content-Type:text/plain\r\n");
        CHECK_PARSER(
            content_type,
            "Content-type: text/plain; charset=us-ascii (Plain text)\r\n");
        CHECK_PARSER(content_type,
                     "Content-type: text/plain; charset=\"us-ascii\"\r\n");
#endif

        return global_rfc2045_parsers;
}

// TODO(nicolas): NEXT implement querying for media type
#if ZWEI_DISABLED
zw_internal size_t get_media_type_size(HParsedToken *ast) { return 0; }

zw_internal uint8_t *copy_media_type(HParsedToken *ast, uint8_t *d_bytes)
{
        return d_bytes;
}
#endif

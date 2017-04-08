/**

   Representing 8-bit content in headers

   @tags "Encoded Word" "Quoted Printable" "Base64"

   [[https://tools.ietf.org/html/rfc2047.txt]]
 */

#include "rfc2047.hpp"
#include "base64.hpp"

#include "abnf_rfc5234.hpp"
#include "hammer_utils.hpp"
#include "mime_types.hpp"
#include "zwei_logging.hpp"
#include "zwei_types.hpp"

#include "hammer.hpp"
#include <utility> // for pair

namespace
{

Charset parse_charset_name_n(uint8_t const *charset_name,
                             size_t charset_name_size)
{
#define LITE(lit) lit, sizeof(lit) - 1

        struct A {
                char const *name;
                size_t name_size;
                Charset charset;
        } known_charsets[] = {
            {LITE("us-ascii"), Charset_US_ASCII},
            {LITE("iso-8859-1"), Charset_ISO_8859_1},
            {LITE("iso-8859-15"), Charset_ISO_8859_15},
            {LITE("utf-8"), Charset_UTF_8},
        };

        using algos::begin;
        using algos::end;

        auto charset_with_name = [charset_name, charset_name_size](const A *x) {
                if (algos::equal_n_m(x->name, x->name_size, charset_name,
                                     charset_name_size)) {
                        return x->charset;
                }
                return Charset_UNSUPPORTED;
        };

        return algos::apply_reduce(begin(known_charsets), end(known_charsets),
                                   charset_with_name, algos::maximum<Charset>(),
                                   Charset_UNSUPPORTED);
#undef LITE
}

zw_internal uint8_t ascii_tolower(uint8_t x)
{
        if (x >= 0x41 && x <= 0x5a) {
                return 0x61 + (x - 0x41);
        }

        return x;
}

struct EncodedWord {
        Charset charset;
        uint8_t const *bytes;
        size_t bytes_count;
};

zw_internal EncodedWord pack_encoded_word(HParsedToken const *token)
{
        EncodedWord result{};

        zw_assert(RFC2047TokenIs(token, ENCODED_WORD), "unexpected token type");

        if (3 == token->seq->used) {
                return result; // return null result for unknown encoding types
        }

        zw_assert(2 == token->seq->used, "unexpected token count");

        auto first_token = token->seq->elements;

        auto charset_token = algos::source(first_token);
        zw_assert(RFC2047TokenIs(charset_token, CHARSET), "unexpected token");

        auto bytes_token = algos::source(algos::successor(first_token));
        zw_assert(bytes_token->token_type == TT_BYTES, "unexpected token");

        result.charset = parse_charset_name_n(charset_token->bytes.token,
                                              charset_token->bytes.len);
        result.bytes = bytes_token->bytes.token;
        result.bytes_count = bytes_token->bytes.len;

        return result;
}

} // anonymous namespace

HAMMER_ACTION(act_charset)
{
        // NOTE(nicolas): charset names are case independent they come
        // from the IANA database.
        auto result = uuh_make_tagged_bytes_from_sequence(
            p->ast, RFC2047HammerTT(CHARSET), p->arena);

        // casting away the constness to apply a lower case function to array
        algos::for_each_n(const_cast<uint8_t *>(result->bytes.token),
                          result->bytes.len,
                          [](uint8_t &x) { x = ascii_tolower(x); });

        return result;
}

HAMMER_ACTION(act_encoded_text)
{
        return uuh_act_bytes_from_sequence(p, user_data);
}

// # 4.2 The Q Encoding
// Note in particular the _ character which has to be converted to byte 0x20
// (i.e. not space in the charset, but an actual 0x20)
HAMMER_ACTION(act_qp_underscore) { return H_MAKE_UINT(0x20); }

HAMMER_ACTION(act_qp_encoded_text)
{
        return uuh_act_bytes_from_sequence(p, user_data);
}

HAMMER_ACTION(act_encoded_word)
{
        return uuh_flatten_sequence(p, RFC2047HammerTT(ENCODED_WORD),
                                    user_data);
}

zw_internal RFC2047 global_rfc2047_parsers;

const RFC2047 &make_rfc2047(const ABNF_RFC5234 &abnf,
                            const RFC2045 &rfc2045,
                            const Base64 &base64)
{
        // # 2. Syntax of encoded-words

        H_RULE(safe_char, h_butnot(abnf.VCHAR, h_ch('?')));
        H_ARULE(encoded_text, h_many1(safe_char));
        H_RULE(especials, UH_IN("()<>@,;:\"/[]?.="));
        H_RULE(token, h_many1(h_butnot(h_ch_range(0x00, 0xff),
                                       UH_ANY(abnf.SP, abnf.CTL, especials))));

        // NOTE(nicolas): this is RFC2045 ptext with safe_char modified with a
        // restriction.
        H_ARULE(qp_underscore, h_ch(0x5f));
        H_ARULE(qp_encoded_text,
                h_many1(UH_ANY(qp_underscore, rfc2045.lenient_hex_octet,
                               h_butnot(rfc2045.safe_char, h_ch('?')))));

        H_RULE(encoding, UH_ANY(h_ch('B'), h_ch('Q')));
        H_ARULE(charset, token);
        H_ARULE(encoded_word,
                h_middle(UH_TOKEN("=?"),
                         UH_SEQ(charset, h_ignore(h_ch('?')),
                                UH_ANY(h_right(UH_SEQ(h_ch('Q'), h_ch('?')),
                                               qp_encoded_text),
                                       h_right(UH_SEQ(h_ch('B'), h_ch('?')),
                                               base64.base64),
                                       UH_SEQ(encoding, h_ignore(h_ch('?')),
                                              encoded_text))),
                         UH_TOKEN("?=")));

        token_types_init(rfc2047_token_types);

        global_rfc2047_parsers.encoded_word = encoded_word;

#if ZWEI_SLOW
        trace_print("Testing RFC2047 parsers");
        CHECK_PARSER(charset, "ISO-8859-1");
        CHECK_PARSER(encoding, "Q");
        CHECK_PARSER(encoded_text, "a");
        CHECK_PARSER(qp_encoded_text, "_");
        CHECK_PARSER(qp_encoded_text, "Keith_Moore");
        CHECK_PARSER(qp_encoded_text, "lenient=cd");
        CHECK_PARSER(encoded_word, "=?ISO-8859-1?Q?a?=");
        CHECK_PARSER(encoded_word, "=?US-ASCII?Q?Keith_Moore?=");
        CHECK_PARSER(encoded_word,
                     "=?US-ASCII?B?bXkgZ2FyZGVuIGlzIHNvIHNwZWNpYWw=?=");
        CHECK_PARSER(encoded_word, "=?UTF-8?B?T2lhIFBhcmZ1bXMg4oCTICBETQ==?=");
        CHECK_PARSER(encoded_word,
                     "=?iso-8859-15?Q?Euro:_=A4,=A6,=A8,=B4,=B8,=BC,=BD,=BE?=");
        {
                uint8_t data[] =
                    "=?iso-8859-15?Q?Euro:_=A4,=A6,=A8,=B4,=B8,=BC,=BD,=BE?=";
                HParseResult *result =
                    h_parse(encoded_word, data, sizeof data - 1);
                size_t size = rfc2047_get_encoded_word_size(result->ast);
                uint8_t *str = reinterpret_cast<uint8_t *>(alloca(1 + size));
                char *str_last =
                    (char *)rfc2047_copy_encoded_word(result->ast, str);
                if (cstr_terminate(str_last, (char *)str + 1 + size)) {
                        printf("result: %s\n", str);
                }
        }
#endif

        return global_rfc2047_parsers;
}

/**
   @requires ValueType(I) = u8 ∧ ValueType(O) = u8
*/
template <InputIterator I, OutputIterator O>
O transcode_encoded_word_to_utf8_n(
    Charset charset,
    I first,
    typename algos::IteratorConcept<I>::difference_type n,
    O d_first)
{
        using algos::sink;
        using algos::source;
        using algos::successor;

        if (charset == Charset_US_ASCII || charset == Charset_UTF_8) {
                // NOTE(nil): in case we encounter a series of encoded words in
                // utf8 format,
                // they may have been split in the middle of an utf8 byte.
                // Therefore it is unwise to try validating the utf8 stream on a
                // word to word basis
                //
                // So we simply copy the bytes here.
                d_first = algos::copy_n(first, n, d_first);
        } else if (charset == Charset_ISO_8859_1) {
                while (n) {
                        uint8_t byte = source(first);
                        if (byte < 0x0080) {
                                sink(d_first) = byte;
                                d_first = successor(d_first);
                        } else /* NOTE(nicolas) < 0x7ff */ {
                                uint32_t mask = (1 << 6) - 1;
                                sink(d_first) = 0xc0 | ((byte >> 6) & mask);
                                d_first = successor(d_first);
                                sink(d_first) = 0x80 | ((byte >> 0) & mask);
                                d_first = successor(d_first);
                        }

                        first = successor(first);
                        --n;
                }
        } else if (charset == Charset_ISO_8859_15) {
                while (n) {
                        uint8_t byte = source(first);
                        uint32_t codepoint = 0;
                        if (byte == 0xA4) {
                                codepoint = 0x20AC;
                        } else if (byte == 0xA6) {
                                codepoint = 0x0160;
                        } else if (byte == 0xA8) {
                                codepoint = 0x0161;
                        } else if (byte == 0xB4) {
                                codepoint = 0x017D;
                        } else if (byte == 0xB8) {
                                codepoint = 0x017E;
                        } else if (byte == 0xBC) {
                                codepoint = 0x0152;
                        } else if (byte == 0xBD) {
                                codepoint = 0x0153;
                        } else if (byte == 0xBE) {
                                codepoint = 0x0178;
                        } else {
                                codepoint = byte;
                        }

#define MASK(a, bits) ((a) & ((1 << bits) - 1))

                        if (codepoint < 0x0080) {
                                sink(d_first) = MASK(codepoint, 7);
                                d_first = successor(d_first);
                        } else if (codepoint < 0x800) {
                                sink(d_first) = 0xc0 | MASK(codepoint >> 6, 5);
                                d_first = successor(d_first);
                                sink(d_first) = 0x80 | MASK(codepoint >> 0, 6);
                                d_first = successor(d_first);
                        } else if (codepoint < 0x10000) {
                                sink(d_first) = 0xE0 | MASK(codepoint >> 12, 4);
                                d_first = successor(d_first);
                                sink(d_first) = 0x80 | MASK(codepoint >> 6, 6);
                                d_first = successor(d_first);
                                sink(d_first) = 0x80 | MASK(codepoint >> 0, 6);
                                d_first = successor(d_first);
                        }
#undef MASK
                        first = successor(first);
                        --n;
                }
        } else {
                // don't know how to deal with this charset
                // TODO(nicolas) ISO-2022 for jp text
                // we should also have a reasonable fallback
        }

        return d_first;
}

/**
 * Given an encoded word, tell me if you can convert it to UTF-8, in
 * how many bytes and allow me to convert it.
 */
size_t rfc2047_get_encoded_word_size(HParsedToken const *token)
{
        auto encoded_word = pack_encoded_word(token);

        algos::VoidOutputIteratorAdapter<uint8_t *> d_first = {};

        return transcode_encoded_word_to_utf8_n(
                   encoded_word.charset, encoded_word.bytes,
                   encoded_word.bytes_count, d_first) -
               d_first;
}

/**
 * Given an encoded word, tell me if you can convert it to UTF-8, in
 * how many bytes and allow me to convert it.
 */
uint8_t *rfc2047_copy_encoded_word(HParsedToken const *token, uint8_t *d_first)
{
        auto encoded_word = pack_encoded_word(token);

        return transcode_encoded_word_to_utf8_n(
            encoded_word.charset, encoded_word.bytes, encoded_word.bytes_count,
            d_first);
}

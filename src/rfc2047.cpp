/**

   Representing 8-bit content in headers

   @tags "Encoded Word" "Quoted Printable" "Base64"

   [[https://tools.ietf.org/html/rfc2047.txt]]
 */

#include "base64.hpp"
#include "rfc2047.hpp"

#include "hammer_utils.hpp"
#include "output_iterators.hpp"
#include "rfc5234.hpp"
#include "zwei_logging.hpp"
#include "zwei_types.hpp"

#include "hammer.hpp"
#include <utility> // for pair

namespace
{
enum CharsetEnum {
        CHARSET_UNSUPPORTED = 0,
        US_ASCII = 3,
        ISO_8859_1 = 4,
        UTF_8 = 106,
} charset;

CharsetEnum parse_charset_name_n(uint8_t const *charset_name,
                                 size_t charset_name_size)
{
        std::pair<typename algos::IteratorConcept<char *>::difference_type,
                  typename algos::IteratorConcept<
                      uint8_t *>::difference_type> const no_mismatch{};

#define LITE(lit) lit, sizeof(lit) - 1

        struct A {
                char const *name;
                size_t name_size;
                CharsetEnum charset;
        } assoc_table[] = {
            {LITE("us-ascii"), US_ASCII},
            {LITE("iso-8859-1"), ISO_8859_1},
            {LITE("utf-8"), UTF_8},
        };

        using algos::begin;
        using algos::end;

        auto entry_if_equal =
            [&no_mismatch, charset_name, charset_name_size](const A *x) {
                    if (no_mismatch == algos::find_mismatch_n_m(
                                           x->name, x->name_size, charset_name,
                                           charset_name_size)) {
                            return x->charset;
                    }

                    return CHARSET_UNSUPPORTED;
            };

        return algos::reduce(begin(assoc_table), end(assoc_table),
                             algos::maximum<CharsetEnum>(), entry_if_equal,
                             CHARSET_UNSUPPORTED);
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
        CharsetEnum charset;
        uint8_t const *bytes;
        size_t bytes_count;
};

EncodedWord pack_encoded_word(HParsedToken const *token)
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
        auto result = uuh_uint8_sequence_to_tagged_bytes(
            p->ast, RFC2047HammerTT(CHARSET), p->arena);

        // casting away the constness to apply a lower case function to array
        algos::for_each_n(const_cast<uint8_t *>(result->bytes.token),
                          result->bytes.len,
                          [](uint8_t &x) { x = ascii_tolower(x); });

        return result;
}

HAMMER_ACTION(act_encoded_text)
{
        return uuh_uint8_sequence_to_tagged_bytes(p->ast, TT_BYTES, p->arena);
}

// # 4.2 The Q Encoding
// Note in particular the _ character which has to be converted to byte 0x20
// (i.e. not space in the charset, but an actual 0x20)
HAMMER_ACTION(act_qp_underscore) { return H_MAKE_UINT(0x20); }

HAMMER_ACTION(act_qp_or_base64)
{
        return uuh_uint8_sequence_to_tagged_bytes(p->ast, TT_BYTES, p->arena);
}

HAMMER_ACTION(act_encoded_word)
{
        return uuh_flatten_sequence(p, RFC2047HammerTT(ENCODED_WORD),
                                    user_data);
}

zw_internal RFC2047 global_rfc2047_parsers;

const RFC2047 &make_rfc2047(const RFC5234 &rfc5234,
                            const RFC2045 &rfc2045,
                            const Base64 &base64)
{
        // # 2. Syntax of encoded-words

        H_RULE(safe_char, h_butnot(rfc5234.VCHAR, h_ch('?')));
        H_ARULE(encoded_text, h_many1(safe_char));
        H_RULE(especials, UH_IN("()<>@,;:\"/[]?.="));
        H_RULE(token,
               h_many1(h_butnot(h_ch_range(0x00, 0xff),
                                UH_ANY(rfc5234.SP, rfc5234.CTL, especials))));

        // NOTE(nicolas): this is RFC2045 ptext with safe_char modified with a
        // restriction.
        H_ARULE(qp_underscore, h_ch(0x5f));
        H_RULE(qp_encoded_text,
               h_many1(UH_ANY(qp_underscore, rfc2045.lenient_hex_octet,
                              h_butnot(rfc2045.safe_char, h_ch('?')))));

        H_RULE(encoding, UH_ANY(h_ch('B'), h_ch('Q')));
        H_ARULE(charset, token);
        H_ARULE(encoded_word,
                h_middle(
                    UH_TOKEN("=?"),
                    UH_SEQ(charset, h_ignore(h_ch('?')),
                           UH_ANY(h_action(h_right(UH_SEQ(h_ch('Q'), h_ch('?')),
                                                   qp_encoded_text),
                                           act_qp_or_base64, NULL),
                                  h_action(h_right(UH_SEQ(h_ch('B'), h_ch('?')),
                                                   base64.base64),
                                           act_qp_or_base64, NULL),
                                  UH_SEQ(encoding, h_ignore(h_ch('?')),
                                         encoded_text))),
                    UH_TOKEN("?=")));

        allocate_token_types(algos::begin(rfc2047_token_types),
                             algos::end(rfc2047_token_types));

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
#endif

        return global_rfc2047_parsers;
}

/**
   @requires ValueType(I) = u8 ∧ ValueType(O) = u8
*/
template <InputIterator I, OutputIterator O>
O transcode_to_utf8_n(CharsetEnum charset,
                      I first,
                      typename algos::IteratorConcept<I>::difference_type n,
                      O d_first)
{
        using algos::sink;
        using algos::source;
        using algos::successor;

        if (charset == US_ASCII || charset == UTF_8) {
                // TODO(nicolas) shouldnt we attempt to validate the utf-8 input
                // stream?
                return algos::copy_n(first, n, d_first);
        } else if (charset == ISO_8859_1) {
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

        } else {
                // don't know how to deal with this charset
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

        algos::NullOutputIteratorAdapter<uint8_t *> d_first = {};

        return transcode_to_utf8_n(encoded_word.charset, encoded_word.bytes,
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

        return transcode_to_utf8_n(encoded_word.charset, encoded_word.bytes,
                                   encoded_word.bytes_count, d_first);
}

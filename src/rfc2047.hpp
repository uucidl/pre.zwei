#pragma once

#include "hammer_utils.hpp"

extern "C" {
#include "../builds/include/hammer/glue.h"
#include "../builds/include/hammer/hammer.h"
}

struct Base64;
struct RFC2045;
struct RFC5234;

enum RFC2047TokenType {
        RFC2047_TT_ENCODED_WORD,
        RFC2047_TT_CHARSET,
        RFC2047_TT_LAST,
};

#define RFC2047HammerTT(type_name)                                             \
        rfc2047_token_types[RFC2047_TT_##type_name].registered_type
#define RFC2047TokenIs(token, type_name)                                       \
        (token)->token_type == RFC2047HammerTT(type_name)

zw_global UserTokenTypeEntry rfc2047_token_types[RFC2047_TT_LAST] = {
    {TT_SEQUENCE, "encoded-word", TT_INVALID},
    {TT_BYTES, "charset", TT_INVALID},
};

struct RFC2047 {
        HParser *encoded_word;
};

const RFC2047 &make_rfc2047(const RFC5234 &rfc5234,
                            const RFC2045 &rfc2045,
                            const Base64 &base64);

/**
   Get size in bytes necessary to store the token as UTF-8
*/
size_t rfc2047_get_encoded_word_size(HParsedToken const *token);

/**
   Copy UTF-8 bytes from the encoded word into d_first.

   @pre there is at least `rfc2047_get_encoded_word_size(token)` at `d_first`
 */
uint8_t *rfc2047_copy_encoded_word(HParsedToken const *token, uint8_t *d_first);

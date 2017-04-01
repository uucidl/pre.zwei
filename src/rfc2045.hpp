#pragma once

#include "zwei_inlines.hpp"

#include "hammer_fwd.hpp"
#include "hammer_utils.hpp"
#include "mail_types.hpp"

struct ABNF_RFC5234;
struct RFC5322_Base;

struct RFC2045 {
        HParser *safe_char;
        HParser *hex_octet;
        HParser *lenient_hex_octet;
        HParser *quoted_printable;
        HParser *content_type_header_field;
        HParser *content_transfer_encoding_header_field;
};

enum RFC2045TokenType {
        RFC2045_TT_CONTENT_TRANSFER_ENCODING_FIELD,
        RFC2045_TT_LAST,
};

zw_internal UserTokenTypeEntry rfc2045_token_types[RFC2045_TT_LAST] = {
    {TT_UINT, "content-transfer-encoding", TT_INVALID},
};

#define RFC2045HammerTT(type_name)                                             \
        rfc2045_token_types[RFC2045_TT_##type_name].registered_type
#define RFC2045TokenIs(token, type_name)                                       \
        (token)->token_type == RFC2045HammerTT(type_name)

const RFC2045 &make_rfc2045(const ABNF_RFC5234 &abnf,
                            const RFC5322_Base &rfc5322_base);

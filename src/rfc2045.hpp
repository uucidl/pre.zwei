#pragma once

#include "zwei_inlines.hpp"

#include "hammer_fwd.hpp"
#include "rfc5234.hpp"

struct RFC2045 {
        HParser *safe_char;
        HParser *hex_octet;
        HParser *lenient_hex_octet;
        HParser *quoted_printable;
};

const RFC2045 &make_rfc2045(const RFC5234 &rfc5234);

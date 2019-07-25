#pragma once

#include "hammer_fwd.hpp"
#include "zwei_inlines.hpp"

struct ABNF_RFC5234;

struct Base64 {
        HParser *base64;
};

zw_internal Base64 const &make_base64(ABNF_RFC5234 const &abnf_rfc5234);

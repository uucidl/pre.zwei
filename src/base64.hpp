#pragma once

#include "hammer_fwd.hpp"
#include "rfc5234.hpp"

struct Base64 {
        HParser *base64;
};

zw_internal Base64 const &make_base64(RFC5234 const &rfc5234);

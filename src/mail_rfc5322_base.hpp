#pragma once

#include "hammer_fwd.hpp"
#include "zwei_inlines.hpp"

struct ABNF_RFC5234;

struct RFC5322_Base {
        HParser *FWS;
        HParser *CFWS;
        HParser *quoted_string;
};

zw_internal const RFC5322_Base &
make_rfc5322_base(const ABNF_RFC5234 &abnf_rfc5234);

#pragma once

// TODO(nicolas) NEXT modularize and rename

#include "hammer_fwd.hpp"

struct ABNF_RFC5234 {
        HParser *ALPHA;
        HParser *BIT;
        HParser *CHAR;
        HParser *CRLF;
        HParser *CTL;
        HParser *DIGIT;
        HParser *DQUOTE;
        HParser *HEXDIG;
        HParser *LWSP;
        HParser *OCTET;
        HParser *SP;
        HParser *VCHAR;
        HParser *WSP;
};

const ABNF_RFC5234 &make_abnf_rfc5234();

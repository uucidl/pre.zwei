#include "abnf_rfc5234.hpp"
#include "hammer_utils.hpp"

#include "hammer.hpp"

zw_internal ABNF_RFC5234 global_rfc5234;

const ABNF_RFC5234 &make_abnf_rfc5234()
{
        // NOTE(nicolas): RFC5234 tokens. Most comments come from the
        // RFC.

        // <RFC5234...
        H_RULE(ALPHA, UH_ANY(h_ch_range(0x41, 0x5a), h_ch_range(0x61, 0x7a)));

        H_RULE(BIT, UH_ANY(h_ch('0'), h_ch('1')));

        // any 7-bit US-ASCII character, excluding NUL
        H_RULE(CHAR, h_ch_range(0x01, 0x7f));

        H_RULE(CR, h_ch(0x0D)); // carriage return

        H_RULE(LF, h_ch(0x0A)); // linefeed

        H_RULE(CRLF, UH_SEQ(CR, LF)); // Internet standard newline

        // controls
        H_RULE(CTL, UH_ANY(h_ch_range(0x00, 0x1F), h_ch(0x7F)));

        H_RULE(DIGIT, h_ch_range(0x30, 0x39)); // 0-9

        H_RULE(DQUOTE, h_ch(0x22)); //  " (Double Quote)

        H_RULE(HEXDIG, UH_ANY(DIGIT, h_ch_range(0x41, 0x46)));

        H_RULE(HTAB, h_ch(0x09)); // horizontal tab

        H_RULE(SP, h_ch(0x20));

        H_RULE(WSP, UH_ANY(SP, HTAB)); // white space

        /*
          Use of this linear-white-space rule
          permits lines containing only white
          space that are no longer legal in
          mail headers and have caused
          interoperability problems in other
          contexts.

          Do not use when defining mail
          headers and use with caution in
          other contexts.
        */

        H_RULE(LWSP, h_many(UH_ANY(WSP, UH_SEQ(CRLF, WSP))));

        H_RULE(OCTET, h_ch_range(0x00, 0xFF)); // 8 bits of data

        H_RULE(VCHAR, h_ch_range(0x21, 0x7E)); // visible (printing) characters

        // ...RFC5234>

        global_rfc5234.ALPHA = ALPHA;
        global_rfc5234.BIT = BIT;
        global_rfc5234.CHAR = CHAR;
        global_rfc5234.CRLF = CRLF;
        global_rfc5234.CTL = CTL;
        global_rfc5234.DIGIT = DIGIT;
        global_rfc5234.DQUOTE = DQUOTE;
        global_rfc5234.HEXDIG = HEXDIG;
        global_rfc5234.LWSP = LWSP;
        global_rfc5234.OCTET = OCTET;
        global_rfc5234.SP = SP;
        global_rfc5234.VCHAR = VCHAR;
        global_rfc5234.WSP = WSP;

        return global_rfc5234;
}

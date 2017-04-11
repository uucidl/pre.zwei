#include "mail_rfc5322_base.hpp"

zw_internal RFC5322_Base rfc5322_base_parsers;

HAMMER_ACTION(act_comment)
{
        // NOTE(nicolas): comments are ignored and source-supplied, so of
        // doubtful value. Let's ignore them.
        return h_act_ignore(p, user_data);
}

zw_internal const RFC5322_Base &make_rfc5322_base(const ABNF_RFC5234 &abnf)
{
        // ## 3.2.2.  Folding White Space and Comments
        H_RULE(quoted_pair, h_right(h_ch('\\'), UH_ANY(abnf.VCHAR, abnf.WSP)));

        // Folding white space
        H_RULE(FWS, UH_ANY(UH_SEQ(h_many(abnf.WSP), h_ignore(abnf.CRLF),
                                  h_many1(abnf.WSP)),
                           h_many1(abnf.WSP)));

        // printable US-ASCII characters not including (, ), backslash
        H_RULE(ctext, UH_ANY(h_ch_range(33, 39), h_ch_range(42, 91),
                             h_ch_range(93, 126)));

        HParser *comment = h_indirect();

        H_RULE(ccontent, UH_ANY(ctext, quoted_pair, comment));

        h_bind_indirect(
            comment, h_action(UH_SEQ(h_ch('('),
                                     h_many(UH_SEQ(h_optional(FWS), ccontent)),
                                     h_optional(FWS), h_ch(')')),
                              act_comment, NULL));

        H_RULE(CFWS, UH_ANY(UH_SEQ(h_many1(UH_SEQ(h_optional(FWS), comment)),
                                   h_optional(FWS)),
                            FWS));

        // ## 3.2.4.  Quoted Strings

        H_RULE(qtext,
               UH_ANY(h_ch(33), h_ch_range(35, 91), h_ch_range(93, 126)));

        H_RULE(qcontent, UH_ANY(qtext, quoted_pair));

        H_RULE(
            quoted_string,
            h_middle(h_optional(CFWS),
                     h_middle(abnf.DQUOTE,
                              UH_SEQ(h_many(UH_SEQ(h_optional(FWS), qcontent)),
                                     h_optional(FWS)),
                              abnf.DQUOTE),
                     h_optional(CFWS)));

        rfc5322_base_parsers.FWS = FWS;
        rfc5322_base_parsers.CFWS = CFWS;
        rfc5322_base_parsers.quoted_string = quoted_string;

        return rfc5322_base_parsers;
}

#include "rfc5322.hpp"

#include "abnf_rfc5234.hpp"
#include "algos.hpp"
#include "date_time_types.hpp"
#include "hammer_defines.hpp"
#include "hammer_iterators.hpp"
#include "hammer_utils.hpp"
#include "mail_rfc5322_base.hpp"
#include "rfc2047.hpp"

#include "../modules/uu.spdr/include/spdr/spdr.hh"
#include "hammer.hpp"
#include <cinttypes>
#include <functional>

/**
  @file

  Implementation of RFC5322 (mail) in Hammer

  [[https://tools.ietf.org/rfc/rfc5234.txt]] describes ABNF

  [[https://tools.ietf.org/rfc/rfc5321.txt]] describes tracing. This
  header is of extreme value for forensics. Potentially for spam
  management as well, because it's one of the few in email that is
  actually partly authoritative.

 */

zw_internal HParsedToken *act_tag_sequence(const HParseResult *p,
                                           RFC5322TokenType token_type,
                                           void *user_data)
{
        return uuh_flatten_sequence(
            p, rfc5322_token_types[token_type].registered_type, user_data);
}

zw_internal HParsedToken *act_flatten_bytes_tagged(
    const HParseResult *p, RFC5322TokenType rfc5322_token_type, void *user_data)
{
        HParsedToken *content = h_act_flatten(p, user_data);

        HTokenType const token_type =
            rfc5322_token_types[rfc5322_token_type].registered_type;
        return uuh_make_tagged_bytes_from_sequence(content, token_type,
                                                   p->arena);
}

// NOTE(nicolas) basic action for unstructured field content
HAMMER_ACTION(act_unstructured)
{
        using algos::begin;
        using algos::end;
        using algos::sink;
        using algos::successor;

        auto flat_sequence = h_act_flatten(p, user_data);
        size_t byte_count = 0;
        auto content_first = begin(flat_sequence);
        auto content_last = end(flat_sequence);
        algos::for_each(
            content_first, content_last, [&byte_count](const HParsedToken *x) {
                    if (x->token_type == TT_UINT && x->uint < 256) {
                            ++byte_count;
                    } else if (RFC2047TokenIs(x, ENCODED_WORD)) {
                            byte_count += rfc2047_get_encoded_word_size(x);
                    }
            });
        uint8_t *bytes = (uint8_t *)h_arena_malloc(p->arena, byte_count);
        auto bytes_last = bytes;
        algos::for_each(
            content_first, content_last, [&bytes_last](const HParsedToken *x) {
                    if (x->token_type == TT_UINT && x->uint < 256) {
                            sink(bytes_last) = x->uint;
                            bytes_last = successor(bytes_last);
                    } else if (RFC2047TokenIs(x, ENCODED_WORD)) {
                            bytes_last =
                                rfc2047_copy_encoded_word(x, bytes_last);
                    }
            });
        byte_count = bytes_last - bytes;
        {
                uint32_t decoder_state = Utf8DecodeResult_Accept;
                uint32_t codepoint = 0;
                uint8_t last_byte = 0; // useful for debugging
                bool can_decode_all =
                    algos::all_n(bytes, byte_count, [&codepoint, &decoder_state,
                                                     &last_byte](uint8_t x) {
                            last_byte = x;
                            return Utf8DecodeResult_Reject !=
                                   utf8_decode(&decoder_state, &codepoint, x);
                    });
                zw_assert(can_decode_all, "data isn't valid utf-8");
        }
        HParsedToken *result = h_make_bytes(p->arena, bytes, byte_count);
        result->token_type = RFC5322HammerTT(UNSTRUCTURED);
        return result;
}

HAMMER_ACTION(act_word)
{
        return act_flatten_bytes_tagged(p, RFC5322_TT_WORD, user_data);
}

HAMMER_ACTION(act_msg_id)
{
        return act_flatten_bytes_tagged(p, RFC5322_TT_MSG_ID, user_data);
}

HAMMER_ACTION(act_local_part)
{
        return act_flatten_bytes_tagged(p, RFC5322_TT_LOCAL_PART, user_data);
}

HAMMER_ACTION(act_domain)
{
        return act_flatten_bytes_tagged(p, RFC5322_TT_DOMAIN, user_data);
}

HAMMER_ACTION(act_display_name)
{
        using algos::sink;
        using algos::source;
        using algos::successor;

        auto *ast = p->ast;
        size_t bytes_count = 0;

        auto count_bytes = [&bytes_count](HParsedToken const *token) {
                if (RFC5322TokenIs(token, WORD)) {
                        bytes_count += token->bytes.len;
                } else if (RFC2047TokenIs(token, ENCODED_WORD)) {
                        bytes_count += rfc2047_get_encoded_word_size(token);
                }
                bytes_count += 1; // separator
        };

        algos::traverse_each(rfc5322_top(ast), std::cref(count_bytes));
        uint8_t *bytes = (uint8_t *)h_arena_malloc(p->arena, bytes_count);
        uint8_t *bytes_last = bytes;
        bool is_first = true;

        auto copy_bytes = [&bytes_last, &is_first](HParsedToken const *token) {
                if (!is_first) {
                        sink(bytes_last) = ' ';
                        bytes_last = successor(bytes_last);
                }
                is_first = false;
                if (RFC5322TokenIs(token, WORD)) {
                        bytes_last = algos::copy_n(
                            token->bytes.token, token->bytes.len, bytes_last);
                } else if (RFC2047TokenIs(token, ENCODED_WORD)) {
                        bytes_last =
                            rfc2047_copy_encoded_word(token, bytes_last);
                }
        };
        algos::traverse_each(rfc5322_top(ast), std::cref(copy_bytes));

        HParsedToken *result =
            h_make_bytes(p->arena, bytes, bytes_last - bytes);
        result->token_type =
            rfc5322_token_types[RFC5322_TT_DISPLAY_NAME].registered_type;
        return result;
}

HAMMER_ACTION(act_mailbox)
{
        return act_tag_sequence(p, RFC5322_TT_MAILBOX, user_data);
}

HAMMER_ACTION(act_group)
{
        return act_tag_sequence(p, RFC5322_TT_GROUP, user_data);
}

HAMMER_ACTION(act_orig_date)
{
        return act_tag_sequence(p, RFC5322_TT_ORIG_DATE_FIELD, user_data);
}

HAMMER_ACTION(act_from)
{
        return act_tag_sequence(p, RFC5322_TT_FROM_FIELD, user_data);
}

HAMMER_ACTION(act_sender)
{
        return act_tag_sequence(p, RFC5322_TT_SENDER_FIELD, user_data);
}

HAMMER_ACTION(act_reply_to)
{
        return act_tag_sequence(p, RFC5322_TT_REPLY_TO_FIELD, user_data);
}

HAMMER_ACTION(act_to)
{
        return act_tag_sequence(p, RFC5322_TT_TO_FIELD, user_data);
}

HAMMER_ACTION(act_cc)
{
        return act_tag_sequence(p, RFC5322_TT_CC_FIELD, user_data);
}

HAMMER_ACTION(act_bcc)
{
        return act_tag_sequence(p, RFC5322_TT_CC_FIELD, user_data);
}

HAMMER_ACTION(act_message_id)
{
        return act_tag_sequence(p, RFC5322_TT_MESSAGE_ID_FIELD, user_data);
}

HAMMER_ACTION(act_in_reply_to)
{
        return act_tag_sequence(p, RFC5322_TT_IN_REPLY_TO_FIELD, user_data);
}

HAMMER_ACTION(act_references)
{
        return act_tag_sequence(p, RFC5322_TT_REFERENCES_FIELD, user_data);
}

HAMMER_ACTION(act_subject)
{
        return act_tag_sequence(p, RFC5322_TT_SUBJECT_FIELD, user_data);
}

HAMMER_ACTION(act_comments)
{
        return act_tag_sequence(p, RFC5322_TT_COMMENTS_FIELD, user_data);
}

HAMMER_ACTION(act_keywords)
{
        return act_tag_sequence(p, RFC5322_TT_KEYWORDS_FIELD, user_data);
}

HAMMER_ACTION(act_field_name)
{
        return act_flatten_bytes_tagged(p, RFC5322_TT_OPTIONAL_FIELD_NAME,
                                        user_data);
}

HAMMER_ACTION(act_optional_field)
{
        return act_tag_sequence(p, RFC5322_TT_OPTIONAL_FIELD, user_data);
}

zw_internal uint8_t ascii_digit10_to_uint(uint8_t val)
{
        zw_assert(val >= 0x30 && val < 0x40, "digit out of range");
        return val - 0x30;
}

zw_internal uint64_t accumulate_base10_digit(HParsedToken const *ast)
{
        uint64_t result = 0;
        auto accumulate_uint = [&result](HParsedToken const &token) {
                if (token.token_type == TT_SEQUENCE) {
                        return;
                }
                if (token.token_type == TT_NONE) {
                        return;
                }
                result *= 10;
                result += ascii_digit10_to_uint(H_CAST_UINT((&token)));
        };
        algos::traverse_each(ast, std::cref(accumulate_uint));
        return result;
}

HAMMER_ACTION(act_month)
{
        zw_assert(p->ast->token_type == TT_UINT, "unexpected token");
        return H_MAKE_UINT(1 + p->ast->uint); // we expect an enum value
}

HAMMER_ACTION(act_accumulate_base10)
{
        return H_MAKE_UINT(accumulate_base10_digit(p->ast));
}

HAMMER_ACTION(act_zone)
{
        using algos::begin;
        using algos::end;

        HParsedToken *result = H_MAKE_SEQN(2);

        HParsedToken const *flat_seq = h_seq_flatten(p->arena, p->ast);
        auto first = begin(flat_seq);
        auto const last = end(flat_seq);

        {
                int64_t hour_offset = 0;
                auto sign = H_CAST_UINT(algos::source(first));
                first = algos::successor(first);
                zw_assert(last - first >= 2, "unexpected length");
                first = algos::for_each_n(
                    first, 2, [&hour_offset](const HParsedToken *token) {
                            hour_offset *= 10;
                            hour_offset +=
                                ascii_digit10_to_uint(H_CAST_UINT(token));
                    });
                if (sign == '-') {
                        hour_offset *= -1;
                }
                zw_assert(last - first >= 2, "unexpected length");
                h_seq_snoc(result, H_MAKE_SINT(hour_offset));
        }

        {
                uint64_t minute_offset = 0;
                algos::for_each(
                    first, last, [&minute_offset](const HParsedToken *token) {
                            minute_offset *= 10;
                            minute_offset +=
                                ascii_digit10_to_uint(H_CAST_UINT(token));
                    });
                h_seq_snoc(result, H_MAKE_UINT(minute_offset));
        }

        return result;
}

HAMMER_ACTION(act_date_time)
{
        using algos::begin;
        using algos::end;
        using algos::successor;
        using algos::source;

        auto first = begin(p->ast);
        auto const last = end(p->ast);
        if (last - first > 2) {
                first = successor(first); // skip day_of_week
        }

        zw_assert(last - first == 2, "unexpected number of tokens");

        CivilDateTime result;

        auto consume_uint = [](HParsedToken const **&pos) {
                auto value = H_CAST_UINT(source(pos));
                pos = successor(pos);
                return value;
        };

        auto consume_sint = [](HParsedToken const **&pos) {
                auto value = H_CAST_SINT(source(pos));
                pos = successor(pos);
                return value;
        };

        // process date
        {
                auto date_first = begin(source(first));
                auto date_last = end(source(first));
                zw_assert(date_last - date_first == 3,
                          "unexpected number of tokens");
                result.day_count = consume_uint(date_first);
                result.month_count = consume_uint(date_first);
                result.year = consume_uint(date_first);
                zw_assert(date_first == date_last,
                          "unexpected number of tokens");
        }
        first = successor(first);

        // process time+zone
        {
                auto time_zone_first = begin(source(first));
                auto time_zone_last = end(source(first));
                {
                        auto time_first = begin(source(time_zone_first));
                        auto const time_last = end(source(time_zone_first));

                        result.hour = consume_uint(time_first);
                        result.minute = consume_uint(time_first);
                        if (time_first != time_last) {
                                if (source(time_first)->token_type == TT_NONE) {
                                        time_first = successor(time_first);
                                } else {
                                        result.seconds =
                                            consume_uint(time_first);
                                }
                        } else {
                                result.seconds = 0;
                        }
                        zw_assert(time_first == time_last,
                                  "unexpected token count");

                        time_zone_first = successor(time_zone_first);
                }
                {
                        auto zone_first = begin(source(time_zone_first));
                        auto const zone_last = end(source(time_zone_first));

                        result.zone_hour_offset = consume_sint(zone_first);
                        result.zone_minute_offset = consume_uint(zone_first);
                        zw_assert(zone_first == zone_last,
                                  "unexpected token count");

                        time_zone_first = successor(time_zone_first);
                }
                zw_assert(time_zone_first == time_zone_last,
                          "unexpected token count");
        }
        first = successor(first);

        zw_assert(first == last, "unexpected number of tokens");

        CivilDateTime *result_value = H_ALLOC(CivilDateTime);
        *result_value = result;

        return h_make(p->arena, RFC5322HammerTT(DATE_TIME), result_value);
}

zw_internal RFC5322 rfc5322_parsers;

zw_internal const RFC5322 &make_rfc5322(const ABNF_RFC5234 &abnf,
                                        const RFC5322_Base &rfc5322_base,
                                        const RFC2047 &rfc2047,
                                        const RFC2045 &rfc2045)
{
        // <RFC5322...
        auto FWS = rfc5322_base.FWS;
        auto CFWS = rfc5322_base.CFWS;
        auto quoted_string = rfc5322_base.quoted_string;

        // ## 3.2.3.  Atom

        // Printable US-ASCII not including specials, used for atoms.
        H_RULE(atext,
               UH_ANY(abnf.ALPHA, abnf.DIGIT, UH_IN("!#$%&'*+-/=?^_`{|}~")));

        H_RULE(atom,
               h_middle(h_optional(CFWS), h_many1(atext), h_optional(CFWS)));

        H_RULE(
            dot_atom_text,
            UH_SEQ(h_many1(atext), h_many(UH_SEQ(h_ch('.'), h_many1(atext)))));

        H_RULE(dot_atom,
               h_middle(h_optional(CFWS), dot_atom_text, h_optional(CFWS)));

#if ZWEI_DISABLED
        // Special characters that do not appear in atext
        H_RULE(specials, UH_ANY(UH_IN("()<>[]:;@\\,."), abnf.DQUOTE));
#endif

        // ## 3.2.5.  Miscellaneous Tokens

        H_ARULE(word, UH_ANY(atom, quoted_string));

        H_RULE(phrase,
               h_many1(UH_ANY(h_middle(h_optional(CFWS), rfc2047.encoded_word,
                                       h_optional(CFWS)),
                              word)));

        // NOTE(nicolas): from RFC2047:
        // (1) An 'encoded-word' may replace a 'text' token (as defined by RFC
        // 822)
        //     in any Subject or Comments header field, any extension message
        //     header field, or any MIME body part field for which the field
        //     body
        //     is defined as '*text'.  An 'encoded-word' may also appear in any
        //     user-defined ("X-") message or body part header field.
        //
        //     Ordinary ASCII text and 'encoded-word's may appear together in
        //     the
        //     same header field.  However, an 'encoded-word' that appears in a
        //     header field defined as '*text' MUST be separated from any
        //     adjacent
        //     'encoded-word' or 'text' by 'linear-white-space'.
        H_ARULE(
            unstructured,
            UH_SEQ(h_many(UH_SEQ(h_optional(FWS),
                                 UH_ANY(UH_SEQ(rfc2047.encoded_word,
                                               h_many(h_right(
                                                   FWS, rfc2047.encoded_word))),
                                        abnf.VCHAR))),
                   h_many(abnf.WSP)));

        // ## 3.3.  Date and Time Specification
        H_RULE(day_name, UH_ANY(UH_IENUM("Mon", 0), UH_IENUM("Tue", 1),
                                UH_IENUM("Wed", 2), UH_IENUM("Thu", 3),
                                UH_IENUM("Fri", 4), UH_IENUM("Sat", 5),
                                UH_IENUM("Sun", 6)));

        H_RULE(day_of_week, h_right(h_optional(FWS), day_name));

        H_RULE(
            day,
            h_action(h_middle(h_optional(FWS),
                              UH_SEQ(abnf.DIGIT, h_optional(abnf.DIGIT)), FWS),
                     act_accumulate_base10, NULL));

        H_ARULE(month, UH_ANY(UH_IENUM("Jan", 0), UH_IENUM("Feb", 1),
                              UH_IENUM("Mar", 2), UH_IENUM("Apr", 3),
                              UH_IENUM("May", 4), UH_IENUM("Jun", 5),
                              UH_IENUM("Jul", 6), UH_IENUM("Aug", 7),
                              UH_IENUM("Sep", 8), UH_IENUM("Oct", 9),
                              UH_IENUM("Nov", 10), UH_IENUM("Dec", 11)));

        H_RULE(year, h_action(h_middle(FWS, UH_SEQ(h_repeat_n(abnf.DIGIT, 4),
                                                   h_many(abnf.DIGIT)),
                                       FWS),
                              act_accumulate_base10, NULL));

        H_RULE(date, UH_SEQ(day, month, year));

        H_RULE(hour, h_action(h_repeat_n(abnf.DIGIT, 2), act_accumulate_base10,
                              NULL));
        H_RULE(minute, h_action(h_repeat_n(abnf.DIGIT, 2),
                                act_accumulate_base10, NULL));
        H_RULE(second, h_action(h_repeat_n(abnf.DIGIT, 2),
                                act_accumulate_base10, NULL));

        H_RULE(time_of_day, UH_SEQ(hour, h_right(h_ch(':'), minute),
                                   h_optional(h_right(h_ch(':'), second))));

        H_ARULE(zone,
                h_right(FWS, UH_SEQ(UH_IN("+-"), h_repeat_n(abnf.DIGIT, 4))));

        H_RULE(time, UH_SEQ(time_of_day, zone));

        H_ARULE(date_time,
                h_left(UH_SEQ(h_optional(h_left(day_of_week, h_ch(','))), date,
                              time),
                       h_optional(CFWS)));

        // ## 3.4.  Address Specification

        H_ARULE(display_name, phrase);

        H_ARULE(local_part, UH_ANY(dot_atom, quoted_string));

        // Printable US-ASCII characters not including [, ] and backslash
        H_RULE(dtext, UH_ANY(h_ch_range(33, 90), h_ch_range(94, 126)));

        H_RULE(domain_litteral,
               h_middle(UH_SEQ(h_optional(CFWS), h_ch('[')),
                        h_many(h_right(h_optional(FWS), dtext)),
                        UH_SEQ(h_ch(']'), h_optional(CFWS))));

        H_ARULE(domain, UH_ANY(dot_atom, domain_litteral));

        H_RULE(addr_spec, UH_SEQ(local_part, h_ch('@'), domain));

        H_RULE(angle_addr, h_middle(h_optional(CFWS),
                                    UH_SEQ(h_ch('<'), addr_spec, h_ch('>')),
                                    h_optional(CFWS)));

        H_RULE(name_addr, UH_SEQ(h_optional(display_name), angle_addr));

        H_ARULE(mailbox, UH_ANY(name_addr, addr_spec));

        H_RULE(mailbox_list, h_sepBy1(mailbox, h_ch(',')));

        H_RULE(group_list, UH_ANY(mailbox_list, CFWS));

        H_ARULE(group, h_left(UH_SEQ(display_name, h_ch(':'),
                                     h_optional(group_list), h_ch(';')),
                              h_optional(CFWS)));

        H_RULE(address, UH_ANY(mailbox, group));

        H_RULE(address_list, h_sepBy1(address, h_ch(',')));

// ## 3.6.  Field Definitions

#define UUH_FIELD(lit, right)                                                  \
        h_middle(h_ascii_itoken((uint8_t *)lit, (sizeof lit) - 1), right,      \
                 abnf.CRLF)

        H_ARULE(orig_date, UUH_FIELD("Date:", date_time));
        H_ARULE(from, UUH_FIELD("From:", mailbox_list));
        H_ARULE(sender, UUH_FIELD("Sender:", mailbox));
        H_ARULE(reply_to, UUH_FIELD("Reply-To:", address_list));
        H_ARULE(to, UUH_FIELD("To:", address_list));
        H_ARULE(cc, UUH_FIELD("Cc:", address_list));
        H_ARULE(bcc, UUH_FIELD("Bcc:", h_optional(UH_ANY(address_list, CFWS))));

        H_RULE(id_left, dot_atom_text);

        H_RULE(no_fold_litteral, h_middle(h_ch('['), h_many(dtext), h_ch(']')));
        H_RULE(id_right, UH_ANY(dot_atom_text, no_fold_litteral));

        H_ARULE(msg_id, h_middle(UH_SEQ(h_optional(CFWS), h_ch('<')),
                                 UH_SEQ(id_left, h_ch('@'), id_right),
                                 UH_SEQ(h_ch('>'), h_optional(CFWS))));

        H_ARULE(message_id, UUH_FIELD("Message-ID:", msg_id));
        H_ARULE(in_reply_to, UUH_FIELD("In-Reply-To:", h_many1(msg_id)));
        H_ARULE(references, UUH_FIELD("References:", h_many1(msg_id)));

        H_ARULE(subject, UUH_FIELD("Subject:", unstructured));
        H_ARULE(comments, UUH_FIELD("Comments:", unstructured));
        H_ARULE(keywords, UUH_FIELD("Keywords:", h_sepBy1(phrase, h_ch(','))));

        H_RULE(resent_date, UUH_FIELD("Resent-Date:", date_time));
        H_RULE(resent_from, UUH_FIELD("Resent-From:", mailbox_list));
        H_RULE(resent_sender, UUH_FIELD("Resent-Sender:", mailbox));
        H_RULE(resent_to, UUH_FIELD("Resent-To:", address_list));
        H_RULE(resent_cc, UUH_FIELD("Resent-CC:", address_list));
        H_RULE(resent_bcc, UUH_FIELD("Resent-Bcc:",
                                     h_optional(UH_ANY(address_list, CFWS))));
        H_RULE(resent_msg_id, UUH_FIELD("Resent-Message-ID:", msg_id));

        H_RULE(path, UH_ANY(angle_addr, UH_SEQ(h_optional(CFWS), h_ch('<'),
                                               h_optional(CFWS), h_ch('>'),
                                               h_optional(CFWS))));
        H_RULE(trace_return, UUH_FIELD("Return-Path:", path));

        H_RULE(received_token, UH_ANY(word, angle_addr, addr_spec, domain));
        H_RULE(received, UUH_FIELD("Received:", UH_SEQ(h_many(received_token),
                                                       h_ch(';'), date_time)));

        H_RULE(trace, UH_SEQ(h_optional(trace_return), h_many1(received)));

#undef UUH_FIELD

        // Printable US-ASCII excluding :
        H_RULE(ftext, UH_ANY(h_ch_range(33, 57), h_ch_range(59, 126)));
        H_ARULE(field_name, h_many1(ftext));

        H_ARULE(optional_field, UH_SEQ(field_name, h_ignore(h_ch(':')),
                                       unstructured, abnf.CRLF));

        H_RULE(fields,
               UH_SEQ(
                   // the trace is described in RFC5321
                   h_many(UH_SEQ(
                       trace,
                       UH_ANY(h_many(UH_ANY(resent_date, resent_from,
                                            resent_sender, resent_to, resent_cc,
                                            resent_bcc, resent_msg_id)),
                              h_many(optional_field)))),
                   h_many(UH_ANY(
                       orig_date, from, sender, reply_to, to, cc, bcc,
                       message_id, in_reply_to, references, subject, comments,
                       keywords, rfc2045.content_transfer_encoding_header_field,
                       rfc2045.content_type_header_field, optional_field))));

        // ## 3.5.  Overall Message Syntax

        // Characters without CR and LF
        H_RULE(text, UH_ANY(h_ch_range(1, 9), h_ch(11), h_ch(12),
                            h_ch_range(14, 127)));

        // TODO(nicolas): I need a range many to implement this:
        // @see https://github.com/UpstandingHackers/hammer/issues/134
        H_RULE(text998, h_many1(text));

        H_RULE(body,
               UH_SEQ(h_many(UH_SEQ(h_many(text998), abnf.CRLF)), text998));

        H_RULE(message,
               UH_SEQ(fields, h_optional(UH_SEQ(abnf.CRLF, body)), h_end_p()));

        // ...RFC5322>

        /* publish our tokens */
        token_types_init(rfc5322_token_types);

        /* publish our parsers */

        if (h_compile(message, PB_MIN, NULL)) {
                fatal("problem compiling rfc5322 parser")
        } else {
                rfc5322_parsers.message = message;
        }

        if (h_compile(fields, PB_MIN, NULL)) {
                fatal("problem compiling rfc5322 parser")
        } else {
                rfc5322_parsers.fields = fields;
        }

#if ZWEI_UNIT_TESTS
        trace_print("Testing rfc5322 parsers:");
        CHECK_PARSER2(UH_SEQ(FWS, abnf.CRLF), "   \r\n", rfc5322_print_ast);
        CHECK_PARSER2(address_list,
                      "mailto:microsound-subscribe@hyperreal.org;",
                      rfc5322_print_ast);
        CHECK_PARSER2(date_time, "Mon, 6 Jan 2014 10:10:15 +0100",
                      rfc5322_print_ast);
        CHECK_PARSER2(date_time, "Fri, 30 Nov 1984 06:07:08 -0732",
                      rfc5322_print_ast);
        CHECK_PARSER2(date_time, "Mon, 06 Jan 2014 13:37 +0100",
                      rfc5322_print_ast);
        CHECK_PARSER2(date_time, "Thu, 9 Sep 1999 16:19:26 +0300 (EEST)",
                      rfc5322_print_ast);
        CHECK_PARSER2(subject, "Subject: I am\r\n folding\r\n",
                      rfc5322_print_ast);
        CHECK_PARSER2(mailbox, "=?UTF-8?Q?Nicolas_L=c3=a9veill=c3=a9?= <a@b.c>",
                      rfc5322_print_ast);
        CHECK_PARSER2(from, "From: \"Schmidt, Christian, MD6042\" "
                            "<Christian.Schmidt@MDM.MDemag.com>\r\n",
                      rfc5322_print_ast);
        CHECK_PARSER2(mailbox_list, "\"Muji\" <muji@b2d1.espmp-agfr.net> \r\n",
                      rfc5322_print_ast);
        CHECK_PARSER2(from, "From: \"Muji\" <muji@b2d1.espmp-agfr.net> \r\n",
                      rfc5322_print_ast);
        CHECK_PARSER2(message, "From: Nicolas(with a comment \\( (inside)) "
                               "<nicolas@uucidl.com>\r\nSubject: I am "
                               "testing this\r\n",
                      rfc5322_print_ast);
        CHECK_PARSER2(fields, "fROm: nicolas@uucidl.com\r\n",
                      rfc5322_print_ast);
        CHECK_PARSER2(fields, "From: Nicolas =?UTF-8?B?TMOpdmVpbGzDqQ==?= "
                              "<nicolas.leveille@free.fr>\r\n",
                      rfc5322_print_ast);
        CHECK_PARSER2(fields, "subject:\r\n", rfc5322_print_ast);

        // Testing unstructured text fields
        CHECK_PARSER2(unstructured, "Gene Structure\r\n", rfc5322_print_ast);
        CHECK_PARSER2(unstructured, "\r\n", rfc5322_print_ast);
        CHECK_PARSER2(unstructured, " \r\n", rfc5322_print_ast);
        CHECK_PARSER2(unstructured,
                      "En Septembre =?utf-8?Q?=C3=A0?= Confluences",
                      rfc5322_print_ast);

        // Testing encoded-words in unstructured text fields
        CHECK_PARSER2(unstructured,
                      "=?ISO-8859-1?B?SWYgeW91IGNhbiByZWFkIHRoaXMgeW8=?=\r\n   "
                      " =?ISO-8859-2?B?dSB1bmRlcnN0YW5kIHRoZSBleGFtcGxlLg==?="
                      "\r\n",
                      rfc5322_print_ast);

        // NOTE(nicolas): this should be equal to ab (spaces in the middle are
        // eaten)
        trace_print("should be equal to `ab`");
        CHECK_PARSER2(unstructured, "=?ISO-8859-1?Q?a?=  =?ISO-8859-1?Q?b?=",
                      rfc5322_print_ast);
        trace_print("should be equal to `a b`");
        CHECK_PARSER2(unstructured, "=?ISO-8859-1?Q?a?= b", rfc5322_print_ast);
#endif

        return rfc5322_parsers;
}

bool has_descendants(RFC5322TreeCoordinate c)
{
        using algos::source;
        // TODO(nil): finding out if a token type is a sequence type should be a
        // cheap
        // operation, because we're doing this check pretty often.
        auto is_sequence =
            TT_SEQUENCE == token_type_base(source(c)->token_type);
        return is_sequence && source(c)->seq->used > 0;
}

RFC5322TreeCoordinate::DescendantIterator
descendants_begin(RFC5322TreeCoordinate c)
{
        RFC5322TreeCoordinate::DescendantIterator result;
        if (!has_descendants(c)) {
                result.token = nullptr;
        } else {
                auto seq = source(c)->seq;
                result.token = (HParsedToken const **)&seq->elements[0];
        }
        return result;
}

RFC5322TreeCoordinate::DescendantIterator
descendants_end(RFC5322TreeCoordinate c)
{
        RFC5322TreeCoordinate::DescendantIterator result;
        if (!has_descendants(c)) {
                result.token = nullptr;
        } else {
                auto seq = source(c)->seq;
                result.token = (HParsedToken const **)&seq->elements[seq->used];
        }
        return result;
}

// TODO(nicolas) do not use stdio, use a stream of TextOutputGroup instead.
zw_internal void rfc5322_pprint_node(FILE *stream,
                                     algos::VisitType visit,
                                     const HParsedToken *ast,
                                     int indent)
{
        const char *type_name = "<unknown>";
        switch (ast->token_type) {
        case TT_NONE:
                type_name = "null";
                break;
        case TT_BYTES:
                type_name = "bytes";
                break;
        case TT_SINT:
                type_name = "s";
                break;
        case TT_UINT:
                type_name = "u";
                break;
        case TT_SEQUENCE:
                type_name = "";
                break;
        case TT_USER:
        default:
                if (ast->token_type >= TT_USER) {
                        type_name = h_get_token_type_name(ast->token_type);
                }
        }

        bool content_type_utf8 = false;
        HTokenType base_token_type = ast->token_type;
        {
                auto match = token_type_find(ast->token_type);
                if (match.first) {
                        content_type_utf8 = true;
                        base_token_type = match.second->base_type;
                }
        }

        if (visit == algos::PRE) {
                zw_assert(type_name, "unknown type name");
                fprintf(stream, "%*s%s%s%s", indent, "",
                        ast->token_type >= TT_USER ? "USER:" : "", type_name,
                        type_name[0] ? " " : "");
        }

        if (base_token_type == TT_SEQUENCE || ast->token_type == TT_SEQUENCE) {
                if (ast->seq->used > 0) {
                        switch (visit) {
                        case algos::PRE:
                                fprintf(stream, "[\n");
                                break;
                        case algos::POST:
                                fprintf(stream, "%*s]\n", indent, "");
                        }
                } else {
                        if (visit == algos::PRE) {
                                fprintf(stream, "[]\n");
                        }
                }
        }

        if (visit != algos::PRE) {
                return;
        }

        switch (base_token_type) {
        case TT_NONE:
                fprintf(stream, "\n");
                break;
        case TT_BYTES:
                if (ast->bytes.len == 0)
                        fprintf(stream, "<>\n");
                else {
                        if (content_type_utf8) {
                                fprintf(stream, "<");
                                for (size_t i = 0; i < ast->bytes.len; i++) {
                                        fprintf(stream, "%c",
                                                ast->bytes.token[i]);
                                }
                        } else {
                                for (size_t i = 0; i < ast->bytes.len; i++) {
                                        fprintf(stream, "%c%02hhx",
                                                (i == 0) ? '<' : '.',
                                                ast->bytes.token[i]);
                                }
                        }
                        fprintf(stream, ">\n");
                }
                break;
        case TT_SINT:
                if (ast->sint < 0) {
                        fprintf(stream, "<-%#" PRIx64 ">", -ast->sint);
                } else {
                        fprintf(stream, "<%#" PRIx64 ">", ast->sint);
                }

                fprintf(stream, " (%" PRIi64 ")\n", ast->sint);
                break;
        case TT_UINT:
                fprintf(stream, "<%#" PRIx64 "> (%" PRIi64 ")\n", ast->uint,
                        ast->uint);
                break;
        case TT_SEQUENCE: {
        } break;
        case TT_USER: {
                auto &token = *ast;
                auto match = token_type_find(token.token_type);
                if (match.first) {
                        if (match.second ==
                            &rfc5322_token_types[RFC5322_TT_DATE_TIME]) {
                                const CivilDateTime &value =
                                    *(CivilDateTime *)token.user;
                                fprintf(stdout, "<%d-%02d-%02dT%02d:%02d:%"
                                                "02d%c%02d:%02d>\n",
                                        value.year, value.month_count,
                                        value.day_count, value.hour,
                                        value.minute, value.seconds,
                                        value.zone_hour_offset >= 0 ? '+' : '-',
                                        abs(value.zone_hour_offset),
                                        value.zone_minute_offset);
                        }
                        break;
                }
        }
        default:
                if (base_token_type < TT_USER) {
                        zw_assert(false, "Should not reach here.");
                }
                fprintf(stdout, "... \n");
        }
}

zw_internal RFC5322TreeCoordinate rfc5322_top(const HParsedToken *&ast)
{
        RFC5322TreeCoordinate result;
        result.token = &ast;
        return result;
}

zw_internal void rfc5322_print_ast(FILE *stream,
                                   const HParsedToken *ast,
                                   int const indent,
                                   int const indent_delta)
{
        auto top = rfc5322_top(ast);
        fprintf(stdout, "rfc5322 tree weight: %lu, height: %lu\n",
                algos::weight_recursive(top), algos::height_recursive(top));

        int next_indent = indent;
        auto print_node = [&next_indent, stream, indent_delta](
            algos::VisitType visit, const RFC5322TreeCoordinate &coord) {
                if (visit == algos::PRE) {
                        next_indent += indent_delta;
                }
                rfc5322_pprint_node(stdout, visit, source(coord),
                                    next_indent - indent_delta);

                if (visit == algos::POST) {
                        next_indent -= indent_delta;
                }
        };

        algos::traverse_nonempty(top, std::cref(print_node));
        fflush(stdout);
}

zw_internal bool rfc5322_validate(const HParsedToken *ast,
                                  MemoryArena work_arena)
{
        // NOTE(nicolas): Some field validations (number of each,
        // depending on the conditions) i.e. sender must exist if
        // there's a multi from etc.. can be implemented as hammer
        // validation functions.

        auto top = rfc5322_top(ast);

        struct {
                uint32_t counts[RFC5322_TT_LAST] = {};
        } flags;

        struct {
                RFC5322TokenType type;
                uint8_t min;
                uint8_t max;
        } static_limits[] = {
            {RFC5322_TT_ORIG_DATE_FIELD, 1, 1},
            {RFC5322_TT_FROM_FIELD, 1, 1},
            {RFC5322_TT_SENDER_FIELD, 0, 1},
            {RFC5322_TT_REPLY_TO_FIELD, 0, 1},
            {RFC5322_TT_TO_FIELD, 0, 1},
            {RFC5322_TT_CC_FIELD, 0, 1},
            {RFC5322_TT_BCC_FIELD, 0, 1},
            {RFC5322_TT_MESSAGE_ID_FIELD, 1, 1},
            {RFC5322_TT_IN_REPLY_TO_FIELD, 0, 1},
            {RFC5322_TT_REFERENCES_FIELD, 0, 1},
            {RFC5322_TT_SUBJECT_FIELD, 0, 1},
        };

        auto collect = [&flags](HParsedToken const *token) {
                using algos::begin;
                using algos::end;
                auto match = token_type_match(begin(rfc5322_token_types),
                                              end(rfc5322_token_types),
                                              token->token_type);
                if (match.first) {
                        ++flags.counts[match.second -
                                       begin(rfc5322_token_types)];
                }
        };

        algos::traverse_each(top, std::cref(collect));

        // we'd want an unlimited extent textoutput here
        TextOutputGroup out;
        allocate(out, &work_arena, 1000);
        bool is_valid = true;
        for (const auto &static_limit : static_limits) {
                auto count = flags.counts[static_limit.type];
                if (count < static_limit.min) {
                        is_valid = false;
                        push_back_formatted(
                            out, "ERROR %s field count %d < min: %d",
                            rfc5322_token_types[static_limit.type].name_literal,
                            count, static_limit.min);
                        trace(out);
                }
                if (count > static_limit.max) {
                        is_valid = false;
                        push_back_formatted(
                            out, "ERROR %s field count %d > max: %d",
                            rfc5322_token_types[static_limit.type].name_literal,
                            count, static_limit.max);
                        trace(out);
                }
        }

        // TODO(nicolas): by returning a single boolean we are
        // dismissing a lot of additional information that we have
        // computed. We should be more precise.

        return is_valid;
}

// TODO(nicolas): isn't there some sort of tree reduction algorithm we could
// write?

template <OutputIterator O>
O copy_all_token_bytes(RFC5322TreeCoordinate const &top, O d_first)
{
        auto copier = [&d_first](HParsedToken const *token) {
                // TODO(nicolas): statistical property is approximatively 50% of
                // all nodes
                // are byte nodes that must be copied. In this case calling
                // `token_type_base`
                // seems wasteful. Why can't the nodes at construction time keep
                // that
                // knowledge somehow.
                auto contain_bytes =
                    TT_BYTES == token_type_base(token->token_type);
                SPDR_EVENT1(global_spdr, "stats", "copy_all_token_bytes.token",
                            SPDR_INT("is_copied", int(contain_bytes)));
                if (contain_bytes) {
                        d_first = algos::copy_n(token->bytes.token,
                                                token->bytes.len, d_first);
                }
        };
        algos::traverse_each(top, std::cref(copier));
        return d_first;
}

zw_internal size_t rfc5322_field_bytes_count(HParsedToken const *token)
{
        algos::VoidOutputIteratorAdapter<uint8_t *> d_first = {};
        return copy_all_token_bytes(rfc5322_top(token), d_first) - d_first;
}

zw_internal uint8_t *rfc5322_field_copy_bytes(HParsedToken const *token,
                                              uint8_t *d_bytes)
{
        return copy_all_token_bytes(rfc5322_top(token), d_bytes);
}

zw_internal ArrayAllocationSize
rfc5322_field_get_bytes_array_size(const HParsedToken *token)
{
        ArrayAllocationSize result;
        result.count = 0;
        result.extra_bytes_count = 0;

        auto top = rfc5322_top(token);
        auto count = [&result](HParsedToken const *token) {
                if (TT_BYTES == token_type_base(token->token_type)) {
                        ++result.count;
                        result.extra_bytes_count += token->bytes.len;
                }
        };
        algos::traverse_each(top, std::cref(count));

        return result;
}

zw_internal ByteCountedRange *
rfc5322_field_copy_bytes_array(const HParsedToken *token,
                               ByteCountedRange *d_first,
                               uint8_t *d_bytes_first)
{
        auto top = rfc5322_top(token);
        auto collect = [&d_first, &d_bytes_first](HParsedToken const *token) {
                if (TT_BYTES == token_type_base(token->token_type)) {
                        auto &range = algos::sink(d_first);
                        range.first = d_bytes_first;
                        range.count = token->bytes.len;
                        d_bytes_first =
                            algos::copy_n(token->bytes.token, token->bytes.len,
                                          d_bytes_first);
                        d_first = algos::successor(d_first);
                }
        };
        algos::traverse_each(top, std::cref(collect));

        return d_first;
}

zw_internal CivilDateTime *
rfc5322_field_copy_date_time(HParsedToken const *token,
                             CivilDateTime *d_date_time)
{
        auto top = rfc5322_top(token);
        auto dest = d_date_time;
        auto collect = [&dest, d_date_time](HParsedToken const *token) {
                if (dest != d_date_time)
                        return;
                if (RFC5322TokenIs(token, DATE_TIME)) {
                        memcpy(dest, token->user, sizeof *dest);
                        ++dest;
                }
        };
        algos::traverse_each(top, std::cref(collect));
        return dest;
}

zw_internal ArrayAllocationSize
rfc5322_field_get_mailbox_array_size(HParsedToken const *token)
{
        ArrayAllocationSize result;
        result.count = 0;
        result.extra_bytes_count = 0;

        auto top = rfc5322_top(token);
        auto count = [&result](HParsedToken const *token) {
                using algos::begin;
                using algos::end;
                auto match = token_type_match(begin(rfc5322_token_types),
                                              end(rfc5322_token_types),
                                              token->token_type);
                if (match.first) {
                        auto type = match.second - begin(rfc5322_token_types);
                        auto &type_entry = *match.second;
                        if (type == RFC5322_TT_MAILBOX) {
                                ++result.count;
                        } else if (type_entry.base_type == TT_BYTES) {
                                result.extra_bytes_count += token->bytes.len;
                        }
                        return;
                }
        };
        algos::traverse_each(top, std::cref(count));

        return result;
}

zw_internal RawMailbox *rfc5322_field_copy_mailbox_array(
    const HParsedToken *token, RawMailbox *d_first, uint8_t *d_bytes_first)
{
        auto top = rfc5322_top(token);
        bool mailbox_child = false;
        auto grab_data = [&d_first, &d_bytes_first, &mailbox_child](
            algos::VisitType visit, RFC5322TreeCoordinate const &coords) {
                using algos::begin;
                using algos::end;

                HParsedToken const &token = *source(coords);

                auto &mailbox = algos::sink(d_first);

                auto match = token_type_match(begin(rfc5322_token_types),
                                              end(rfc5322_token_types),
                                              token.token_type);
                if (match.first) {
                        auto type = match.second - begin(rfc5322_token_types);
                        auto const &type_entry = *match.second;
                        if (type == RFC5322_TT_MAILBOX) {
                                if (visit == algos::PRE) {
                                        mailbox = {};
                                        mailbox_child = true;
                                } else if (visit == algos::POST) {
                                        d_first = algos::successor(d_first);
                                        mailbox_child = false;
                                }
                        } else if (visit == algos::PRE && mailbox_child) {
                                auto const &source_bytes = token.bytes;
                                ByteCountedRange *dest_byte_range = nullptr;
                                if (type == RFC5322_TT_DOMAIN) {
                                        dest_byte_range =
                                            &(mailbox.domain_bytes);
                                } else if (type == RFC5322_TT_LOCAL_PART) {
                                        dest_byte_range =
                                            &(mailbox.local_part_bytes);
                                } else if (type == RFC5322_TT_DISPLAY_NAME) {
                                        dest_byte_range =
                                            &(mailbox.display_name_bytes);
                                }

                                zw_assert(dest_byte_range, "unexpected token");
                                zw_assert(type_entry.base_type == TT_BYTES,
                                          "unexpected type");

                                if (dest_byte_range) {
                                        dest_byte_range->first = d_bytes_first;
                                        dest_byte_range->count =
                                            source_bytes.len;
                                        d_bytes_first = algos::copy_n(
                                            source_bytes.token,
                                            source_bytes.len, d_bytes_first);
                                }
                        }
                        return;
                }
        };
        algos::traverse_nonempty(top, std::cref(grab_data));

        return d_first;
}

zw_internal ContentTransferEncodingType
rfc5322_get_content_transfer_encoding(const HParsedToken *ast)
{
        ContentTransferEncodingType result =
            ContentTransferEncoding_7BIT; // default
        auto top = rfc5322_top(ast);
        auto grab_data = [&result](algos::VisitType visit,
                                   RFC5322TreeCoordinate const &coords) {
                HParsedToken const &token = *source(coords);
                if (RFC2045TokenIs(&token, CONTENT_TRANSFER_ENCODING_FIELD)) {
                        result = (ContentTransferEncodingType)token.uint;
                }
        };
        algos::traverse_nonempty(top, std::cref(grab_data));
        return result;
}

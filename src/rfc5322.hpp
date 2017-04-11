#pragma once

#include "zwei_inlines.hpp"

#include "hammer.hpp"
#include "hammer_fwd.hpp"
#include "hammer_iterators.hpp"
#include "zwei_types.hpp"

struct RFC2045;
struct RFC2047;
struct ABNF_RFC5234;
struct RFC5322_Base;

struct ArrayAllocationSize {
        size_t count; /// number of elements in the array
        size_t
            extra_bytes_count; /// size in bytes to allocate for the bytes array
};

struct RFC5322TreeCoordinate {
        using RegularMe = RFC5322TreeCoordinate;
        using ReadableMe = RFC5322TreeCoordinate;
        using TreeCoordinateMe = RFC5322TreeCoordinate;
        HParsedToken const **token;

        friend bool has_descendants(RFC5322TreeCoordinate c);

        struct DescendantIterator {
                using RegularMe = DescendantIterator;
                using ReadableMe = DescendantIterator;
                using IteratorMe = DescendantIterator;

                HParsedToken const **token;
                DescendantIterator() {}
                DescendantIterator(const RegularMe &x) : token(x.token){};
                RegularMe &operator=(const RegularMe &x)
                {
                        token = x.token;
                        return *this;
                }

                friend bool operator==(const RegularMe &a, const RegularMe &b)
                {
                        return a.token == b.token;
                }

                friend bool operator!=(const RegularMe &a, const RegularMe &b)
                {
                        return !(a == b);
                }

                friend RFC5322TreeCoordinate source(ReadableMe c)
                {
                        RFC5322TreeCoordinate result;
                        result.token = c.token;
                        return result;
                }

                friend IteratorMe successor(IteratorMe it)
                {
                        using algos::successor;

                        DescendantIterator result;
                        result.token = successor(it.token);
                        return result;
                }
        };

        RFC5322TreeCoordinate() {}
        RFC5322TreeCoordinate(const RegularMe &x) : token(x.token){};
        RegularMe &operator=(const RegularMe &x)
        {
                token = x.token;
                return *this;
        }

        friend bool operator==(const RegularMe &a, const RegularMe &b)
        {
                return a.token == b.token;
        }

        friend bool operator!=(const RegularMe &a, const RegularMe &b)
        {
                return !(a == b);
        }

        friend HParsedToken const *source(ReadableMe c) { return *c.token; }

        friend DescendantIterator descendants_begin(TreeCoordinateMe c);
        friend DescendantIterator descendants_end(TreeCoordinateMe c);
};

template <> struct algos::TreeCoordinateConcept<RFC5322TreeCoordinate> {
        using WeightType = size_t;
};

/**
   These mark the various tokens identified in a mail.
*/

enum RFC5322TokenType {
        RFC5322_TT_DATE_TIME,
        RFC5322_TT_UNSTRUCTURED,
        RFC5322_TT_WORD,
        RFC5322_TT_COMMENT,
        RFC5322_TT_MSG_ID,
        RFC5322_TT_DOMAIN,
        RFC5322_TT_LOCAL_PART,
        RFC5322_TT_DISPLAY_NAME,
        RFC5322_TT_MAILBOX,
        RFC5322_TT_GROUP,
        RFC5322_TT_ORIG_DATE_FIELD,
        RFC5322_TT_FROM_FIELD,
        RFC5322_TT_SENDER_FIELD,
        RFC5322_TT_REPLY_TO_FIELD,
        RFC5322_TT_TO_FIELD,
        RFC5322_TT_CC_FIELD,
        RFC5322_TT_BCC_FIELD,
        RFC5322_TT_MESSAGE_ID_FIELD,
        RFC5322_TT_IN_REPLY_TO_FIELD,
        RFC5322_TT_REFERENCES_FIELD,
        RFC5322_TT_SUBJECT_FIELD,
        RFC5322_TT_COMMENTS_FIELD,
        RFC5322_TT_KEYWORDS_FIELD,
        RFC5322_TT_OPTIONAL_FIELD_NAME,
        RFC5322_TT_OPTIONAL_FIELD,
        RFC5322_TT_LAST,
};

#define RFC5322HammerTT(type_name)                                             \
        rfc5322_token_types[RFC5322_TT_##type_name].registered_type
#define RFC5322TokenIs(token, type_name)                                       \
        (token)->token_type == RFC5322HammerTT(type_name)

zw_internal UserTokenTypeEntry rfc5322_token_types[RFC5322_TT_LAST] = {
    {TT_USER, "date-time", TT_INVALID},
    {TT_BYTES, "unstructured", TT_INVALID},
    {TT_BYTES, "word", TT_INVALID},
    {TT_BYTES, "comment", TT_INVALID},
    {TT_BYTES, "msg-id", TT_INVALID},
    {TT_BYTES, "domain", TT_INVALID},
    {TT_BYTES, "local-part", TT_INVALID},
    {TT_BYTES, "display-name", TT_INVALID},
    {TT_SEQUENCE, "mailbox", TT_INVALID},
    {TT_SEQUENCE, "group", TT_INVALID},
    {TT_SEQUENCE, "orig-date", TT_INVALID},
    {TT_SEQUENCE, "from", TT_INVALID},
    {TT_SEQUENCE, "sender", TT_INVALID},
    {TT_SEQUENCE, "reply-to", TT_INVALID},
    {TT_SEQUENCE, "to", TT_INVALID},
    {TT_SEQUENCE, "cc", TT_INVALID},
    {TT_SEQUENCE, "bcc", TT_INVALID},
    {TT_SEQUENCE, "message-id", TT_INVALID},
    {TT_SEQUENCE, "in-reply-to", TT_INVALID},
    {TT_SEQUENCE, "references", TT_INVALID},
    {TT_SEQUENCE, "subject", TT_INVALID},
    {TT_SEQUENCE, "comments", TT_INVALID},
    {TT_SEQUENCE, "keywords", TT_INVALID},
    {TT_BYTES, "field-name", TT_INVALID},
    {TT_SEQUENCE, "optional-field", TT_INVALID},
};

struct RFC5322 {
        HParser *message;
        HParser *fields;
};

zw_internal const RFC5322 &make_rfc5322(const ABNF_RFC5234 &abnf,
                                        const RFC5322_Base &rfc5322_base,
                                        const RFC2047 &rfc2047,
                                        const RFC2045 &rfc2045);

zw_internal bool rfc5322_validate(const HParsedToken *ast,
                                  MemoryArena work_arena);
zw_internal void rfc5322_print_ast(FILE *stream,
                                   const HParsedToken *ast,
                                   int indent,
                                   int indent_delta);
zw_internal RFC5322TreeCoordinate rfc5322_top(HParsedToken const *&ast);

/**
 * Grabbing bytes out of a field. Returns the total bytes count
 */
zw_internal size_t rfc5322_field_bytes_count(HParsedToken const *token);

/**
 * Grabbing bytes out of a field. Copy all bytes into the d_bytes destination.
 */
zw_internal uint8_t *rfc5322_field_copy_bytes(HParsedToken const *token,
                                              uint8_t *d_bytes);

/**
 * Grabbing multiple bytes tags. Obtain the amount
 */
zw_internal ArrayAllocationSize
rfc5322_field_get_bytes_array_size(HParsedToken const *token);

zw_internal CivilDateTime *
rfc5322_field_copy_date_time(HParsedToken const *token,
                             CivilDateTime *d_date_time);

/**
   @pre dest must point at an area >= allocation_size as returned by
   `rfc5322_field_get_bytes_array_size`
 */
zw_internal ByteCountedRange *
rfc5322_field_copy_bytes_array(const HParsedToken *token,
                               ByteCountedRange *d_first,
                               uint8_t *d_bytes_first);

zw_internal ArrayAllocationSize
rfc5322_field_get_mailbox_array_size(const HParsedToken *token);

/**
   @pre dest must point at an area >= allocation_size as returned by
   `rfc5322_field_get_mailbox_array_size`
 */
zw_internal RawMailbox *rfc5322_field_copy_mailbox_array(
    const HParsedToken *token, RawMailbox *d_first, uint8_t *d_bytes_first);
zw_internal ContentTransferEncodingType
rfc5322_get_content_transfer_encoding(const HParsedToken *ast);

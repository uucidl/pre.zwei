#pragma once

#include "algos.hpp"
#include "hammer.hpp"
#include "hammer_iterators.hpp"
#include "zwei_inlines.hpp"

// <PARSERS...

/**
 * US-ASCII case insensitive token parser.
 *
 * Used in ABNF when tokens are described in double quotes.
 */
HAMMER_FN_DECL(HParser *, h_ascii_itoken, const uint8_t *str, const size_t len);

// ...PARSERS>

// <ACTIONS...

zw_internal HParsedToken *uuh_make_tagged_bytes_from_sequence(
    HParsedToken const *sequence, HTokenType bytes_token_type, HArena *arena);

// TODO(nicolas): probably should return instead one of our own node types, so
// we can enrich it with the base hammer type or simply the information whether
// it was bytes or a sequence
zw_internal HParsedToken *uuh_flatten_sequence(HParseResult const *p,
                                               HTokenType token_type,
                                               void *user_data);

zw_internal HParsedToken *uuh_act_integer(HParseResult const *p,
                                          void *user_data);
zw_internal HParsedToken *uuh_act_bytes_from_sequence(HParseResult const *p,
                                                      void *user_data);

// ...ACTIONS>

// <TOKEN TYPES...
/**
 * Represents a user token type
 */
struct UserTokenTypeEntry {
        HTokenType base_type;
        char const *name_literal;
        HTokenType registered_type;
};

zw_internal void hammer_init();

// [first,last) must remain valid for as long as hammer is used
zw_internal void token_types_init(UserTokenTypeEntry *first,
                                  UserTokenTypeEntry *last);

template <typename RandomAccessRange>
void token_types_init(RandomAccessRange &range)
{
        using algos::begin;
        using algos::end;
        return token_types_init(begin(range), end(range));
}

zw_internal HTokenType token_type_base(HTokenType derived_type);

zw_internal std::pair<bool, UserTokenTypeEntry const *>
token_type_find(HTokenType token_type);

zw_internal std::pair<bool, UserTokenTypeEntry const *>
token_type_match(UserTokenTypeEntry const *first,
                 UserTokenTypeEntry const *last,
                 HTokenType token_type);

// ...TOKEN TYPES>

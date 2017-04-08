#include "hammer_utils.hpp"

// TODO(nicolas): parsing performance of lower-case US-ASCII.
//
// All the ABNF string tokens areupposed to be case-insensitive.
//
// This hacky `h_itoken` was created using hammer's base
// parsers. However we need to assess its performance.
//
// If inefficient, this means going into `token.c` and creating a case
// insensitive version of the h_token parser. Note that this case
// insensitivity would only be supported for US-ASCII.

HParser *h_ascii_itoken(const uint8_t *str, const size_t len)
{
        // NOTE(nicolas): I suspect this will be inefficient compared to a
        // version
        // that can do tolower(...) in the backend, but we'll try with
        // this first.

        HParser *choices[len + 1];

        auto sentinel = algos::apply_copy(
            str, str + len, (HParser **)&choices, [](const uint8_t &c) {
                    if (c >= 0x61 && c <= 0x7a) {
                            // in [a-z]
                            return UH_ANY(h_ch(c), h_ch(0x41 + (c - 0x61)));
                    } else if (c >= 0x41 && c <= 0x5a) {
                            // in [A-Z]
                            return UH_ANY(h_ch(c), h_ch(0x61 + (c - 0x41)));
                    } else {
                            return h_ch(c);
                    }
            });
        algos::sink(sentinel) = nullptr;

        return h_sequence__a((void **)choices);
}

zw_internal HParsedToken *uuh_act_integer(const HParseResult *p,
                                          void *user_data)
{
        uint64_t value = (uint64_t)user_data;
        return H_MAKE_UINT(value);
}

zw_internal HParsedToken *
uuh_make_bytes_from_sequence(HParsedToken const *sequence, HArena *arena)
{
        zw_assert(sequence->token_type == TT_SEQUENCE, "unexpected token");
        using algos::begin;
        using algos::end;

        const auto content_first = begin(sequence);
        const auto content_last = end(sequence);

        size_t bytes_size = 0;
        algos::for_each(
            content_first, content_last, [&bytes_size](const HParsedToken *x) {
                    zw_assert(x->token_type == TT_NONE ||
                                  x->token_type == TT_UINT,
                              "unexpected token");
                    if (x->token_type == TT_UINT) {
                            zw_assert(x->uint < 256, "unexpected integer");
                            ++bytes_size;
                    }
            });

        uint8_t *bytes = (uint8_t *)h_arena_malloc(arena, bytes_size);
        auto last = algos::apply_copy_if(
            content_first, content_last, bytes,
            [](const HParsedToken *x) { return x->uint; },
            [](const HParsedToken *x) { return x->token_type == TT_UINT; });

        HParsedToken *result = h_make_bytes(arena, bytes, last - bytes);
        return result;
}

zw_internal HParsedToken *uuh_make_tagged_bytes_from_sequence(
    HParsedToken const *sequence, HTokenType bytes_token_type, HArena *arena)
{
        HParsedToken *result = uuh_make_bytes_from_sequence(sequence, arena);
        result->token_type = bytes_token_type;
        return result;
}

zw_internal HParsedToken *uuh_act_bytes_from_sequence(const HParseResult *p,
                                                      void *user_data)
{
        return uuh_make_bytes_from_sequence(p->ast, p->arena);
}

zw_internal HParsedToken *uuh_flatten_sequence(HParseResult const *p,
                                               HTokenType token_type,
                                               void *user_data)
{
        HParsedToken *sequence = h_act_flatten(p, user_data);
        sequence->token_type = token_type;
        return sequence;
}

// <TOKEN TYPES...

zw_internal void hammer_init()
{
        zw_assert(TT_USER == h_allocate_token_type("_user_"),
                  "expected to be the first");
}

zw_internal void allocate_token_types(UserTokenTypeEntry *first,
                                      UserTokenTypeEntry *last)
{
        algos::for_each(first, last, [](UserTokenTypeEntry &entry) {
                entry.registered_type =
                    h_allocate_token_type(entry.name_literal);
                zw_assert(entry.registered_type > TT_USER,
                          "TT_USER is reserved");
        });
}

zw_internal std::pair<bool, std::pair<int, UserTokenTypeEntry>>
match_token_type(UserTokenTypeEntry const *first,
                 UserTokenTypeEntry const *last,
                 HTokenType token_type)
{
        using algos::source;

        if (first == last) {
                return std::make_pair(
                    false, std::pair<int, UserTokenTypeEntry>(0, {}));
        }

        auto max_distance = last - first;
        auto offset = token_type - source(first).registered_type;

        if (offset >= 0 && offset < max_distance) {
                auto &type_entry = source(first + offset);
                return std::make_pair(true, std::make_pair(offset, type_entry));
        }

        return std::make_pair(false, std::pair<int, UserTokenTypeEntry>(0, {}));
}

// ..TOKEN TYPES>

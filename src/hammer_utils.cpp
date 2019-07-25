#include "hammer_utils.hpp"

#include "hammer_defines.hpp"

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

HParser *
h_ascii_itoken__m(HAllocator *mm_, const uint8_t *str, const size_t len)
{
        // NOTE(nicolas): I suspect this will be inefficient compared to a
        // version
        // that can do tolower(...) in the backend, but we'll try with
        // this first.

        HParser **choices = reinterpret_cast<HParser **>(
            h_alloc(mm_, (len + 1) * sizeof(*choices)));

        auto sentinel =
            algos::apply_copy(str, str + len, choices, [](const uint8_t &c) {
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
        HParser *sequence = h_sequence__a((void **)choices);
        mm_->free(mm_, choices);
        return sequence;
}

// TODO(nicolas): TAG(workaround) hammer internals, probably should not
// be used.
extern "C" HAllocator system_allocator;

HParser *h_ascii_itoken(const uint8_t *str, const size_t len)
{
        return h_ascii_itoken__m(&system_allocator, str, len);
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

struct TokenTypeSource {
        UserTokenTypeEntry *first;
        UserTokenTypeEntry *last;
};

enum { TOKEN_TYPES_SOURCES_N = 8,
};

zw_global TokenTypeSource token_types_sources[TOKEN_TYPES_SOURCES_N] = {};
zw_global size_t token_types_sources_size = 0;

zw_internal void hammer_init()
{
        zw_assert(TT_USER == h_allocate_token_type("_user_"),
                  "expected to be the first");
}

zw_internal void token_types_init(UserTokenTypeEntry *first,
                                  UserTokenTypeEntry *last)
{
        algos::for_each(first, last, [](UserTokenTypeEntry &entry) {
                entry.registered_type =
                    h_allocate_token_type(entry.name_literal);
                zw_assert(entry.registered_type > TT_USER,
                          "TT_USER is reserved");
        });
        auto source_index = token_types_sources_size;
        auto &source_entry = token_types_sources[source_index];
        ++token_types_sources_size;
        source_entry.first = first;
        source_entry.last = last;
}

zw_internal std::pair<bool, UserTokenTypeEntry const *>
token_type_match(UserTokenTypeEntry const *first,
                 UserTokenTypeEntry const *last,
                 HTokenType token_type)
{
        using algos::source;

        auto min_token = source(first).registered_type;
        auto max_token = last - first + min_token;

        if (token_type < min_token || token_type >= max_token) {
                return {};
        }

        auto offset = token_type - source(first).registered_type;
        auto &type_entry = source(first + offset);
        fatal_ifnot(token_type == type_entry.registered_type, "mismatch");
        return {true, first + offset};
}

zw_internal std::pair<bool, UserTokenTypeEntry const *>
token_type_find(HTokenType token_type)
{
        std::pair<bool, UserTokenTypeEntry const *> result = {};
        auto source_first = token_types_sources;
        auto const source_last = token_types_sources + token_types_sources_size;
        while (!result.first && source_first != source_last) {
                auto const &defs = algos::source(source_first);
                result = token_type_match(defs.first, defs.last, token_type);
                source_first = algos::successor(source_first);
        }
        return result;
}

zw_internal HTokenType token_type_base(HTokenType derived_type)
{
        auto type_match = token_type_find(derived_type);
        if (!type_match.first) {
                return derived_type;
        }

        return type_match.second->base_type;
}

// ..TOKEN TYPES>

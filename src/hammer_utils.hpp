#pragma once

#include "algos.hpp"
#include "hammer.hpp"
#include "hammer_iterators.hpp"
#include "zwei_inlines.hpp"

// <PARSERS...

#define UH_SEQ(...) h_sequence(__VA_ARGS__, NULL)
#define UH_ANY(...) h_choice(__VA_ARGS__, NULL)

#define REQUIRES_LITTERAL(lit) ("" lit)

/// h_in working on string litterals
#define UH_IN(lit) h_in((uint8_t *)REQUIRES_LITTERAL(lit), sizeof(lit) - 1)

#define UH_TOKEN(lit)                                                          \
        h_token((uint8_t *)REQUIRES_LITTERAL(lit), sizeof(lit) - 1)

// Case-insensitive ASCII token
#define UH_ITOKEN(lit)                                                         \
        h_ascii_itoken((uint8_t *)REQUIRES_LITTERAL(lit), sizeof(lit) - 1)

#define UH_IENUM(lit, key)                                                     \
        h_action(h_ascii_itoken((uint8_t *)lit, sizeof lit - 1),               \
                 uuh_act_integer, (void *)key)

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

/**
   US-ASCII case insensitive token parser.

   Used in ABNF when tokens are described in double quotes.
*/
HAMMER_FN_DECL(HParser *, h_ascii_itoken, const uint8_t *str, const size_t len);

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

// ...PARSERS>

// <ACTIONS...

#define HAMMER_ACTION(name)                                                    \
        zw_internal HParsedToken *name(const HParseResult *p, void *user_data)

zw_internal HParsedToken *uuh_act_integer(const HParseResult *p,
                                          void *user_data)
{
        uint64_t value = (uint64_t)user_data;
        return H_MAKE_UINT(value);
}

zw_internal HParsedToken *uuh_uint8_sequence_to_tagged_bytes(
    HParsedToken const *sequence, HTokenType token_type, HArena *arena)
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
        result->token_type = token_type;
        return result;
}

// TODO(nicolas): probably should return instead one of our own node types, so
// we can enrich it with the base hammer type or simply the information whether
// it was bytes or a sequence
zw_internal HParsedToken *uuh_flatten_sequence(HParseResult const *p,
                                               HTokenType token_type,
                                               void *user_data)
{
        HParsedToken *sequence = h_act_flatten(p, user_data);
        sequence->token_type = token_type;
        return sequence;
}

// ...ACTIONS>

// <TOKEN TYPES...
/**
   Represents a user token type
*/
struct UserTokenTypeEntry {
        HTokenType base_type;
        char const *name_literal;
        HTokenType registered_type;
};

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

template <typename RandomAccessRange>
std::pair<bool, std::pair<int, UserTokenTypeEntry>>
match_token_type(RandomAccessRange const &range, HParsedToken const &token)
{
        using algos::begin;
        using algos::end;
        return match_token_type(begin(range), end(range), token.token_type);
}
// ...TOKEN TYPES>

#if ZWEI_UNIT_TESTS
#include "zwei_logging.hpp"

#define CHECK_PARSER(parser, lit) CHECK_PARSER2(parser, lit, h_pprint)

#define CHECK_PARSER2(parser, lit, printfn)                                    \
        do {                                                                   \
                uint8_t data[] = lit;                                          \
                size_t data_size = sizeof data - 1;                            \
                auto check_parser_printer = [=](const HParsedToken *ast) {     \
                        printfn(stdout, ast, 0, 4);                            \
                        return true;                                           \
                };                                                             \
                check_parser_do_test(parser, data, data_size,                  \
                                     check_parser_printer);                    \
        } while (0)

#define VALIDATE_PARSER(parser, lit, checkerfn)                                \
        do {                                                                   \
                uint8_t data[] = lit;                                          \
                size_t data_size = sizeof data - 1;                            \
                check_parser_do_test(parser, data, data_size, checkerfn);      \
        } while (0)

/// ValueType(UnaryPredicate) == const HParsedToken*
template <UnaryPredicate P>
zw_internal void
check_parser_do_test(HParser *parser, uint8_t *data, size_t data_size, P astfn)
{
        char memory[512];
        auto arena = memory_arena(memory, sizeof memory);
        auto traceg = TextOutputGroup{};
        allocate(traceg, &arena, 28);
        push_back_cstr(traceg, "input: ");
        push_back_extent(traceg, data, data_size);
        trace(traceg);
        if (h_compile(parser, PB_MIN, NULL) < 0) {
                push_back_cstr(traceg, "problem with parser compile");
                error(traceg);
        }
        HParseResult *hr = h_parse(parser, data, data_size);
        if (!hr) {
                push_back_cstr(traceg, "FAIL: could not parse!");
                error(traceg);
                exit(1);
        } else {
                if (!astfn(hr->ast)) {
                        push_back_cstr(traceg, "FAIL: could not parse!");
                        error(traceg);
                        exit(1);
                }
                fflush(stdout);
                push_back_cstr(traceg, "SUCCESS: managed to parse!");
                trace(traceg);
                h_parse_result_free(hr);
        }
};

#endif

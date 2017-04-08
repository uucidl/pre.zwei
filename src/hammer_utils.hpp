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

/**
 * US-ASCII case insensitive token parser.
 *
 * Used in ABNF when tokens are described in double quotes.
 */
HAMMER_FN_DECL(HParser *, h_ascii_itoken, const uint8_t *str, const size_t len);

// ...PARSERS>

// <ACTIONS...

#define HAMMER_ACTION(name)                                                    \
        zw_internal HParsedToken *name(const HParseResult *p, void *user_data)

zw_internal HParsedToken *uuh_make_tagged_bytes_from_sequence(
    HParsedToken const *sequence, HTokenType bytes_token_type, HArena *arena);

// TODO(nicolas): probably should return instead one of our own node types, so
// we can enrich it with the base hammer type or simply the information whether
// it was bytes or a sequence
zw_internal HParsedToken *uuh_flatten_sequence(HParseResult const *p,
                                               HTokenType token_type,
                                               void *user_data);

HAMMER_ACTION(uuh_act_integer);
HAMMER_ACTION(uuh_act_bytes_from_sequence);

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

zw_internal std::pair<bool, UserTokenTypeEntry const *>
token_type_find(HTokenType token_type);

zw_internal std::pair<bool, UserTokenTypeEntry const *>
token_type_match(UserTokenTypeEntry const *first,
                 UserTokenTypeEntry const *last,
                 HTokenType token_type);

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

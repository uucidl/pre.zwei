#pragma once

#include "algos_concepts.hpp"
#include "zwei_text.hpp"

#include "../builds/include/hammer/hammer.h"

// <PARSERS...

#define UH_SEQ(...) h_sequence(__VA_ARGS__, NULL)
#define UH_ANY(...) h_choice(__VA_ARGS__, NULL)

#define UH_requires_literal(lit) ("" lit)

/// h_in working on string litterals
#define UH_IN(lit) h_in((uint8_t *)UH_requires_literal(lit), sizeof(lit) - 1)

#define UH_TOKEN(lit)                                                          \
        h_token((uint8_t *)UH_requires_literal(lit), sizeof(lit) - 1)

// Case-insensitive ASCII token
#define UH_ITOKEN(lit)                                                         \
        h_ascii_itoken((uint8_t *)UH_requires_literal(lit), sizeof(lit) - 1)

#define UH_IENUM(lit, key)                                                     \
        h_action(h_ascii_itoken((uint8_t *)lit, sizeof lit - 1),               \
                 uuh_act_integer, (void *)key)

// ...PARSERS>

// <ACTIONS...

#define HAMMER_ACTION(name)                                                    \
        zw_internal HParsedToken *name(const HParseResult *p, void *user_data)

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
zw_internal void check_parser_do_test(HParser *parser,
                                      uint8_t const *data,
                                      size_t data_size,
                                      P astfn)
{
        char memory[512];
        auto arena = memory_arena(memory, sizeof memory);
        auto traceg = textoutputgroup_allocate(&arena, 28);
        push_cstr(traceg, "input: ");
        push_extent(traceg, data, data_size);
        trace(traceg);
        if (h_compile(parser, PB_MIN, NULL) < 0) {
                push_cstr(traceg, "problem with parser compile");
                error(traceg);
        }
        HParseResult *hr = h_parse(parser, data, data_size);
        if (!hr) {
                push_cstr(traceg, "FAIL: could not parse!");
                error(traceg);
                exit(1);
        } else {
                if (!astfn(hr->ast)) {
                        push_cstr(traceg, "FAIL: could not parse!");
                        error(traceg);
                        exit(1);
                }
                fflush(stdout);
                push_cstr(traceg, "SUCCESS: managed to parse!");
                trace(traceg);
                h_parse_result_free(hr);
        }
};
#endif

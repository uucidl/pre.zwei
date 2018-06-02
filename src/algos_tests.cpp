#include "algos_tests.hpp"

#if !defined(fatal_ifnot)
#error "please define `fatal_ifnot` macro"
#define fatal_ifnot(__expr, message)
#endif

namespace algos
{
void run_tests()
{
        /* trivial */ {
                uint8_t ab[] = {1, 2};
                fatal_ifnot(
                    pointer(ab[1]) == pointer(max(ab[0], ab[1])),
                    "max selects the biggest and returns it by reference");
                fatal_ifnot(
                    pointer(ab[1]) == pointer(max(ab[1], ab[0])),
                    "max selects the biggest and returns it by reference");
        }

        /* TAG(degenerate) same values */ {
                uint8_t ab[] = {3, 3};
                fatal_ifnot(pointer(ab[1]) == pointer(max(ab[0], ab[1])),
                            "max select the rightmost reference when equal");
        }

        /* guarded, bounded, counted ranges */ {
                char const text[] = "hello, world";
                auto last = bound_guarded(text, '\0');
                auto count = count_guarded(text, '\0');
                fatal_ifnot(source(last) == '\0', "sentinel reached");
                fatal_ifnot(last - text == count,
                            "[first, last) == [[first, count))");
        }

        /* find_if_n */ {
                uint8_t abc[] = {1, 3, 0, 4};
                auto const is_negative = [](uint8_t x) { return x <= 0; };
                auto match = find_if_n(begin(abc), 4, is_negative);
                fatal_ifnot(source(match.first) == 0, "match: iterator");
                fatal_ifnot(match.second == 2, "match: count remaining");
                fatal_ifnot(all_n(begin(abc), 4 - match.second,
                                  [&](uint8_t x) { return !is_negative(x); }),
                            "all_n is derived");
        }

        /* equal_bounded_guarded(I0 f0, I0 l0, I1 f1, P1 valid1, R relation) */
        {
                char const abc_string[] = "abc";
                char const abcd_string[] = "abcd";

                auto const cstr_equal = [](char a, char b) { return a == b; };

                fatal_ifnot(equal_bounded_guarded(
                                begin(abcd_string), begin(abcd_string) + 3,
                                begin(abc_string), '\0', cstr_equal),
                            "should be equal");
                fatal_if(equal_bounded_guarded(
                             begin(abc_string), end(abc_string) - 1,
                             begin(abcd_string), '\0', cstr_equal),
                         "should not be equal");
        }
}
} // namespace algos

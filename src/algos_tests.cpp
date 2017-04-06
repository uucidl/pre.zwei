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
                    algos::pointer(ab[1]) ==
                        algos::pointer(algos::max(ab[0], ab[1])),
                    "max selects the biggest and returns it by reference");
                fatal_ifnot(
                    algos::pointer(ab[1]) ==
                        algos::pointer(algos::max(ab[1], ab[0])),
                    "max selects the biggest and returns it by reference");
        }

        /* TAG(degenerate) same values */ {
                uint8_t ab[] = {3, 3};
                fatal_ifnot(algos::pointer(ab[1]) ==
                                algos::pointer(algos::max(ab[0], ab[1])),
                            "max select the rightmost reference when equal");
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
}
}

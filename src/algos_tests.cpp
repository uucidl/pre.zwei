#include "algos_tests.hpp"

#if !defined(fatal_ifnot)
#error "please define `fatal_ifnot` macro"
#define fatal_ifnot(__expr, message)
#endif

namespace algos
{
void run_tests()
{
        uint8_t ab[] = {1, 2};
        fatal_ifnot(algos::pointer(ab[1]) ==
                        algos::pointer(algos::max(ab[0], ab[1])),
                    "max selects the biggest and returns it by reference");
        fatal_ifnot(algos::pointer(ab[1]) ==
                        algos::pointer(algos::max(ab[1], ab[0])),
                    "max selects the biggest and returns it by reference");
}
}

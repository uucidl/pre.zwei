#include "hammer_iterators.hpp"

#include "hammer.hpp"

HParsedToken const **begin(HParsedToken const *p)
{
        return (HParsedToken const **)(h_seq_elements(p));
}

HParsedToken const **end(HParsedToken const *p)
{
        return begin(p) + h_seq_len(p);
}

HParsedToken const **descendants_begin(HParsedToken const *x)
{
        if (x->token_type == TT_SEQUENCE) {
                return begin(x);
        }
        return nullptr;
}

HParsedToken const **descendants_end(HParsedToken const *x)
{
        if (x->token_type == TT_SEQUENCE) {
                return end(x);
        }
        return nullptr;
}

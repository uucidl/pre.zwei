#include "hammer.hpp"
#include "hammer_iterators.hpp"

HParsedToken const **begin(HParsedToken const *p)
{
        return (HParsedToken const **)(h_seq_elements(p));
}

HParsedToken const **end(HParsedToken const *p)
{
        return begin(p) + h_seq_len(p);
}

HParsedToken const **begin(HParsedToken const &token)
{
        return (HParsedToken const **)(h_seq_elements(&token));
}

HParsedToken const **end(HParsedToken const &token)
{
        return begin(token) + h_seq_len(&token);
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

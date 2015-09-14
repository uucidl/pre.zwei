#pragma once

/** @file Iterator implementations for hammer */

#include "algos.hpp"
#include "hammer_fwd.hpp"

/* Some HParsedToken are a sequence of HParsedToken */
template <> class algos::SequenceConcept<HParsedToken>
{
        using value_type = HParsedToken *;
        using iterator_type = HParsedToken *const *;
        using const_iterator_type = const HParsedToken *const *;
};

HParsedToken const **begin(HParsedToken const *p);
HParsedToken const **end(HParsedToken const *p);
HParsedToken const **begin(HParsedToken const &token);
HParsedToken const **end(HParsedToken const &token);

template <> struct algos::TreeCoordinateConcept<HParsedToken const *> {
        using WeightType = size_t;
};

HParsedToken const **descendants_begin(HParsedToken const *x);
HParsedToken const **descendants_end(HParsedToken const *x);

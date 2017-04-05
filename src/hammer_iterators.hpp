#pragma once

/** @file Iterator implementations for hammer */

#include "hammer_fwd.hpp"

// @model{HParsedToken const *, Sequence}

HParsedToken const **begin(HParsedToken const *p);
HParsedToken const **end(HParsedToken const *p);

// @model{HParsedToken const *, Tree}

HParsedToken const **descendants_begin(HParsedToken const *x);
HParsedToken const **descendants_end(HParsedToken const *x);

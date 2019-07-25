#pragma once

#include "zwei_inlines.hpp"

#include <cstddef>
#include <cstdint>

// TODO(nicolas): this definition is unsafe, as the size of the destination
// isn't actually checked
zw_internal void
sha1(uint8_t const *bytes, size_t bytes_size, uint8_t result[20]);

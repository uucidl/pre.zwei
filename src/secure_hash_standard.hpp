#pragma once

#include <cstddef>
#include <cstdint>

zw_internal void
sha1(uint8_t const *bytes, size_t bytes_size, uint8_t result[20]);

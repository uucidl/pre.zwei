#pragma once

#include "zwei_inlines.hpp"

#include "zwei_types.hpp"

/**
   @file Zoe specific code

   Zoe was an earlier implementation of Zwei.
*/

zw_internal void zoe_support_init();

zw_internal MayFail<ZoeMailStoreFile>
zoe_parse_uuid_filename(char const *filename, size_t filename_size);

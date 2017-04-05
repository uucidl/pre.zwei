#pragma once

#include "zwei_inlines.hpp"

#include "zwei_types.hpp"

/**
   @file Zoe specific code

   Zoe was an earlier implementation of Zwei.
*/

zw_internal void zoe_support_init();

// returns 0 on success
zw_internal int zoe_parse_uuid_filename(char const *filename,
                                        size_t filename_size,
                                        ZoeMailStoreFile *zoe_file);

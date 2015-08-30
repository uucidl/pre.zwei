#pragma once

#include <cstdint>

struct ZoeMailStoreFile {
        uint8_t uuid[16];
        uint64_t unix_epoch_millis;
};

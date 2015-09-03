#pragma once

#include <cstdint>

struct ZoeMailStoreFile {
        uint8_t uuid[16];
        uint64_t unix_epoch_millis;
};

struct ByteCountedRange {
        uint8_t *first;
        size_t count;
};

struct RawMailbox {
        ByteCountedRange display_name_bytes;
        ByteCountedRange local_part_bytes;
        ByteCountedRange domain_bytes;
};

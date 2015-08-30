#pragma once

struct ByteCountedRange {
        uint8_t *first;
        size_t count;
};

struct RawMailbox {
        ByteCountedRange display_name_bytes;
        ByteCountedRange local_part_bytes;
        ByteCountedRange domain_bytes;
};

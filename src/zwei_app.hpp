#pragma once

struct BufferRange;

#define CHECK_MIME_MESSAGE(name) void name(struct BufferRange* range)
typedef CHECK_MIME_MESSAGE(CheckMimeMessageFn);

#include <cstdint>

struct ZoeMailStoreFile {
        uint8_t uuid[16];
        uint64_t unix_epoch_millis;
};

#define PARSE_ZOE_MAILSTORE_PATH(name) int name(struct ZoeMailStoreFile* result, char const *filename)
typedef PARSE_ZOE_MAILSTORE_PATH(ParseZoeMailstorePathFn);
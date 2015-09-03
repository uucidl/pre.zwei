#pragma once

#include "zoe_types.hpp"

#include <cstdint>

enum ZweiAppFlags {
        ZWEI_DEBUG_MODE_FLAG = 1 << 0,
};

struct SPDR_Context;

struct Platform {
        SPDR_Context *spdr; // tracing services
};

#define INIT_APP(name) void name(Platform platform, int flags)
typedef INIT_APP(InitAppFn);

struct BufferRange;

#define ACCEPT_MIME_MESSAGE(name)                                              \
        void name(uint8_t *data_first, uint8_t *data_last,                     \
                  ZoeMailStoreFile *zoe_mailstore_file,                        \
                  MemoryArena *message_arena, MemoryArena *result_arena)
typedef ACCEPT_MIME_MESSAGE(AcceptMimeMessageFn);

#define PARSE_ZOE_MAILSTORE_PATH(name)                                         \
        int name(struct ZoeMailStoreFile *result, char const *filename)
typedef PARSE_ZOE_MAILSTORE_PATH(ParseZoeMailstorePathFn);

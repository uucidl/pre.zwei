#pragma once

#include <cstdint>

struct SPDR_Context;

enum ZweiAppFlags {
        ZWEI_DEBUG_MODE_FLAG = 1 << 0,
};

struct Platform {
        SPDR_Context *spdr; // tracing services
};

struct BufferRange;
struct ZoeMailStoreFile;

#define INIT_APP(name) void name(Platform platform, int flags)
typedef INIT_APP(InitAppFn);

#define ACCEPT_MIME_MESSAGE(name)                                              \
        void name(uint8_t *data_first, uint8_t *data_last,                     \
                  ZoeMailStoreFile *zoe_mailstore_file,                        \
                  MemoryArena *message_arena, MemoryArena *result_arena)
typedef ACCEPT_MIME_MESSAGE(AcceptMimeMessageFn);

#define PARSE_ZOE_MAILSTORE_PATH(name)                                         \
        int name(ZoeMailStoreFile *result, char const *filename)
typedef PARSE_ZOE_MAILSTORE_PATH(ParseZoeMailstorePathFn);

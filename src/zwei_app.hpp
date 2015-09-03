#pragma once

#include "zwei_inlines.hpp"
#include "zwei_types.hpp"

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

/**
   @return errorcode (!= 0) or a message summary in message_summary
 */
#define ACCEPT_MIME_MESSAGE(name)                                              \
        int name(uint8_t const *data_first, uint8_t const *data_last,          \
                 ZoeMailStoreFile const *zoe_mailstore_file,                   \
                 MemoryArena *message_arena, MemoryArena *result_arena,        \
                 MessageSummary *message_summary)
typedef ACCEPT_MIME_MESSAGE(AcceptMimeMessageFn);

#define PARSE_ZOE_MAILSTORE_PATH(name)                                         \
        int name(ZoeMailStoreFile *result, char const *filename)
typedef PARSE_ZOE_MAILSTORE_PATH(ParseZoeMailstorePathFn);

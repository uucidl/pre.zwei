#pragma once

#if !defined(ZWEI_API_DLL)
#define ZWEI_API_DLL
#endif
#define ZWEI_API extern "C" ZWEI_API_DLL

#include "zwei_inlines.hpp"
#include "zwei_types.hpp"

#include <cstdint>

struct SPDR_Context;

enum ZweiAppFlags {
        ZWEI_DEBUG_MODE_FLAG = 1 << 0,
};

struct PlatformFileList {
        size_t count;
        char const **paths;
        char const **filenames;
        struct {
                uint64_t size;
        } * attributes;
};

#define PLATFORM_QUERY_ALL_FILES(name)                                         \
        struct PlatformFileList *name(char const *root_dir_path,               \
                                      bool trace_on, MemoryArena work_arena,   \
                                      MemoryArena *result_arena)
typedef PLATFORM_QUERY_ALL_FILES((*PlatformQueryAllFilesFn));
// TODO(nicolas) use pattern above (*name) for all function pointer types

struct Platform {
        SPDR_Context *spdr; // tracing services
        PlatformQueryAllFilesFn query_all_files;
};

struct IOBufferIterator;
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

#define PARSE_ZOE_MAILSTORE_FILENAME(name)                                     \
        int name(ZoeMailStoreFile *result, char const *filename)
typedef PARSE_ZOE_MAILSTORE_FILENAME(ParseZoeMailstoreFilenameFn);

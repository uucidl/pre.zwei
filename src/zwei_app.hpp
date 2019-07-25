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
        struct Entry {
                uint64_t filesize;
                char const *path;
                char const *filename;
        };
        Entry *entries_first;
        size_t entries_size;
};

// TODO(nicolas): trace_on -> enum
#define PLATFORM_QUERY_ALL_FILES(name)                                         \
        struct PlatformFileList *name(char const *root_dir_path,               \
                                      bool trace_on, MemoryArena work_arena,   \
                                      MemoryArena *result_arena)
typedef PLATFORM_QUERY_ALL_FILES((*PlatformQueryAllFilesFn));

#define PLATFORM_QUIT(name) void name(void)
typedef PLATFORM_QUIT((*PlatformQuitFn));

struct Platform {
        SPDR_Context *spdr; // tracing services
        PlatformQueryAllFilesFn query_all_files;
        PlatformQuitFn quit;
};

struct ProgramResources {
        MemoryArena permanent_arena; // memory guaranteed to be preserved across
                                     // app reloads
        MemoryArena transient_arena; // memory that can go away at any time
};

struct BufferedReader;
struct ZoeMailStoreFile;

#define INIT_APP(name)                                                         \
        void name(Platform platform, int flags, ProgramResources *resources)
typedef INIT_APP((*InitAppFn));

/**
   @return errorcode (!= 0) or a message summary in message_summary
 */
#define GET_MESSAGE_SUMMARY(name)                                              \
        int name(uint8_t const *data_first, uint8_t const *data_last,          \
                 ZoeMailStoreFile const *zoe_mailstore_file,                   \
                 MemoryArena *message_arena, MemoryArena *result_arena,        \
                 MessageSummary *message_summary)
typedef GET_MESSAGE_SUMMARY((*GetMessageSummaryFn));

#define PARSE_ZOE_MAILSTORE_FILENAME(name)                                     \
        int name(ZoeMailStoreFile *result, char const *filename)
typedef PARSE_ZOE_MAILSTORE_FILENAME((*ParseZoeMailstoreFilenameFn));

extern SPDR_Context *global_spdr;

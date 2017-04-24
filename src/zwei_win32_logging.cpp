#include "algos.hpp"
#include "win32_kernel.hpp"
#include "zwei_inlines.hpp"
#include "zwei_textoutputgroup.hpp"

// (Ghetto trace) (Trace in an hostile environment)
#include <stdlib.h>
#define TRACE_BEGIN
#define TRACE_END
#if defined(_WIN32)
extern "C" {
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
}
#pragma comment(lib, "Kernel32")
#if defined(_MSC_VER)
#undef TRACE_BEGIN
#undef TRACE_END
#define TRACE_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4996))
#define TRACE_END __pragma(warning(pop))
#endif
#endif
#define TRACE(...)                                                             \
        {                                                                      \
                TRACE_BEGIN                                                    \
                static int fpi;                                                \
                static char fp[4096];                                          \
                if (!fpi) {                                                    \
                        uint32_t pid = GetCurrentProcessId();                  \
                        fpi = sprintf(fp, "trace-%x.txt", pid) >= 0;           \
                }                                                              \
                if (fpi) {                                                     \
                        FILE *y = fopen(fp, "a");                              \
                        if (y) {                                               \
                                fprintf(y, __VA_ARGS__);                       \
                                fclose(y);                                     \
                        }                                                      \
                }                                                              \
                                                                               \
                TRACE_END                                                      \
        }
// (Ghetto trace)

void error_print(char const *message)
{
        char buffer[4096];
        MemoryArena arena = memory_arena(buffer, sizeof(buffer));
        auto out = textoutputgroup_allocate(&arena, 200);
        push_formatted(out, "%s", message);
        error(out);
}

void trace_print(char const *message)
{
        char buffer[4096];
        MemoryArena arena = memory_arena(buffer, sizeof(buffer));
        auto out = textoutputgroup_allocate(&arena, 200);
        push_formatted(out, "%s", message);
        error(out);
}

void text_output_group_print(char const *prefix,
                             int fd,
                             TextOutputGroup const &group)
{
        TRACE("%s", prefix);
        wchar_t out_buffer[4096];
        wchar_t *out_end = out_buffer + sizeof out_buffer;
        wchar_t *out_last = out_buffer;
        auto first_entry = group.first;
        auto const last_entry = group.last;
        algos::for_each(
            first_entry, last_entry,
            [&out_last, out_end](TextOutputGroupEntry &x) {
                    TRACE("%.*s", x.count, x.first);
                    if (out_last != out_end) {
                            out_last += MultiByteToWideChar(
                                Win32CodePage_UTF8, 0,
                                reinterpret_cast<char const *>(x.first),
                                int(x.count), out_last, out_end - out_last);
                    }
            });
        // truncate if necessary
        wchar_t *out_terminator =
            out_last > out_end - 2 ? out_end - 2 : out_last;
        *out_terminator = L'\n';
        ++out_terminator;
        *out_terminator = L'\0';
        ++out_terminator;
        OutputDebugStringW(out_buffer);
        TRACE("\n");
}

void trace(TextOutputGroup *group)
{
        text_output_group_print("TRACE: ", 1, *group);
        clear(group);
}

void error(TextOutputGroup *group)
{
        text_output_group_print("ERROR: ", 2, *group);
        clear(group);
}

void console(TextOutputGroup *group)
{
        // TODO(nicolas): implement console output
        text_output_group_print("", 1, *group);
        clear(group);
}

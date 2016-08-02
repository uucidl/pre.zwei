#include "zwei_app.hpp"
#include "zwei_inlines.hpp"
#include "zwei_logging.hpp"
#include "zwei_text.hpp"
#include "zwei_textapp.cpp"

#include "utf8_decode.cpp"

#include "win32_gdi.hpp"
#include "win32_kernel.hpp"
#include "win32_opengl.hpp"
#include "win32_user.hpp"

#define APIENTRY __stdcall
#define GL_GLEXT_PROTOTYPES 1
#include "../modules/khronos/gl/glcorearb.h"
#include "../modules/nothings_stb/stb_truetype.h"
#include "../modules/uu.spdr/include/spdr/spdr.hh"

#include <cstdint>

/* NOTE(nil): on FindFirstFile and its friends

It is really FindFirstFileExW in disguise

Internally it converts the path to an NT path, and calls the NTdll internals:

- NtOpenFile
- NtQueryDirectoryFile

When using the FindFirstFileA variant, the preambles have to convert the strings
to unicode.

*/

typedef void *HINSTANCE;
typedef char *LPSTR;

// Before:(OpenGL support)
typedef int __stdcall wgl_swap_interval_ext(int interval);
zw_global wgl_swap_interval_ext *wglSwapInterval;
zw_global GLuint OpenGLDefaultInternalTextureFormat;

typedef void *__stdcall wgl_create_context_attribts_arb(void *hDC,
                                                        void *hShareContext,
                                                        const int *attribList);
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126

#define WGL_CONTEXT_DEBUG_BIT_ARB 0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002

#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C

struct opengl_info {
        bool modern_context;

        char *vendor;
        char *renderer;
        char *version;
        char *shading_language_version;
        char *extensions;

        bool GL_EXT_texture_sRGB;
        bool GL_EXT_framebuffer_sRGB;
};

bool ascii_iswhitespace(char x)
{
        return x == ' ' || x == '\t' || x == '\v' || x == '\f' || x == '\n' ||
               x == '\r';
}

zw_internal opengl_info opengl_get_info(bool modern_context)
{
        opengl_info result = {};

        result.modern_context = modern_context;
        result.vendor = (char *)glGetString(GL_VENDOR);
        result.renderer = (char *)glGetString(GL_RENDERER);
        result.version = (char *)glGetString(GL_VERSION);
        if (result.modern_context) {
                result.shading_language_version =
                    (char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
        } else {
                result.shading_language_version = "(none)";
        }

        result.extensions = (char *)glGetString(GL_EXTENSIONS);
        char *first = result.extensions;
        while ('\0' != algos::source(first)) {
                std::pair<char *, char *> range =
                    algos::find_adjacent_if_guarded(first, '\0', [](char x) {
                            return !ascii_iswhitespace(x);
                    });
                if (algos::equal_bounded_guarded(range.first, range.second,
                                                 "GL_EXT_texture_sRGB", '\0',
                                                 std::equal_to<char>())) {
                        result.GL_EXT_texture_sRGB = true;
                } else if (algos::equal_bounded_guarded(
                               range.first, range.second,
                               "GL_EXT_framebuffer_sRGB", '\0',
                               std::equal_to<char>())) {
                        result.GL_EXT_framebuffer_sRGB = true;
                }
                first = range.second;
        }
        return result;
}

zw_internal void opengl_init(bool modern_context)
{
        auto info = opengl_get_info(modern_context);

        OpenGLDefaultInternalTextureFormat = GL_RGBA8;
        if (info.GL_EXT_texture_sRGB) {
                OpenGLDefaultInternalTextureFormat = GL_SRGB8_ALPHA8;
        }
        if (info.GL_EXT_framebuffer_sRGB) {
                glEnable(GL_FRAMEBUFFER_SRGB);
        }
}

zw_internal void win32_init_opengl(void *window)
{
        void *windowDC = GetDC(window);

        Win32PixelFormatDescriptor desired_pixel_format = {};
        desired_pixel_format.nSize = sizeof(desired_pixel_format);
        desired_pixel_format.nVersion = 1;
        desired_pixel_format.iPixelType =
            Win32PixelFormatDescriptorPixelType_RGBA;
        desired_pixel_format.dwFlags =
            Win32PixelFormatDescriptorFlags_SupportOpenGL |
            Win32PixelFormatDescriptorFlags_DrawToWindow |
            Win32PixelFormatDescriptorFlas_DoubleBuffer;
        desired_pixel_format.cColorBits = 32;
        desired_pixel_format.cAlphaBits = 8;
        desired_pixel_format.cDepthBits = 24;
        desired_pixel_format.iLayerType =
            Win32PixelFormatDescriptorLayerType_MainPlane;

        auto suggested_pixel_format_index =
            ChoosePixelFormat(windowDC, &desired_pixel_format);
        Win32PixelFormatDescriptor suggested_pixel_format;
        DescribePixelFormat(windowDC, suggested_pixel_format_index,
                            sizeof(suggested_pixel_format),
                            &suggested_pixel_format);
        SetPixelFormat(windowDC, suggested_pixel_format_index,
                       &suggested_pixel_format);

        void *OpenGLRC = wglCreateContext(windowDC);
        fatal_ifnot(wglMakeCurrent(windowDC, OpenGLRC),
                    "wglMakeCurrent failed");
        auto is_modern_context = false;

        wgl_create_context_attribts_arb *wglCreateContextAttribsARB =
            (wgl_create_context_attribts_arb *)wglGetProcAddress(
                "wglCreateContextAttribsARB");
        if (wglCreateContextAttribsARB) {
                int attribs[] = {
                        WGL_CONTEXT_MAJOR_VERSION_ARB,
                        3,
                        WGL_CONTEXT_MINOR_VERSION_ARB,
                        0,
                        WGL_CONTEXT_FLAGS_ARB,
                        0
#if INTRO_INTERNAL
                            | WGL_CONTEXT_DEBUG_BIT_ARB
#endif
                        ,
                        WGL_CONTEXT_PROFILE_MASK_ARB,
                        WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                        0,
                        WGL_DEPTH_BITS_ARB,
                        24,
#if INTRO_OPENGL_STENCIL_BUFFER
                        WGL_STENCIL_BITS_ARB,
                        8,
#endif
                };

                void *share_context = 0;
                void *modern_glrc = wglCreateContextAttribsARB(
                    windowDC, share_context, attribs);
                if (modern_glrc) {
                        if (wglMakeCurrent(windowDC, modern_glrc)) {
                                is_modern_context = true;
                                wglDeleteContext(OpenGLRC);
                                OpenGLRC = modern_glrc;
                        }
                }
        } else {
                fatal_if(true, "this opengl context is too old");
        }

        opengl_init(is_modern_context);

        wglSwapInterval =
            (wgl_swap_interval_ext *)wglGetProcAddress("wglSwapIntervalEXT");
        if (wglSwapInterval) {
                wglSwapInterval(1);
        }
        ReleaseDC(window, windowDC);
}
// After:(OpenGL Support)

enum RenderGroupCommandType {
        RenderGroupCommand_FlatRect,
};

struct RenderGroupFlatRect {
        float x_min;
        float x_max;
        float y_min;
        float y_max;
};

struct RenderGroupCommand {
        RenderGroupCommandType type;
        union {
                RenderGroupFlatRect rect;
        };
};

struct RenderGroup {
        enum { RenderGroupCommandCapacity = 256,
        };
        RenderGroupCommand buffer[RenderGroupCommandCapacity];
        uint32_t last_command_pos;
};

zw_internal void push_flat_rect(
    RenderGroup *group, float x_min, float x_max, float y_min, float y_max)
{
        fatal_if(group->last_command_pos == countof(group->buffer),
                 "render group over capacity");
        RenderGroupCommand *next_command =
            &group->buffer[group->last_command_pos];
        ++group->last_command_pos;
        next_command->type = RenderGroupCommand_FlatRect;
        next_command->rect.x_min = x_min;
        next_command->rect.x_max = x_max;
        next_command->rect.y_min = y_min;
        next_command->rect.y_max = y_max;
}

void win32_opengl_render(RenderGroup const *render_group)
{
        // TODO(nicolas): implement this
}

struct FontAtlas {
        int32_t width;
        int32_t height;
        uint8_t *data;
};

struct Win32FileMap {
        ByteCountedRange data;
        void *file_handle;
        void *file_mapping_handle;
};

struct Win32FontRenderData {
        enum { FontAtlasCapacity = 64,
        };
        FontAtlas atlases[FontAtlasCapacity];
        Win32FileMap
            font_source; // kept as a source for the data that is not in atlas
        stbtt_fontinfo font;
        // some pre-defined ranges
        size_t basic_latin_chars_atlas_index;
        stbtt_packedchar *basic_latin_chars;
        uint32_t basic_latin_codepoint_first = 0x00;
        uint32_t basic_latin_codepoint_last = 0x80;
};

stbtt_packedchar *find_glyph_data(Win32FontRenderData const *data,
                                  uint32_t codepoint)
{
        uint32_t range_first = data->basic_latin_codepoint_first;
        uint32_t range_last = data->basic_latin_codepoint_last;
        if (codepoint >= range_first && codepoint < range_last) {
                return &data->basic_latin_chars[codepoint - range_first];
        }
        return nullptr;
}

zw_internal SPDR_Context *global_spdr;
zw_internal void *global_main_window;
zw_internal bool global_is_running = true;
zw_internal Win32FontRenderData global_font_render_data;

zw_internal char *cstr_copy(char const *original, MemoryArena *arena)
{
        size_t size = 1 + cstr_len(original);
        char *result = push_array_rvalue(arena, result, size);
        char *result_last = result;
        cstr_cat(result_last, result + size, original);
        fatal_ifnot(cstr_terminate(result_last, result + size),
                    "not enough space");
        return result;
}

zw_internal char *win32_join_filepath(char const *base_path_first,
                                      char const *base_path_last,
                                      char const *filename,
                                      MemoryArena *arena)
{
        size_t const file_path_size = (base_path_last - base_path_first) +
                                      cstr_len("\\") + cstr_len(filename) + 1;
        char *file_path = push_array_rvalue(arena, file_path, file_path_size);
        char *const file_path_end = file_path + file_path_size;
        char *file_path_last = file_path;
        cstr_cat(file_path_last, file_path_end, base_path_first,
                 base_path_last);
        cstr_cat(file_path_last, file_path_end, "\\");
        cstr_cat(file_path_last, file_path_end, filename);
        fatal_ifnot(cstr_terminate(file_path_last, file_path_end),
                    "not enough space for filename");
        return file_path;
};

/*

Drawing text:

input data { utf8 string [ byte_1 .. byte_n ] }
(utf8 decoding)
  -> unicode codepoints { [ codepoint_1, ... codepoint_n ] }
(shaping, which is language specific)
  -> { [ (glyph_index, x, y), ... ] }
(font lookup, using font + fallback font + cache)
  -> { [ font_data, x, y ] }

A primitive form of shaping would take the input's languages natural
direction { left-to-right-descent, right-to-left-descent,
top-bottom-right-left-slide } and each codepoint and advance x,y by the size of
the glyph directly associated with the codepoint.

This is pretty poor for anything that's not western, for instance arab script.
Even for western scripts, it wouldn't produce ligature glyphs.

  TODO(nicolas): look at
URL(https://www.freedesktop.org/wiki/Software/HarfBuzz/) for a shaping library.

 */
zw_internal void win32_draw_text(char const *string_start,
                                 size_t string_size,
                                 RenderGroup *render_group)
{
        char buffer[4096];
        auto buffer_arena = memory_arena(buffer, sizeof buffer);
        auto output = textoutputgroup_allocate(&buffer_arena, 200);

        // TODO(nicolas): word-wrap string between boundaries of the screen
        // TODO(nicolas): allow changing font
        // TODO(nicolas): allow stating screen boundaries and location for the
        // text
        uint32_t utf8_decoder_state = Utf8DecodeResult_Accept;
        uint32_t codepoint = 0;
        char const *prefix = "";
        float x0 = 0.0;
        float y0 = 0.0;
        for (size_t char_index = 0; char_index < string_size; ++char_index) {
                if (Utf8DecodeResult_Accept ==
                    utf8_decode(&utf8_decoder_state, &codepoint,
                                string_start[char_index])) {
                        stbtt_packedchar *glyph_data = find_glyph_data(
                            &global_font_render_data, codepoint);
                        fatal_ifnot(glyph_data, "unknown codepoint");
                        // TODO(nicolas): probably use xoff, yoff here for
                        // kerning
                        push_flat_rect(render_group, x0,
                                       x0 + glyph_data->x1 - glyph_data->x0, y0,
                                       y0 + glyph_data->y1 - glyph_data->y0);
                        x0 += glyph_data->xadvance;
                        // a full code point arrived
                        push_formatted(output, "%s0x%x", prefix, codepoint);
                        prefix = " ";
                }
        }
        trace(output);
}

zw_internal PLATFORM_QUERY_ALL_FILES(win32_query_all_files)
{
        // TODO(nicolas): read file directory
        // URL(https://msdn.microsoft.com/en-us/library/windows/desktop/aa363858(v=vs.85).aspx)
        // URL(https://msdn.microsoft.com/en-us/library/windows/desktop/aa364418(v=vs.85).aspx)
        // FindFirstFile
        //
        // TODO(nicolas): For sorting by physical location, see
        // URL(https://msdn.microsoft.com/en-us/library/windows/desktop/aa364572%28v=vs.85%29.aspx)

        struct FSEntry {
                Win32FindDataA FileAttributes;
                char const *filename;
                char const *path;
                FSEntry *next_entry;
        };
        struct FSEntry entry_free_list;
        entry_free_list.next_entry = &entry_free_list;
        auto new_entry = [&entry_free_list, &work_arena]() {
                FSEntry *result;
                if (entry_free_list.next_entry == &entry_free_list) {
                        result = push_typed(&work_arena, FSEntry);
                } else {
                        result = entry_free_list.next_entry;
                        result->next_entry = nullptr;
                        entry_free_list.next_entry =
                            entry_free_list.next_entry->next_entry;
                }
                return result;
        };
        auto discard_entry = [&entry_free_list](FSEntry *fs_entry) {
                fs_entry->next_entry = entry_free_list.next_entry;
                entry_free_list.next_entry = fs_entry;
        };

        FSEntry directories_to_visit;
        directories_to_visit.next_entry = &directories_to_visit;
        {
                auto e = new_entry();
                e->path = root_dir_path;
                e->next_entry = &directories_to_visit;
                directories_to_visit.next_entry = e;
        }
        size_t file_list_count = 0;
        FSEntry *circular_file_list_last = nullptr;
        while (directories_to_visit.next_entry != &directories_to_visit) {
                auto fs_entry = directories_to_visit.next_entry;
                directories_to_visit.next_entry = fs_entry->next_entry;
                fs_entry->next_entry = nullptr;
                char const *directory_path = fs_entry->path;
                char const *directory_path_last =
                    directory_path + cstr_len(directory_path);
                char query_buffer[4096];
                char *const query = query_buffer;
                {
                        char *query_last = query_buffer;
                        char *const query_buffer_last =
                            query_buffer + sizeof(query_buffer);
                        cstr_cat(query_last, query_buffer_last, directory_path,
                                 directory_path_last);
                        cstr_cat(query_last, query_buffer_last, "\\*");
                        fatal_ifnot(
                            cstr_terminate(query_last, query_buffer_last),
                            "not enough space for directory name");
                }

                Win32FindDataA FileAttributes;
                void *SearchHandle = FindFirstFileA(query, &FileAttributes);
                fatal_ifnot(win32_valid_handle(SearchHandle),
                            "FindFirstFile failed");
                size_t buffer_size = 64 * 1024 * 1024;
                uint8_t *buffer = static_cast<uint8_t *>(
                    HeapAlloc(GetProcessHeap(), 0, buffer_size));
                size_t result_buffer_size = 64 * 1024 * 1024;
                uint8_t *result_buffer = static_cast<uint8_t *>(
                    HeapAlloc(GetProcessHeap(), 0, result_buffer_size));
                do {
                        if (FileAttributes.dwFileAttributes &
                            Win32FileAttributesFlags_Directory) {
                                if (FileAttributes.cFileName[0] != '.') {
                                        auto fs_entry = new_entry();
                                        fs_entry->FileAttributes =
                                            FileAttributes;
                                        fs_entry->path = win32_join_filepath(
                                            directory_path, directory_path_last,
                                            FileAttributes.cFileName,
                                            &work_arena);
                                        fs_entry->next_entry =
                                            directories_to_visit.next_entry;
                                        directories_to_visit.next_entry =
                                            fs_entry;
                                }
                        } else {
                                if (*cstr_find(FileAttributes.cFileName, '~') !=
                                    '\0') {
                                        char buffer[4096];
                                        MemoryArena arena =
                                            memory_arena(buffer, sizeof buffer);
                                        auto out = textoutputgroup_allocate(
                                            &arena, 200);
                                        push_cstr(out, "ignoring file: ");
                                        push_bounded(out, directory_path,
                                                     directory_path_last);
                                        push_cstr(out, "\\");
                                        push_cstr(out,
                                                  FileAttributes.cFileName);
                                        trace(out);
                                } else {
                                        auto fs_entry = new_entry();
                                        fs_entry->FileAttributes =
                                            FileAttributes;
                                        fs_entry->filename =
                                            cstr_copy(FileAttributes.cFileName,
                                                      result_arena);
                                        fs_entry->path = win32_join_filepath(
                                            directory_path, directory_path_last,
                                            FileAttributes.cFileName,
                                            result_arena);
                                        if (!circular_file_list_last) {
                                                circular_file_list_last =
                                                    fs_entry;
                                                fs_entry->next_entry = fs_entry;
                                        } else {
                                                fs_entry->next_entry =
                                                    circular_file_list_last
                                                        ->next_entry;
                                                circular_file_list_last
                                                    ->next_entry = fs_entry;
                                                circular_file_list_last =
                                                    fs_entry;
                                        }
                                        ++file_list_count;
                                }
                        }
                } while (FindNextFileA(SearchHandle, &FileAttributes) != 0);
                fatal_ifnot(WIN32_ERROR_NO_MORE_FILES == GetLastError(),
                            "Directory listing failed");
                fatal_ifnot(FindClose(SearchHandle), "FindClose");
        }
        auto result = push_typed(result_arena, PlatformFileList);
        result->entries_size = file_list_count;
        result->entries_first = push_array_rvalue(
            result_arena, result->entries_first, result->entries_size);
        if (circular_file_list_last) {
                auto const end = circular_file_list_last;
                auto pos = circular_file_list_last;
                size_t file_index = 0;
                do {
                        pos = pos->next_entry;
                        if (trace_on) {
                                trace_print(pos->path);
                        }
                        auto &d_entry = result->entries_first[file_index];
                        d_entry.path = cstr_copy(pos->path, result_arena);
                        d_entry.filename = cstr_copy(
                            pos->FileAttributes.cFileName, result_arena);
                        d_entry.filesize =
                            uint64_t(pos->FileAttributes.nFileSizeLow) +
                            (uint64_t(pos->FileAttributes.nFileSizeHigh) << 8);
                        ++file_index;
                } while (pos != end);
        }
        return result;
}

zw_internal WIN32_WINDOW_PROC(win32_window_proc)
{
        intptr_t result = 0;

        switch (uMsg) {
        case Win32MessageType_Close:
        case Win32MessageType_Destroy: {
                global_is_running = false;
        } break;
        case Win32MessageType_Paint: {
                Win32PaintInfo paint;
                void *DeviceContext = BeginPaint(hwnd, &paint);
                EndPaint(hwnd, &paint);
        } break;
        default:
                result = DefWindowProcA(hwnd, uMsg, wParam, lParam);
                break;
        }
        return result;
}

zw_internal void win32_process_pending_messages(DesktopInputs *d_inputs)
{
        d_inputs->pointer_button_state &=
            ~(DesktopInputsPointerButtonFlags_Transitionned0 |
              DesktopInputsPointerButtonFlags_Transitionned1 |
              DesktopInputsPointerButtonFlags_Transitionned2);
        Win32Message msg;
        while (PeekMessageA(&msg, 0, 0, 0, PeekMessageRemoveFlag_Remove)) {
                switch (msg.message) {
                case Win32MessageType_Quit: {
                        global_is_running = false;
                } break;

                case Win32MessageType_LeftMouseButtonDown: {
                        d_inputs->pointer_button_state |=
                            DesktopInputsPointerButtonFlags_Down0;
                        d_inputs->pointer_button_state |=
                            DesktopInputsPointerButtonFlags_Transitionned0;
                        goto mouse_button;
                }
                case Win32MessageType_RightMouseButtonDown: {
                        d_inputs->pointer_button_state |=
                            DesktopInputsPointerButtonFlags_Down1;
                        d_inputs->pointer_button_state |=
                            DesktopInputsPointerButtonFlags_Transitionned1;
                        goto mouse_button;
                }
                case Win32MessageType_LeftMouseButtonUp: {
                        d_inputs->pointer_button_state &=
                            ~DesktopInputsPointerButtonFlags_Down0;
                        d_inputs->pointer_button_state |=
                            DesktopInputsPointerButtonFlags_Transitionned0;
                        goto mouse_button;
                }
                case Win32MessageType_RightMouseButtonUp: {
                        d_inputs->pointer_button_state &=
                            ~DesktopInputsPointerButtonFlags_Down1;
                        d_inputs->pointer_button_state |=
                            DesktopInputsPointerButtonFlags_Transitionned1;
                        goto mouse_button;
                }
                mouse_button : {
                        uintptr_t ulParam = msg.lParam; // treat as unsigned, in
                                                        // order to bit shift
                        d_inputs->pointer_x = (int16_t)(ulParam & 0xffff);
                        d_inputs->pointer_y =
                            (int16_t)((ulParam >> 16) & 0xffff);
                } break;

                case Win32MessageType_SystemKeyDown:
                case Win32MessageType_SystemKeyUp:
                case Win32MessageType_KeyDown:
                case Win32MessageType_KeyUp: {
                        uint32_t vk_code = uint32_t(msg.wParam);
                        bool was_down = ((msg.lParam & (1 << 30)) != 0);
                        bool is_down = ((msg.lParam & (1 << 31)) == 0);

                        if (is_down) {
                                // process system keys:
                                bool alt_key_was_down =
                                    (msg.lParam & (1 << 29)) != 0;
                                if (vk_code == Win32VirtualKey_F4 &&
                                    alt_key_was_down) {
                                        global_is_running = false;
                                }
                                if (vk_code == Win32VirtualKey_Return &&
                                    alt_key_was_down) {
                                        if (msg.hwnd) {
                                                // Would toggle fullscreen
                                                // win32_toggle_fullscreen(msg.hwnd);
                                        }
                                }
                        }
                } break;

                default: {
                        TranslateMessage(&msg);
                        DispatchMessageA(&msg);
                } break;
                }
        }
}

zw_internal Win32FileMap win32_map_entire_file(const char *file_path)
{
        Win32FileMap result = {};
        auto file_handle = CreateFileA(
            file_path, Win32GenericAccessFlags_Read, Win32ShareModeFlags_Read,
            nullptr, Win32CreationDisposition_OpenExisting, 0, nullptr);
        fatal_ifnot(win32_valid_handle(file_handle), "CreateFile"
                                                     ": opening "
                                                     "file "
                                                     "failed");
        auto file_mapping_handle = CreateFileMappingA(
            file_handle, nullptr, Win32PageProtection_Readonly, 0, 0, nullptr);
        fatal_ifnot(win32_valid_handle(file_mapping_handle),
                    "CreateFileMapping: failed");
        auto data = MapViewOfFile(file_mapping_handle, Win32FileMapAccess_Read,
                                  0, 0, 0);
        uint64_t bytes_size;
        fatal_ifnot(GetFileSizeEx(file_handle, &bytes_size), "GetFileSize");
        result.file_handle = file_handle;
        result.file_mapping_handle = file_mapping_handle;
        result.data.first = static_cast<uint8_t *>(data);
        result.data.count = bytes_size;
        return result;
}

zw_internal void win32_unmap(Win32FileMap file_map)
{
        if (file_map.file_mapping_handle) {
                CloseHandle(file_map.file_mapping_handle);
        }
        if (file_map.file_handle) {
                CloseHandle(file_map.file_handle);
        }
}

int __stdcall WinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine,
                      int nCmdShow)
{
        char exe_filepath_buffer[4096];
        char const *exe_filepath_start;
        size_t exe_filename_size = GetModuleFileNameA(
            0, exe_filepath_buffer, sizeof exe_filepath_buffer);
        exe_filepath_start = exe_filepath_buffer;
        char const *exe_dirname_last = cstr_find_last(exe_filepath_start, '\\');
        fatal_if(exe_dirname_last == exe_filepath_start,
                 "invalid main executable filename");
        {
                // "C:\\Windows\\Fonts\\verdana.ttf"
                auto main_font_file_map =
                    win32_map_entire_file("c:/windows/fonts/arialbd.ttf");
                global_font_render_data.font_source = main_font_file_map;
                ByteCountedRange main_font_data = main_font_file_map.data;
                stbtt_fontinfo main_font;
                auto init_font_result =
                    stbtt_InitFont(&main_font, main_font_data.first, 0);
                fatal_if(init_font_result == 0, "could not read font");

                // Construct a basic font atlas.
                // Should we construct a big atlas at first, or start with
                // a basic one and builds new ones on demand?
                //
                // Even if we went the lazy route, we would need some indexing
                // data structure to go from unicode code point to glyph index.
                int32_t font_atlas_width = 512;
                int32_t font_atlas_height = 512;
                int32_t font_atlas_pixel_size = 1; // 1 8bit channel
                int32_t font_atlas_stride = 0;
                int32_t font_atlas_padding = 1;
                fatal_ifnot(font_atlas_stride == 0 ||
                                font_atlas_stride >= font_atlas_width,
                            "invalid stride");
                int32_t font_atlas_row_size = font_atlas_stride == 0
                                                  ? font_atlas_width
                                                  : font_atlas_stride;
                uint8_t *font_atlas_data = reinterpret_cast<uint8_t *>(
                    HeapAlloc(GetProcessHeap(), 0,
                              font_atlas_row_size * font_atlas_height *
                                  font_atlas_pixel_size));
                stbtt_pack_context pack_context;
                auto pack_result = stbtt_PackBegin(
                    &pack_context, font_atlas_data, font_atlas_width,
                    font_atlas_height, font_atlas_stride, font_atlas_padding,
                    nullptr);
                fatal_ifnot(pack_result == 1, "stbtt_PackBegin failed");
                // TODO(nicolas): load replacement character U+FFFD
                uint32_t basic_latin_codepoint_first = 0x00;
                uint32_t basic_latin_codepoint_last = 0x80;
                uint32_t basic_latin_codepoint_count =
                    basic_latin_codepoint_last - basic_latin_codepoint_first;
                stbtt_packedchar *basic_latin_chars =
                    (stbtt_packedchar *)HeapAlloc(
                        GetProcessHeap(), 0,
                        basic_latin_codepoint_count *
                            sizeof *basic_latin_chars);
                auto font_height_px = 18;
                stbtt_PackFontRange(
                    &pack_context, main_font_data.first, 0, font_height_px,
                    basic_latin_codepoint_first, basic_latin_codepoint_count,
                    basic_latin_chars); // C0 controls + Basic Latin range
                // success or not?
                stbtt_PackEnd(&pack_context);
                global_font_render_data = {};
                {
                        global_font_render_data.font_source =
                            main_font_file_map;
                        global_font_render_data.font = main_font;
                        auto atlas_id = 0;
                        FontAtlas *atlas =
                            &global_font_render_data.atlases[atlas_id];
                        atlas->width = font_atlas_width;
                        atlas->height = font_atlas_height;
                        atlas->data = font_atlas_data;
                        global_font_render_data.basic_latin_chars_atlas_index =
                            atlas_id;
                        global_font_render_data.basic_latin_chars =
                            basic_latin_chars;
                }
        }
        InitAppFn init_app = nullptr;
        GetMessageSummaryFn get_message_summary = nullptr;
        ParseZoeMailstoreFilenameFn parse_zoe_mailstore_filename = nullptr;
        UpdateAndRenderDesktopUIFn update_and_render_desktop_ui = nullptr;
        {
                char library_filename_buffer[4096];
                {
                        char *library_filename_buffer_last =
                            library_filename_buffer +
                            sizeof library_filename_buffer;
                        char *library_filename_last = library_filename_buffer;
                        cstr_cat(library_filename_last,
                                 library_filename_buffer_last,
                                 exe_filepath_start, exe_dirname_last);
                        cstr_cat(library_filename_last,
                                 library_filename_buffer_last, "\\");
                        cstr_cat(library_filename_last,
                                 library_filename_buffer_last, "zwei_app.dll");
                        fatal_ifnot(
                            cstr_terminate(library_filename_last,
                                           library_filename_buffer_last),
                            "not enough space for library name");
                }
                char const *library_filename_start = library_filename_buffer;
                trace_print(library_filename_start);
                auto library_module = LoadLibraryA(library_filename_start);
                fatal_ifnot(library_module, "could not load library");
                init_app = reinterpret_cast<InitAppFn>(
                    GetProcAddress(library_module, "init_app"));
                fatal_ifnot(init_app, "init_app: missing");
                get_message_summary = reinterpret_cast<GetMessageSummaryFn>(
                    GetProcAddress(library_module, "get_message_summary"));
                fatal_ifnot(get_message_summary,
                            "get_message_summary: missing");
                parse_zoe_mailstore_filename =
                    reinterpret_cast<ParseZoeMailstoreFilenameFn>(
                        GetProcAddress(library_module,
                                       "parse_zoe_mailstore_filename"));
                fatal_ifnot(parse_zoe_mailstore_filename,
                            "parze_zoe_mailstore_filename: missing");
                update_and_render_desktop_ui =
                    reinterpret_cast<UpdateAndRenderDesktopUIFn>(GetProcAddress(
                        library_module, "update_and_render_desktop_ui"));
        }

        Platform platform;
        {
                size_t const spdr_buffer_size = 256 * 1024 * 1024;
                spdr_init(&global_spdr,
                          HeapAlloc(GetProcessHeap(), 0, spdr_buffer_size),
                          spdr_buffer_size);
                spdr_enable_trace(global_spdr, 1);
                spdr_set_log_fn(
                    global_spdr,
                    [](const char *line, void *) { trace_print(line); },
                    nullptr);
                platform.spdr = global_spdr;
                platform.query_all_files = win32_query_all_files;
                platform.quit = []() {
                        if (global_is_running) {
                                global_is_running = false;
                        } else {
                                std::exit(0);
                        }
                };
        }
        ProgramResources program_resources;
        {
                size_t buffer_size = 64 * 1024 * 1024;
                uint8_t *buffer = static_cast<uint8_t *>(
                    HeapAlloc(GetProcessHeap(), 0, buffer_size));
                size_t permanent_buffer_size = 64 * 1024 * 1024;
                uint8_t *permanent_buffer = static_cast<uint8_t *>(
                    HeapAlloc(GetProcessHeap(), 0, permanent_buffer_size));
                program_resources.permanent_arena =
                    memory_arena(permanent_buffer, permanent_buffer_size);
                program_resources.transient_arena =
                    memory_arena(buffer, buffer_size);
        }

        Win32WindowClassExA window_class = {};
        {
                window_class.cbSize = sizeof window_class;
                window_class.lpfnWndProc = win32_window_proc;
                window_class.lpszClassName = "Zwei.MainWindow";
                window_class.hCursor = LoadCursorA(nullptr, WIN32_CURSOR_ARROW);
        }
        fatal_ifnot(RegisterClassExA(&window_class),
                    "could not register main window class");
        global_main_window = CreateWindowExA(
            0, window_class.lpszClassName, "Zwei", 0,
            CreateWindowFlags_USE_DEFAULT, CreateWindowFlags_USE_DEFAULT,
            CreateWindowFlags_USE_DEFAULT, CreateWindowFlags_USE_DEFAULT, 0, 0,
            hInstance, 0);
        fatal_ifnot(global_main_window, "couldn't create main window");
        win32_init_opengl(global_main_window);
        ShowWindow(global_main_window, ShowWindowCmd_SHOW);

        // Window main loop
        InvalidateRect(global_main_window, nullptr, true);
        UpdateWindow(global_main_window);

        init_app(platform, ZWEI_DEBUG_MODE_FLAG, &program_resources);

        DesktopInputs program_inputs;
        while (global_is_running) {
                RenderGroup render_group = {};
                // UI main loop
                // TODO(nicolas): this is a UI so we should not eat CPU
                // unless there's a good reason for it. Eating cpu
                // only makes sense when:
                // - in focus and an event occured
                // - in focus and an animation is running
                win32_process_pending_messages(&program_inputs);
                update_and_render_desktop_ui(platform, program_inputs,
                                             &program_resources);
                char const test_string[] = "Hello, World ♪ ♫ ♪";
                win32_draw_text(test_string, sizeof test_string - 1,
                                &render_group);
                win32_opengl_render(&render_group);
        }

        char const *dirname, *dirname_last;
        if (*lpCmdLine) {
                trace_print("Using directory from command Line: ");
                trace_print(lpCmdLine);
                auto last = lpCmdLine + cstr_len(lpCmdLine);
                auto previous = last - 1;
                while (algos::source(previous) == '\\') {
                        last = previous;
                        previous = last - 1;
                }
                dirname = lpCmdLine;
                dirname_last = last;
        } else {
                dirname = exe_filepath_start;
                dirname_last = exe_dirname_last;
        }

        // TODO(nicolas): read file directory
        // URL(https://msdn.microsoft.com/en-us/library/windows/desktop/aa363858(v=vs.85).aspx)
        // URL(https://msdn.microsoft.com/en-us/library/windows/desktop/aa364418(v=vs.85).aspx)
        // FindFirstFile
        PlatformFileList *input_files = nullptr;
        {
                auto temp_arena = program_resources.transient_arena;
                size_t dirpath_len = dirname_last - dirname;
                char *dirpath =
                    push_array_rvalue(&temp_arena, dirpath, 1 + dirpath_len);
                char *dirpath_last = dirpath;
                cstr_cat(dirpath_last, dirpath + dirpath_len + 1, dirname,
                         dirname_last);
                cstr_terminate(dirpath_last, dirpath + dirpath_len + 1);
                input_files = platform.query_all_files(
                    dirpath, false, temp_arena,
                    &program_resources.permanent_arena);
        }
        for (size_t file_index = 0; file_index < input_files->entries_size;
             ++file_index) {
                auto const file_path =
                    input_files->entries_first[file_index].path;
                auto const filename =
                    input_files->entries_first[file_index].filename;
                auto file_mapping = win32_map_entire_file(file_path);
                DEFER(win32_unmap(file_mapping));

                ProcessedMessage message = {};
                Zwei zwei;
                zwei.get_message_summary = get_message_summary;
                zwei.parse_zoe_mailstore_filename =
                    parse_zoe_mailstore_filename;

                process_message(
                    zwei, filename, file_path, file_mapping.data.first,
                    file_mapping.data.first + file_mapping.data.count,
                    program_resources.transient_arena,
                    program_resources.permanent_arena, message);
                print_processed_message(message,
                                        program_resources.transient_arena);
        }
        return 0;
}

#include "zwei_iobuffer.cpp"
#include "zwei_text.cpp"
#include "zwei_win32_logging.cpp"
#define ZWEI_TEXTAPP_IMPLEMENTATION
#include "zwei_textapp.cpp"

#include "secure_hash_standard.cpp"

#define UTF8_DECODE_IMPLEMENTATION
#include "utf8_decode.cpp"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../modules/nothings_stb/stb_truetype.h"

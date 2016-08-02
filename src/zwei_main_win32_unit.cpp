#include "zwei_app.hpp"
#include "zwei_inlines.hpp"
#include "zwei_logging.hpp"
#include "zwei_text.hpp"
#include "zwei_textapp.cpp"

#include "win32_kernel.hpp"

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

SPDR_Context *global_spdr;

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
        InitAppFn init_app;
        GetMessageSummaryFn get_message_summary;
        ParseZoeMailstoreFilenameFn parse_zoe_mailstore_filename;
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
                platform.quit = []() { std::exit(0); };
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

        init_app(platform, ZWEI_DEBUG_MODE_FLAG, &program_resources);

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
                auto file_handle = CreateFileA(
                    file_path, Win32GenericAccessFlags_Read,
                    Win32ShareModeFlags_Read, nullptr,
                    Win32CreationDisposition_OpenExisting, 0, nullptr);
                fatal_ifnot(win32_valid_handle(file_handle), "CreateFile"
                                                             ": opening "
                                                             "file "
                                                             "failed");
                DEFER(CloseHandle(file_handle));
                auto file_mapping_handle = CreateFileMappingA(
                    file_handle, nullptr, Win32PageProtection_Readonly, 0, 0,
                    nullptr);
                fatal_ifnot(win32_valid_handle(file_mapping_handle),
                            "CreateFileMapping: failed");
                DEFER(CloseHandle(file_mapping_handle));
                auto data = MapViewOfFile(file_mapping_handle,
                                          Win32FileMapAccess_Read, 0, 0, 0);

                auto bytes = static_cast<uint8_t *>(data);
                uint64_t bytes_size;
                fatal_ifnot(GetFileSizeEx(file_handle, &bytes_size),
                            "GetFileSize");
                ProcessedMessage message = {};
                Zwei zwei;
                zwei.get_message_summary = get_message_summary;
                zwei.parse_zoe_mailstore_filename =
                    parse_zoe_mailstore_filename;
                process_message(zwei, filename, file_path, bytes,
                                bytes + bytes_size,
                                program_resources.transient_arena,
                                program_resources.permanent_arena, message);
                print_processed_message(message,
                                        program_resources.transient_arena);
        }
        return 0;
}

#include "zwei_text.cpp"
#include "zwei_win32_logging.cpp"
#define ZWEI_TEXTAPP_IMPLEMENTATION
#include "secure_hash_standard.cpp"
#include "zwei_iobuffer.cpp"
#include "zwei_textapp.cpp"

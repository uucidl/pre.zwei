// TODO(nicolas): move to usage of the W versions of the functions?
#pragma once
#include <cstdint>
// SECTION(Windows API) {
// Instead of including windows.h, we put the inline declarations here.
// URL(https://msdn.microsoft.com/fr-fr/library/windows/desktop/hh920508(v=vs.85).aspx)
// URL(https://msdn.microsoft.com/en-us/library/windows/desktop/aa383751(v=vs.85).aspx)
extern "C" {

#define WIN32_ERROR_NO_MORE_FILES (18)
#define WIN32_ERROR_INVALID_PARAMETER (87)
#define WIN32_INVALID_HANDLE_VALUE (reinterpret_cast<void *>(intptr_t(-1)))
#define WIN32_MAX_PATH (260)

enum Win32CodePage : uint32_t {
        Win32CodePage_UTF8 = 65001,
};

enum Win32FileAttributesFlags {
        Win32FileAttributesFlags_Directory = 0x00000010,
};

enum Win32GenericAccessFlags : uint32_t {
        Win32GenericAccessFlags_All = 1u << 28,
        Win32GenericAccessFlags_Execute = 1u << 29,
        Win32GenericAccessFlags_Write = 1u << 30,
        Win32GenericAccessFlags_Read = 1u << 31,
};

enum Win32ShareModeFlags : uint32_t {
        Win32ShareModeFlags_Owned = 0,
        Win32ShareModeFlags_Delete = 0x00000004,
        Win32ShareModeFlags_Read = 0x00000001,
        Win32ShareModeFlags_Write = 0x00000002,
};

enum Win32CreationDisposition : uint32_t {
        Win32CreationDisposition_CreateAlways = 2,
        Win32CreationDisposition_CreateNew = 1,
        Win32CreationDisposition_OpenAlways = 4,
        Win32CreationDisposition_OpenExisting = 3,
        Win32CreationDisposition_TruncateExisting = 5,
};

enum Win32PageProtection : uint32_t {
        Win32PageProtection_ExecuteRead = 0x20,
        Win32PageProtection_ExecuteReadWrite = 0x40,
        Win32PageProtection_ExecuteWriteCopy = 0x80,
        Win32PageProtection_Readonly = 0x02,
        Win32PageProtection_ReadWrite = 0x04,
        Win32PageProtection_WriteCopy = 0x08,
};

enum Win32FileMapAccess : uint32_t {
        Win32FileMapAccess_All = 983071,
        Win32FileMapAccess_Read = 0x4,
        Win32FileMapAccess_Write = 0x2,
        Win32FileMapAccess_Copy = 0x1,
        Win32FileMapAccess_Execute = 0x0020,
};

struct Win32FileTime {
        uint32_t dwLowDateTime;
        uint32_t dwHighDateTime;
};

struct Win32FindDataA {
        uint32_t dwFileAttributes;
        Win32FileTime ftCreationTime;
        Win32FileTime ftLastAccessTime;
        Win32FileTime ftLastWriteTime;
        uint32_t nFileSizeHigh;
        uint32_t nFileSizeLow;
        uint32_t dwReserved0;
        uint32_t dwReserved1;
        char cFileName[WIN32_MAX_PATH];
        char cAlternateFileName[14];
};

struct Win32SecurityAttributes {
        uint32_t nLength;
        void *lpSecurityDescriptor;
        int32_t bInheritHandle;
};

static inline bool win32_valid_handle(void *handle)
{
        return handle && handle != WIN32_INVALID_HANDLE_VALUE;
}

void *__stdcall CreateFileA(
    _In_ char const *lpFileName,
    _In_ uint32_t dwDesiredAccess,
    _In_ uint32_t dwShareMode,
    _In_opt_ Win32SecurityAttributes *lpSecurityAttributes,
    _In_ uint32_t dwCreationDisposition,
    _In_ uint32_t dwFlagsAndAttributes,
    _In_opt_ void *hTemplateFile);

void *__stdcall CreateFileMappingA(
    _In_ void *hFile,
    _In_opt_ Win32SecurityAttributes *lpAttributes,
    _In_ uint32_t flProtect,
    _In_ uint32_t dwMaximumSizeHigh,
    _In_ uint32_t dwMaximumSizeLow,
    _In_opt_ char const *lpName);

int32_t __stdcall CloseHandle(_In_ void *hObject);

int32_t __stdcall FindClose(_Inout_ void *hFindFile);

void *__stdcall FindFirstFileA(_In_ char const *lpFileName,
                               _Out_ Win32FindDataA *lpFindFileData);
int32_t __stdcall FindNextFileA(_In_ void *hFindFile,
                                _Out_ Win32FindDataA *lpFindFileData);

int32_t __stdcall GetFileSizeEx(_In_ void *hFile,
                                _Out_opt_ uint64_t *lpFileSize);

uint32_t __stdcall GetLastError(void);

uint32_t __stdcall GetModuleFileNameA(_In_opt_ void *hModule,
                                      _Out_ char *lpFilename,
                                      _In_ uint32_t nSize);

typedef void(Win32ProcFn)();
Win32ProcFn *__stdcall GetProcAddress(_In_ void *hModule,
                                      _In_ char const *lpProcName);

void *__stdcall GetProcessHeap(void);

void *__stdcall HeapAlloc(_In_ void *hHeap,
                          _In_ uint32_t dwFlags,
                          _In_ size_t dwBytes);

void *__stdcall LoadLibraryA(_In_ char const *lpFileName);

void *__stdcall MapViewOfFile(_In_ void *hFileMappingObject,
                              _In_ uint32_t dwDesiredAccess,
                              _In_ uint32_t dwFileOffsetHigh,
                              _In_ uint32_t dwFileOffsetLow,
                              _In_ size_t dwNumberOfBytesToMap);

int __stdcall MultiByteToWideChar(_In_ unsigned int CodePage,
                                  _In_ uint32_t dwFlags,
                                  _In_ char const *lpMultiByteStr,
                                  _In_ int cbMultiByte,
                                  _Out_opt_ wchar_t *lpWideCharStr,
                                  _In_ int cchWideChar);

void __stdcall OutputDebugStringA(_In_opt_ char const *outputString);
void __stdcall OutputDebugStringW(_In_opt_ wchar_t const *outputString);
}

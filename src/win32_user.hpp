#pragma once
// equivalent to winuser.h (user32.dll)
// Reference for Microsoft's types:
// URL(https://msdn.microsoft.com/en-us/library/windows/desktop/aa383751(v=vs.85).aspx)

#define WIN32_USERAPI __stdcall

#include <cstdint>
extern "C" {
#define WIN32_WINDOW_PROC(name)                                                \
        intptr_t name(_In_ void *hwnd, _In_ uint32_t uMsg,                     \
                      _In_ uintptr_t wParam, _In_ intptr_t lParam)

typedef WIN32_WINDOW_PROC((WIN32_USERAPI * Win32WindowProcFn));

struct Win32Point {
        int32_t x;
        int32_t y;
};

enum Win32VirtualKey : uint8_t {
        Win32VirtualKey_LeftMouseButton = 0x01,
        Win32VirtualKey_RightMouseButton = 0x02,
        Win32VirtualKey_Backspace = 0x08,
        Win32VirtualKey_Tab = 0x09,
        Win32VirtualKey_Return = 0x0D,
        Win32VirtualKey_SHIFT = 0x10,
        Win32VirtualKey_CONTROL = 0x11,
        Win32VirtualKey_ESCAPE = 0x1B,
        Win32VirtualKey_SPACE = 0x20,
        Win32VirtualKey_F4 = 0x73
};

enum Win32MessageType {
        Win32MessageType_LeftMouseButtonDown = 0x0201,
        Win32MessageType_LeftMouseButtonUp = 0x0202,
        Win32MessageType_RightMouseButtonDown = 0x0204,
        Win32MessageType_RightMouseButtonUp = 0x0205,
        Win32MessageType_Close = 0x010,
        Win32MessageType_Destroy = 0x002,
        Win32MessageType_Paint = 0x000F,
        Win32MessageType_Quit = 0x0012,
        Win32MessageType_KeyDown = 0x0100,
        Win32MessageType_KeyUp = 0x0101,
        Win32MessageType_SystemKeyDown = 0x0104,
        Win32MessageType_SystemKeyUp = 0x0105,
};

struct Win32Message {
        void *hwnd;
        uint32_t message;
        uintptr_t wParam;
        intptr_t lParam;
        uint32_t time;
        Win32Point pt;
};

struct Win32Rect {
        int32_t left;
        int32_t top;
        int32_t right;
        int32_t bottom;
};

struct Win32WindowClassExA {
        uint32_t cbSize;
        uint32_t style;
        Win32WindowProcFn lpfnWndProc;
        int32_t cbClsExtra;
        int32_t cbWndExtra;
        void *hInstance;
        void *hIcon;
        void *hCursor;
        void *hbrBackground;
        char const *lpszMenuName;
        char const *lpszClassName;
        void *hIconSm;
};

struct Win32PaintInfo {
        void *hdc;
        int fErase;
        Win32Rect rcPaint;
        int fRestore;
        int fIncUpdate;
        uint8_t rgbReserved[32];
};

enum Win32CreateWindowFlags : int32_t {
        CreateWindowFlags_USE_DEFAULT = 1 << 31,
};

void *WIN32_USERAPI CreateWindowExA(_In_ uint32_t dwExStyle,
                                    _In_opt_ const char *lpClassName,
                                    _In_opt_ const char *lpWindowName,
                                    _In_ uint32_t dwStyle,
                                    _In_ int32_t x,
                                    _In_ int32_t y,
                                    _In_ int32_t nWidth,
                                    _In_ int32_t nHeight,
                                    _In_opt_ void *hWndParent,
                                    _In_opt_ void *hMenu,
                                    _In_opt_ void *hInstance,
                                    _In_opt_ void *lpParam);

void *WIN32_USERAPI BeginPaint(_In_ void *hwnd, _Out_ Win32PaintInfo *lpPaint);

intptr_t WIN32_USERAPI DispatchMessageA(_In_ const Win32Message *lpmsg);

int WIN32_USERAPI EndPaint(_In_ void *hWnd, _In_ const Win32PaintInfo *lpPaint);

void *WIN32_USERAPI GetDC(_In_ void *hWnd);

int WIN32_USERAPI InvalidateRect(_In_ void *hWnd,
                                 _In_ const Win32Rect *lpRect,
                                 _In_ int bErase);

#define WIN32_CURSOR_ARROW                                                     \
        reinterpret_cast<const char *>(uintptr_t(uint16_t(32512)))

void *WIN32_USERAPI LoadCursorA(_In_opt_ void *hInstance,
                                _In_ const char *lpCursorName);

enum PeekMessageRemoveFlag : uint32_t {
        PeekMessageRemoveFlag_None = 0,
        PeekMessageRemoveFlag_Remove = 1,
};

int WIN32_USERAPI PeekMessageA(_Out_ Win32Message *lpMsg,
                               _In_opt_ void *hWnd,
                               _In_ uint32_t wMsgFilterMin,
                               _In_ uint32_t wMsgFilterMax,
                               _In_ uint32_t wRemoveMsg);

WIN32_WINDOW_PROC(WIN32_USERAPI DefWindowProcA);

uint16_t WIN32_USERAPI RegisterClassExA(Win32WindowClassExA const *lpWndClass);

int WIN32_USERAPI ReleaseDC(_In_ void *hWnd, _In_ void *hDC);

enum ShowWindowCmd {
        ShowWindowCmd_SHOW = 5,
};

int WIN32_USERAPI ShowWindow(_In_ void *hWnd, _In_ int32_t nCmdShow);

int WIN32_USERAPI TranslateMessage(_In_ const Win32Message *lpMsg);

int WIN32_USERAPI UpdateWindow(_In_ void *hWnd);
}
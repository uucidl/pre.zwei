#pragma once
#define WIN32_OPENGLAPI __stdcall
// WIN32_LIB(Opengl32)
extern "C" {

void *WIN32_OPENGLAPI wglCreateContext(void *hdc);

Win32ProcFn *WIN32_OPENGLAPI wglGetProcAddress(const char *lpszProc);

int WIN32_OPENGLAPI wglMakeCurrent(void *hdc, void *hglrc);

int WIN32_OPENGLAPI wglDeleteContext(void *hglrc);
}
#pragma once
#define WIN32_GDIAPI __stdcall

#include <cstdint>
extern "C" {

// also known as BITMAPINFOHEADER
struct Win32BitmapInfoHeader {
        uint32_t biSize;
        int32_t biWidth;
        int32_t biHeight;
        uint16_t biPlanes;
        uint16_t biBitCount;
        uint32_t biCompression;
        uint32_t biSizeImage;
        int32_t biXPelsPerMeter;
        int32_t biYPelsPerMeter;
        uint32_t biClrUsed;
        uint32_t biClrImportant;
};

// also known as RGBQUAD
struct Win32RgbQuad {
        uint8_t rgbBlue;
        uint8_t rgbGreen;
        uint8_t rgbRed;
        uint8_t rgbReserved;
};

// also known as BITMAPINFO
struct Win32BitmapInfo {
        Win32BitmapInfoHeader bmiHeader;
        Win32RgbQuad bmiColors[1];
};

enum Win32PixelFormatDescriptorFlags {
        Win32PixelFormatDescriptorFlas_DoubleBuffer = 1 << 0,
        Win32PixelFormatDescriptorFlags_DrawToWindow = 1 << 2,
        Win32PixelFormatDescriptorFlags_SupportOpenGL = 1 << 5,
};

enum Win32PixelFormatDescriptorPixelType {
        Win32PixelFormatDescriptorPixelType_RGBA = 0,
};

enum Win32PixelFormatDescriptorLayerType {
        Win32PixelFormatDescriptorLayerType_MainPlane = 0,
};

// also known as PIXELFORMATDESCRIPTOR
struct Win32PixelFormatDescriptor {
        uint16_t nSize;
        uint16_t nVersion;
        uint32_t dwFlags;
        uint8_t iPixelType;
        uint8_t cColorBits;
        uint8_t cRedBits;
        uint8_t cRedShift;
        uint8_t cGreenBits;
        uint8_t cGreenShift;
        uint8_t cBlueBits;
        uint8_t cBlueShift;
        uint8_t cAlphaBits;
        uint8_t cAlphaShift;
        uint8_t cAccumBits;
        uint8_t cAccumRedBits;
        uint8_t cAccumGreenBits;
        uint8_t cAccumBlueBits;
        uint8_t cAccumAlphaBits;
        uint8_t cDepthBits;
        uint8_t cStencilBits;
        uint8_t cAuxBuffers;
        uint8_t iLayerType;
        uint8_t bReserved;
        uint32_t dwLayerMask;
        uint32_t dwVisibleMask;
        uint32_t dwDamageMask;
};

int WIN32_GDIAPI ChoosePixelFormat(void *hdc,
                                   const Win32PixelFormatDescriptor *ppfd);

int WIN32_GDIAPI DescribePixelFormat(void *hdc,
                                     int iPixelFormat,
                                     unsigned int nBytes,
                                     Win32PixelFormatDescriptor *ppfd);

int WIN32_GDIAPI SetPixelFormat(void *hdc,
                                int iPixelFormat,
                                const Win32PixelFormatDescriptor *ppfd);
}
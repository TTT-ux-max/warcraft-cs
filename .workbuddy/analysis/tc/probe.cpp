// Mirrors the module's link inputs: static CRT, Win32 UI, OpenGL and audio entry points.
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <wincodec.h>

extern "C" __declspec(dllexport) int wcs_toolchain_probe(void) {
    DWORD tick = timeGetTime();
    HDC dc = GetDC(nullptr);
    if (dc) { ReleaseDC(nullptr, dc); }
    IWICImagingFactory* factory = nullptr;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&factory));
    if (factory) { factory->Release(); }
    CoUninitialize();
    return static_cast<int>(tick & 0x7fffffff);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

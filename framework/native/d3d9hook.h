#pragma once
#include <windows.h>
#include <d3d9.h>

namespace d3d9hook {

struct Info {
    HWND hwnd;
    unsigned backW, backH;
    int windowed;
    unsigned refresh;
    unsigned long long presents;
    float fps;
    IDirect3DDevice9 *device;
    int deviceHooked;
    int createDeviceHooked;
    int earlyInstalled;
};
extern Info g_info;

bool InstallEarly(void);   /* hook d3d9!Direct3DCreate9 before renderer module loads */
void Shutdown(void);
void SubclassGameWindows(void);
void LogProcessWindows(const char *tag);
}

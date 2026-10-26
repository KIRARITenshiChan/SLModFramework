#pragma once
#include <windows.h>
struct IDirect3DDevice9;
namespace overlay {
bool Ready();
bool Init(IDirect3DDevice9 *dev, HWND hwnd);
void RenderFrame(void);
void Invalidate(void);
void CreateObjects(void);
void Shutdown(void);
/* Offline UI-only test hooks (no D3D9 device needed): framework/tests/d3d9_harness.cpp --imgui-only. */
bool InitUiOnlyForTest(HWND hwnd);
int RenderUiOnlyForTest(void); /* returns total vertex count of the generated draw data */
int LastVtx(void);        /* vertex count of the last real D3D9 RenderFrame() */
int RenderedFrames(void); /* number of real D3D9 RenderFrame() calls */
/* Development-only scene warp menu (F9): a standalone ImGui window that writes
 * the Fusion "Jump to frame" request via scene::WarpToHandle(). The menu owns
 * input focus (see d3d9hook WndProc) while open. */
void ToggleWarpMenu(void);
bool WarpMenuOpen(void);
}

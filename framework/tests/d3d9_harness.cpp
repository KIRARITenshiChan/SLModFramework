/* Offline Direct3D 9 overlay pipeline test harness (Phase 3).
 *
 * No game involved: this process creates a windowed D3D9 device and drives
 * frames through the exact same MinHook + ImGui path the in-game framework
 * uses (Direct3DCreate9 -> CreateDevice -> IDirect3DDevice9::Present/Reset,
 * WndProc subclass, Insert hotkey toggling, input swallowing).
 *
 * It proves the overlay pipeline independently of the game and of session
 * lock state. Output: Workspace/logs/framework/harness-<ts>.log
 * Exit code: 0 = PASS, 1 = FAIL.
 */
#include "common.h"
#include "d3d9hook.h"
#include "overlay.h"
#include "plugin_host.h"

#include <d3d9.h>
#include <stdio.h>
#include <string.h>

#define HARNESS_FRAMES 200
#define HARNESS_W 1280
#define HARNESS_H 720

static HWND g_harnessWnd;
static int g_origKeys;   /* messages that reached the "game" wndproc (should be 0 while GUI is open) */
static int g_origMouse;

static LRESULT CALLBACK HarnessWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
        g_origKeys++;
    if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN ||
        msg == WM_RBUTTONUP || msg == WM_MOUSEWHEEL)
        g_origMouse++;
    if (msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void PumpMessages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void PostHotkey(void)
{
    PostMessageW(g_harnessWnd, WM_KEYDOWN, (WPARAM)g_fw.hotkeyVK, 0);
    PostMessageW(g_harnessWnd, WM_KEYUP, (WPARAM)g_fw.hotkeyVK, 0);
}

/* UI-only test: builds the exact ImGui UI without a D3D9 device. It validates
 * Begin/End balance, tables, collapsing headers and ImGui asserts even while the
 * Windows session is locked (when no D3D9 device can be created). */
static int RunImGuiOnly(const char *logPath)
{
    Logf("=== imgui-only test begin (no D3D9 device needed) ===");
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HarnessWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = L"SLModHarnessWnd";
    if (!RegisterClassExW(&wc)) {
        Logf("imgui-only| FAIL RegisterClassExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    g_harnessWnd = CreateWindowExW(0, wc.lpszClassName, L"SLModFramework imgui harness",
                                   WS_OVERLAPPEDWINDOW, 160, 160, 800, 600, NULL, NULL,
                                   wc.hInstance, NULL);
    if (!g_harnessWnd) {
        Logf("imgui-only| FAIL CreateWindowExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    ShowWindow(g_harnessWnd, SW_SHOWNORMAL);
    UpdateWindow(g_harnessWnd);
    g_fw.guiVisible = 1;
    g_fw.hudEnabled = 1;
    if (!overlay::InitUiOnlyForTest(g_harnessWnd)) {
        Logf("imgui-only| FAIL overlay::InitUiOnlyForTest");
        DestroyWindow(g_harnessWnd);
        return 1;
    }
    int vtxGui = 0, vtxHud = 0;
    for (int i = 0; i < 3; i++) {
        PumpMessages();
        vtxGui = overlay::RenderUiOnlyForTest();
        Logf("imgui-only| frame %d gui+hud vtx=%d", i, vtxGui);
    }
    g_fw.guiVisible = 0;
    for (int i = 0; i < 2; i++) {
        PumpMessages();
        vtxHud = overlay::RenderUiOnlyForTest();
        Logf("imgui-only| frame %d hud-only vtx=%d", i, vtxHud);
    }
    int pass = (vtxGui > 0) && (vtxHud > 0);
    Logf("SUMMARY3 pass=%d gui_hud_vtx=%d hud_only_vtx=%d", pass, vtxGui, vtxHud);
    Logf("=== imgui-only test end (exit %d) ===", pass ? 0 : 1);
    overlay::Shutdown();
    DestroyWindow(g_harnessWnd);
    if (g_fw.log) { fflush(g_fw.log); fclose(g_fw.log); g_fw.log = NULL; }
    printf("HARNESS imgui-only %s log=%s\n", pass ? "PASS" : "FAIL", logPath);
    return pass ? 0 : 1;
}

static char g_fixturesDir[MAX_PATH];

static void ResolveFixturesDir(void)
{
    char exe[MAX_PATH];
    int i;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    for (i = (int)strlen(exe) - 1; i > 0 && exe[i] != '\\'; --i) {}
    if (i > 0) exe[i] = 0;                     /* drop harness32.exe -> ...\framework\bin */
    wsprintfA(g_fixturesDir, "%s\\..\\tests\\fixtures\\plugins", exe);
}

/* Get() returns a pointer to a shared static View, so copy it before the next
 * lookup (otherwise every lookup aliases the last plugin). */
static int CopyPlugin(const char *id, plugins::View *out)
{
    int n = plugins::Count();
    for (int i = 0; i < n; i++) {
        const plugins::View *v = plugins::Get(i);
        if (v && v->id && strcmp(v->id, id) == 0) {
            *out = *v;
            return 1;
        }
    }
    return 0;
}

/* Plugins-only test: loads the fixture plugin tree through the real host (Lua
 * sandbox included) and renders the GUI so the plugin list and plugin-drawn UI
 * are exercised, all without a D3D9 device. */
static int RunPluginsOnly(const char *logPath)
{
    Logf("=== plugins test begin (no D3D9 device needed) ===");
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HarnessWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = L"SLModHarnessWnd";
    if (!RegisterClassExW(&wc)) {
        Logf("plugins| FAIL RegisterClassExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    g_harnessWnd = CreateWindowExW(0, wc.lpszClassName, L"SLModFramework plugins harness",
                                   WS_OVERLAPPEDWINDOW, 160, 160, 800, 600, NULL, NULL,
                                   wc.hInstance, NULL);
    if (!g_harnessWnd) {
        Logf("plugins| FAIL CreateWindowExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    ShowWindow(g_harnessWnd, SW_SHOWNORMAL);
    UpdateWindow(g_harnessWnd);
    g_fw.guiVisible = 1;
    g_fw.hudEnabled = 0;
    if (!overlay::InitUiOnlyForTest(g_harnessWnd)) {
        Logf("plugins| FAIL overlay::InitUiOnlyForTest");
        DestroyWindow(g_harnessWnd);
        return 1;
    }
    ResolveFixturesDir();
    plugins::SetRootForTest(g_fixturesDir);
    Logf("plugins| fixture root=%s", g_fixturesDir);
    int vtx = 0;
    for (int i = 0; i < 3; i++) {
        PumpMessages();
        vtx = overlay::RenderUiOnlyForTest();
        Logf("plugins| frame %d vtx=%d", i, vtx);
    }
    int total = plugins::Count();
    plugins::View good = {}, mismatch = {}, syntax = {}, disabled = {}, runtime = {}, sandbox = {};
    int hasGood = CopyPlugin("test.good", &good);
    int hasMismatch = CopyPlugin("test.mismatch", &mismatch);
    int hasSyntax = CopyPlugin("test.syntax", &syntax);
    int hasDisabled = CopyPlugin("test.disabled", &disabled);
    int hasRuntime = CopyPlugin("test.runtime", &runtime);
    int hasSandbox = CopyPlugin("test.sandbox", &sandbox);
    int good_ok = hasGood && good.status == plugins::kLoaded && good.updates > 0 && good.errors == 0;
    int mismatch_ok = hasMismatch && mismatch.status == plugins::kApiMismatch && mismatch.error && mismatch.error[0];
    int syntax_ok = hasSyntax && syntax.status == plugins::kError && syntax.error && syntax.error[0];
    int disabled_ok = hasDisabled && disabled.status == plugins::kDisabled && disabled.updates == 0;
    int runtime_ok = hasRuntime && runtime.status == plugins::kLoaded && runtime.errors > 0;
    int sandbox_ok = hasSandbox && sandbox.status == plugins::kLoaded && sandbox.errors == 0 &&
                     sandbox.updates > 0;
    int pass = total == 6 && good_ok && mismatch_ok && syntax_ok && disabled_ok &&
               runtime_ok && sandbox_ok && vtx > 0;
    Logf("SUMMARY4 pass=%d total=%d vtx=%d good=%d/%d/%d mismatch=%d syntax=%d disabled=%d runtime_errors=%d sandbox=%d/%d",
         pass, total, vtx,
         hasGood ? good.status : -1, hasGood ? (int)good.updates : -1, hasGood ? (int)good.errors : -1,
         mismatch_ok, syntax_ok, disabled_ok,
         hasRuntime ? (int)runtime.errors : -1,
         hasSandbox ? sandbox.status : -1, hasSandbox ? (int)sandbox.errors : -1);
    Logf("plugins| host summary: %s", plugins::Summary());
    Logf("=== plugins test end (exit %d) ===", pass ? 0 : 1);
    overlay::Shutdown();
    DestroyWindow(g_harnessWnd);
    if (g_fw.log) { fflush(g_fw.log); fclose(g_fw.log); g_fw.log = NULL; }
    printf("HARNESS plugins %s log=%s\n", pass ? "PASS" : "FAIL", logPath);
    return pass ? 0 : 1;
}

/* WndProc-only test: verifies hotkey toggle + input swallowing without any D3D9
 * device, so it can run even while the Windows session is locked. */
static int RunWndprocOnly(void)
{
    Logf("=== wndproc-only test begin (no D3D9 device needed) ===");
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HarnessWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = L"SLModHarnessWnd";
    if (!RegisterClassExW(&wc)) {
        Logf("wndproc-only| FAIL RegisterClassExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    g_harnessWnd = CreateWindowExW(0, wc.lpszClassName, L"SLModFramework wndproc harness",
                                   WS_OVERLAPPEDWINDOW, 160, 160, 800, 600, NULL, NULL,
                                   wc.hInstance, NULL);
    if (!g_harnessWnd) {
        Logf("wndproc-only| FAIL CreateWindowExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    d3d9hook::SubclassGameWindows();
    d3d9hook::LogProcessWindows("wndproc-only");

    /* 1) Insert toggles the GUI on; the key is swallowed by the hotkey branch. */
    PostMessageW(g_harnessWnd, WM_KEYDOWN, (WPARAM)g_fw.hotkeyVK, 0);
    PostMessageW(g_harnessWnd, WM_KEYUP, (WPARAM)g_fw.hotkeyVK, 0);
    PumpMessages();
    int t1 = (int)g_fw.hotkeyToggles, v1 = (int)g_fw.guiVisible;
    int swKeysAfterToggle = (int)g_fw.swallowedKeys, origKeysAfterToggle = g_origKeys;
    Logf("wndproc-only| toggle#1 toggles=%d gui=%d swallowed_keys=%d orig_keys=%d",
         t1, v1, swKeysAfterToggle, origKeysAfterToggle);

    /* 1b) Keyboard auto-repeat (lParam bit 30 set) must not toggle again. */
    int swBeforeRepeat = (int)g_fw.swallowedKeys;
    PostMessageW(g_harnessWnd, WM_KEYDOWN, (WPARAM)g_fw.hotkeyVK, 0x40000001);
    PumpMessages();
    int t1b = (int)g_fw.hotkeyToggles, v1b = (int)g_fw.guiVisible;
    int swAfterRepeat = (int)g_fw.swallowedKeys;
    Logf("wndproc-only| hotkey-repeat toggles=%d gui=%d swallowed_keys=%d (before=%d)",
         t1b, v1b, swAfterRepeat, swBeforeRepeat);

    /* 2) GUI open: space + mouse must be swallowed and never reach the wndproc. */
    PostMessageW(g_harnessWnd, WM_KEYDOWN, VK_SPACE, 0);
    PostMessageW(g_harnessWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    PostMessageW(g_harnessWnd, WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
    PumpMessages();
    int swKeysOpen = (int)g_fw.swallowedKeys, swMouseOpen = (int)g_fw.swallowedMouse;
    int origKeysOpen = g_origKeys, origMouseOpen = g_origMouse;
    Logf("wndproc-only| gui-open swallowed_keys=%d swallowed_mouse=%d orig_keys=%d orig_mouse=%d",
         swKeysOpen, swMouseOpen, origKeysOpen, origMouseOpen);

    /* 3) Insert toggles the GUI off (hotkey is handled before the swallow logic). */
    PostMessageW(g_harnessWnd, WM_KEYDOWN, (WPARAM)g_fw.hotkeyVK, 0);
    PostMessageW(g_harnessWnd, WM_KEYUP, (WPARAM)g_fw.hotkeyVK, 0);
    PumpMessages();
    int t2 = (int)g_fw.hotkeyToggles, v2 = (int)g_fw.guiVisible;
    Logf("wndproc-only| toggle#2 toggles=%d gui=%d", t2, v2);

    /* 4) GUI closed: space must pass through to the original wndproc. */
    PostMessageW(g_harnessWnd, WM_KEYDOWN, VK_SPACE, 0);
    PumpMessages();
    int swKeysClosed = (int)g_fw.swallowedKeys, origKeysClosed = g_origKeys;
    Logf("wndproc-only| gui-closed swallowed_keys=%d orig_keys=%d", swKeysClosed, origKeysClosed);

    /* 5) WM_CLOSE: framework must flag closing + drop GUI before the game dies. */
    PostMessageW(g_harnessWnd, WM_CLOSE, 0, 0);
    PumpMessages();
    int closing = (int)g_fw.closing, v3 = (int)g_fw.guiVisible;
    Logf("wndproc-only| wm_close closing=%d gui=%d", closing, v3);

    /* TranslateMessage turns the posted WM_KEYDOWN into an extra WM_CHAR, so the
     * absolute counts are 1 (Insert keyup) + 2 (space down + char) per "open" burst.
     * Assert relations instead of brittle literals. */
    int pass = (t1 == 1) && (v1 == 1) && (t1b == 1) && (v1b == 1) &&
               (swAfterRepeat == swBeforeRepeat) && (t2 == 2) && (v2 == 0) &&
               (origKeysAfterToggle == 0) && (swKeysAfterToggle >= 1) &&
               (swKeysOpen == swKeysAfterToggle + 2) && (swMouseOpen == 2) &&
               (origKeysOpen == 0) && (origMouseOpen == 0) &&
               (swKeysClosed == swKeysOpen) && (origKeysClosed >= 2) &&
               (closing == 1) && (v3 == 0);
    Logf("SUMMARY2 pass=%d toggles=%d/%d gui=%d/%d swallowed_keys=%d/%d swallowed_mouse=%d "
         "orig_keys=%d/%d/%d orig_mouse=%d hotkey_repeat=%d/%d/%d",
         pass, t1, t2, v1, v2, swKeysAfterToggle, swKeysAfterToggle + swMouseOpen, swMouseOpen,
         origKeysAfterToggle, origKeysOpen, origKeysClosed, origMouseOpen,
         t1b, v1b, swAfterRepeat - swBeforeRepeat);
    Logf("SUMMARY2-ext closing=%d gui_after_close=%d", closing, v3);
    Logf("=== wndproc-only test end (exit %d) ===", pass ? 0 : 1);

    DestroyWindow(g_harnessWnd);
    if (g_fw.log) { fflush(g_fw.log); fclose(g_fw.log); g_fw.log = NULL; }
    printf("HARNESS wndproc-only %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}


/* P8.2 test: log rotation + diagnostics bundle (no D3D9 device needed). */
static int RunP8Only(const char *logPath)
{
    char rot[MAX_PATH], diag[MAX_PATH];
    const char *res;
    int i, rotated = 0, diagOk = 0;
    Logf("=== p8-only test begin (log rotation + diagnostics bundle) ===");
    _putenv("SLMODF_LOG_ROTATE_BYTES=8192");
    LogSetPath(logPath);
    for (i = 0; i < 400; i++)
        Logf("p8| rotation filler line %04d padding padding padding padding padding padding", i);
    wsprintfA(rot, "%s.1", logPath);
    rotated = GetFileAttributesA(rot) != INVALID_FILE_ATTRIBUTES;
    Logf("p8| rotation triggered=%d rotated_file=%s", rotated, rot);
    res = ExportDiagnostics();
    lstrcpynA(diag, res ? res : "", MAX_PATH);
    if (diag[0]) {
        HANDLE h = CreateFileA(diag, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz;
            diagOk = GetFileSizeEx(h, &sz) && sz.QuadPart > 200;
            CloseHandle(h);
        }
    }
    int pass = rotated && diagOk;
    Logf("SUMMARY5 pass=%d rotated=%d diag_ok=%d diag=%s", pass, rotated, diagOk, diag);
    Logf("=== p8-only test end (exit %d) ===", pass ? 0 : 1);
    if (g_fw.log) { fflush(g_fw.log); fclose(g_fw.log); g_fw.log = NULL; }
    printf("HARNESS p8-only %s diag=%s\n", pass ? "PASS" : "FAIL", diag);
    return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
    char logPath[MAX_PATH];
    SYSTEMTIME st;

    /* Framework global state: only what the harness path actually touches. */
    memset(&g_fw, 0, sizeof(g_fw));
    g_fw.pid = GetCurrentProcessId();
    lstrcpynA(g_fw.exePath, "C:\\harness\\SisterLocation.exe", MAX_PATH);
    lstrcpynA(g_fw.exeDir, "C:\\harness", MAX_PATH);
    /* Resolve the log dir relative to the executable (bin/harness32.exe ->
     * Workspace/logs/framework) so evidence stays in the Workspace tree no
     * matter which cwd the harness is started from. */
    {
        char exe[MAX_PATH];
        int i;
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        for (i = (int)strlen(exe) - 1; i > 0 && exe[i] != '\\'; --i) {}
        if (i > 0) exe[i] = 0;                 /* drop "harness32.exe" -> ...\bin */
        for (i = (int)strlen(exe) - 1; i > 0 && exe[i] != '\\'; --i) {}
        if (i > 0) exe[i] = 0;                 /* drop "bin"           -> ...\framework */
        for (i = (int)strlen(exe) - 1; i > 0 && exe[i] != '\\'; --i) {}
        if (i > 0) exe[i] = 0;                 /* drop "framework"     -> ...\Workspace */
        wsprintfA(g_fw.logDir, "%s\\logs\\framework", exe);
    }
    lstrcpynA(g_fw.cfgPath, "C:\\harness\\Workspace\\config\\framework.ini", MAX_PATH);
    lstrcpynA(g_fw.sha256, "<harness>", sizeof(g_fw.sha256));
    g_fw.hotkeyVK = VK_INSERT;
    g_fw.hudEnabled = 1;
    g_fw.hookD3D9 = 1;
    g_fw.mmfs2WaitMs = 60000;

    /* Logging setup (same layout as the in-game framework logs). */
    EnsureDirA(g_fw.logDir);
    GetLocalTime(&st);
    wsprintfA(logPath, "%s\\harness-%04u%02u%02u-%02u%02u%02u.log", g_fw.logDir,
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_fw.log = fopen(logPath, "wb");
    if (!g_fw.log) {
        printf("HARNESS: cannot open log %s\n", logPath);
        return 1;
    }

    Logf("=== D3D9 overlay harness begin (SLModFramework %s) ===", SLMODF_VERSION);
    Logf("harness| pid=%lu log=%s", (unsigned long)GetCurrentProcessId(), logPath);
    LogSetPath(logPath);

    if (argc > 1 && strcmp(argv[1], "--wndproc-only") == 0)
        return RunWndprocOnly();
    if (argc > 1 && strcmp(argv[1], "--imgui-only") == 0)
        return RunImGuiOnly(logPath);
    if (argc > 1 && strcmp(argv[1], "--plugins") == 0)
        return RunPluginsOnly(logPath);
    if (argc > 1 && strcmp(argv[1], "--p8-only") == 0)
        return RunP8Only(logPath);

    int early = d3d9hook::InstallEarly() ? 1 : 0;
    Logf("harness| early install result=%d", early);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HarnessWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = L"SLModHarnessWnd";
    if (!RegisterClassExW(&wc)) {
        Logf("harness| FAIL RegisterClassExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    g_harnessWnd = CreateWindowExW(0, wc.lpszClassName, L"SLModFramework offline harness",
                                   WS_OVERLAPPEDWINDOW, 120, 120, HARNESS_W, HARNESS_H,
                                   NULL, NULL, wc.hInstance, NULL);
    if (!g_harnessWnd) {
        Logf("harness| FAIL CreateWindowExW err=%lu", (unsigned long)GetLastError());
        return 1;
    }
    ShowWindow(g_harnessWnd, SW_SHOWNORMAL);
    UpdateWindow(g_harnessWnd);
    Logf("harness| window hwnd=0x%08lX class=SLModHarnessWnd", (unsigned long)(ULONG_PTR)g_harnessWnd);

    d3d9hook::SubclassGameWindows();
    d3d9hook::LogProcessWindows("pre_device");

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        Logf("harness| FAIL Direct3DCreate9 returned null");
        return 1;
    }
    Logf("harness| Direct3DCreate9 ok (createDeviceHook=%d)", d3d9hook::g_info.createDeviceHooked);
    {
        HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
        Logf("harness| input desktop handle=%p (null => workstation locked / secure desktop)",
             (void *)desk);
        if (desk) CloseDesktop(desk);
    }
    UINT adapters = d3d->GetAdapterCount();
    Logf("harness| adapter_count=%u", adapters);
    for (UINT a = 0; a < adapters && a < 4; a++) {
        D3DADAPTER_IDENTIFIER9 id;
        memset(&id, 0, sizeof(id));
        if (SUCCEEDED(d3d->GetAdapterIdentifier(a, 0, &id)))
            Logf("harness| adapter %u desc=%s vendor=0x%04X device=0x%04X driver=%s",
                 a, id.Description, id.VendorId, id.DeviceId, id.Driver);
    }

    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.hDeviceWindow = g_harnessWnd;
    pp.BackBufferWidth = HARNESS_W;
    pp.BackBufferHeight = HARNESS_H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9 *dev = NULL;
    D3DDEVTYPE usedType = D3DDEVTYPE_HAL;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_harnessWnd,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) {
        Logf("harness| HAL CreateDevice failed hr=0x%08lX, retrying with D3DDEVTYPE_REF",
             (unsigned long)hr);
        usedType = D3DDEVTYPE_REF;
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, g_harnessWnd,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    }
    if (FAILED(hr) || !dev) {
        Logf("harness| SKIP CreateDevice hr=0x%08lX (no usable D3D9 device in this session)",
             (unsigned long)hr);
        Logf("harness| SKIP hint: unlock the Windows session and re-run; HAL devices are not "
             "available on the secure/locked desktop");
        Logf("=== D3D9 overlay harness end (exit 2 = SKIP) ===");
        if (d3d) d3d->Release();
        DestroyWindow(g_harnessWnd);
        if (g_fw.log) { fflush(g_fw.log); fclose(g_fw.log); g_fw.log = NULL; }
        printf("HARNESS SKIP (no D3D9 device) log=%s\n", logPath);
        return 2;
    }
    Logf("harness| CreateDevice ok type=%d device=%p hookDeviceHooked=%d",
         (int)usedType, (void *)dev, d3d9hook::g_info.deviceHooked);

    int resetOk = 0;
    for (int i = 0; i < HARNESS_FRAMES; i++) {
        PumpMessages();
        dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB((UINT)(i & 0xFF), 40, 90), 1.0f, 0);
        dev->Present(NULL, NULL, NULL, NULL);

        if (i == 30 || i == 120) {
            PostHotkey();
            Logf("harness| frame %d posted VK_INSERT", i);
        }
        if (i == 45) {
            PostMessageW(g_harnessWnd, WM_KEYDOWN, VK_SPACE, 0);
            PostMessageW(g_harnessWnd, WM_KEYUP, VK_SPACE, 0);
            Logf("harness| frame %d posted WM_KEYDOWN/UP space", i);
        }
        if (i == 50) {
            PostMessageW(g_harnessWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            PostMessageW(g_harnessWnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
            Logf("harness| frame %d posted WM_LBUTTONDOWN/UP", i);
        }
        if (i == 100) {
            hr = dev->Reset(&pp);
            resetOk = SUCCEEDED(hr) ? 1 : 0;
            Logf("harness| frame %d Reset hr=0x%08lX ok=%d", i, (unsigned long)hr, resetOk);
        }
        Sleep(10);
    }
    PumpMessages();
    Sleep(100);
    PumpMessages();

    int presents = (int)d3d9hook::g_info.presents;
    int toggles = (int)g_fw.hotkeyToggles;
    int keys = (int)g_fw.swallowedKeys;
    int mouse = (int)g_fw.swallowedMouse;
    int ready = overlay::Ready() ? 1 : 0;
    int devHooked = d3d9hook::g_info.deviceHooked;
    int cdHooked = d3d9hook::g_info.createDeviceHooked;
    int fpsMilli = (int)(d3d9hook::g_info.fps * 1000.0f);
    int drawn = overlay::RenderedFrames();
    int vtx = overlay::LastVtx();

    int pass = (presents >= 150) && (toggles >= 2) && (keys >= 1) && (mouse >= 1) &&
               ready && devHooked && cdHooked && early && resetOk && (drawn >= 100) && (vtx > 0);

    Logf("SUMMARY pass=%d presents=%d toggles=%d swallowed_keys=%d swallowed_mouse=%d "
         "ready=%d device_hooked=%d create_device_hooked=%d early=%d reset_ok=%d "
         "backbuffer=%dx%d windowed=%d fps_milli=%d drawn=%d vtx=%d",
         pass, presents, toggles, keys, mouse, ready, devHooked, cdHooked, early, resetOk,
         (int)d3d9hook::g_info.backW, (int)d3d9hook::g_info.backH,
         d3d9hook::g_info.windowed ? 1 : 0, fpsMilli, drawn, vtx);
    Logf("=== D3D9 overlay harness end (exit %d) ===", pass ? 0 : 1);

    overlay::Shutdown();
    d3d9hook::Shutdown();
    if (dev) dev->Release();
    if (d3d) d3d->Release();
    DestroyWindow(g_harnessWnd);
    if (g_fw.log) {
        fflush(g_fw.log);
        fclose(g_fw.log);
        g_fw.log = NULL;
    }
    printf("HARNESS %s log=%s\n", pass ? "PASS" : "FAIL", logPath);
    return pass ? 0 : 1;
}

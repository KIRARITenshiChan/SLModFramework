/* P3: D3D9 hooks (Direct3DCreate9 -> CreateDevice -> Present/Reset) + WndProc subclassing.
 * Installed only after mmfs2.dll is loaded (worker thread), never from DllMain.
 */
#include "common.h"
#include "d3d9hook.h"
#include "overlay.h"
#include "state.h"
#include "MinHook.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#include <d3d9.h>
#include <mmsystem.h>
#include <string.h>

namespace d3d9hook {
Info g_info;
}

using d3d9hook::g_info;

typedef IDirect3D9 *(WINAPI *Direct3DCreate9_t)(UINT);
typedef HRESULT (WINAPI *CreateDevice_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT (WINAPI *Present_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT (WINAPI *Reset_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);

static Direct3DCreate9_t g_origCreate9;
static CreateDevice_t g_origCreateDevice;
static Present_t g_origPresent;
static Reset_t g_origReset;
static HMODULE g_hD3D9;
static int g_mhInit;
static int g_d3d9VtblHooked;

static HRESULT WINAPI Hook_Present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty);
static HRESULT WINAPI Hook_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp);

typedef BOOL (WINAPI *ClipCursor_t)(const RECT *);
typedef BOOL (WINAPI *SetCursorPos_t)(int, int);
typedef int (WINAPI *ShowCursor_t)(BOOL);

static ClipCursor_t g_origClipCursor;
static SetCursorPos_t g_origSetCursorPos;
static ShowCursor_t g_origShowCursor;
static RECT g_qolSavedClip;
static int g_qolSavedClipValid;
static int g_qolReleased;
static int g_qolForcedShow;
static void *g_qolClipTarget;
static void *g_qolSetPosTarget;
static void *g_qolShowTarget;

/* P8 QoL: FNAF SL clips the cursor and recenters it every frame, so alt-tab
 * cannot free the mouse. Release the clip while the game is not the foreground
 * window or while our GUI is open, and restore the saved clip rect on return. */
static int QolGameIsForeground(void)
{
    HWND fg, a, b;
    if (!g_fw.hwnd || !IsWindow(g_fw.hwnd)) return 1;
    fg = GetForegroundWindow();
    if (!fg) return 1; /* desktop switch / no focus: cannot prove another app owns it */
    a = GetAncestor(fg, GA_ROOT);
    b = GetAncestor(g_fw.hwnd, GA_ROOT);
    if (a && b) return a == b;
    return fg == g_fw.hwnd;
}

static int QolShouldRelease(void)
{
    if (!g_fw.qolReleaseMouse) return 0;
    if (g_fw.guiVisible) return 1;
    if (overlay::WarpMenuOpen()) return 1; /* F9 dev warp menu owns the mouse too */
    return !QolGameIsForeground();
}

static void QolUpdate(void)
{
    int release = QolShouldRelease();
    if (release == g_qolReleased) return;
    g_qolReleased = release;
    if (release) {
        if (g_origClipCursor) g_origClipCursor(NULL);
        if (g_origShowCursor && !g_qolForcedShow) {
            g_qolForcedShow = 1;
            g_origShowCursor(TRUE);
        }
        Logf("qol| mouse released gui=%ld foreground=%d", (long)g_fw.guiVisible, QolGameIsForeground());
    } else {
        if (g_qolSavedClipValid && g_origClipCursor) {
            g_origClipCursor(&g_qolSavedClip);
            g_qolSavedClipValid = 0;
        }
        if (g_qolForcedShow && g_origShowCursor) {
            g_qolForcedShow = 0;
            g_origShowCursor(FALSE);
        }
        Logf("qol| mouse captured again");
    }
}

static BOOL WINAPI Hook_ClipCursor(const RECT *r)
{
    if (QolShouldRelease()) {
        if (r) { g_qolSavedClip = *r; g_qolSavedClipValid = 1; }
        return g_origClipCursor ? g_origClipCursor(NULL) : TRUE;
    }
    if (r) { g_qolSavedClip = *r; g_qolSavedClipValid = 1; }
    return g_origClipCursor ? g_origClipCursor(r) : TRUE;
}

static BOOL WINAPI Hook_SetCursorPos(int x, int y)
{
    if (QolShouldRelease()) return TRUE;
    return g_origSetCursorPos ? g_origSetCursorPos(x, y) : TRUE;
}

static int WINAPI Hook_ShowCursor(BOOL show)
{
    if (QolShouldRelease()) {
        if (!show) {
            if (!g_qolForcedShow) {
                g_qolForcedShow = 1;
                if (g_origShowCursor) return g_origShowCursor(TRUE);
            }
            return 1;
        }
        return g_origShowCursor ? g_origShowCursor(show) : show;
    }
    if (g_qolForcedShow && !show) {
        g_qolForcedShow = 0;
        return g_origShowCursor ? g_origShowCursor(FALSE) : -1;
    }
    return g_origShowCursor ? g_origShowCursor(show) : (show ? 1 : 0);
}

static int QolInstallHooks(void)
{
    HMODULE u = GetModuleHandleA("user32.dll");
    int n = 0;
    if (!u) return 0;
    g_qolClipTarget = (void *)GetProcAddress(u, "ClipCursor");
    g_qolSetPosTarget = (void *)GetProcAddress(u, "SetCursorPos");
    g_qolShowTarget = (void *)GetProcAddress(u, "ShowCursor");
    if (g_qolClipTarget && MH_CreateHook(g_qolClipTarget, (void *)Hook_ClipCursor, (void **)&g_origClipCursor) == MH_OK &&
        MH_EnableHook(g_qolClipTarget) == MH_OK) n++;
    if (g_qolSetPosTarget && MH_CreateHook(g_qolSetPosTarget, (void *)Hook_SetCursorPos, (void **)&g_origSetCursorPos) == MH_OK &&
        MH_EnableHook(g_qolSetPosTarget) == MH_OK) n++;
    if (g_qolShowTarget && MH_CreateHook(g_qolShowTarget, (void *)Hook_ShowCursor, (void **)&g_origShowCursor) == MH_OK &&
        MH_EnableHook(g_qolShowTarget) == MH_OK) n++;
    Logf("qol| cursor hooks installed=%d/3 release_mouse=%d clip=%p setpos=%p showcursor=%p",
         n, g_fw.qolReleaseMouse, g_qolClipTarget, g_qolSetPosTarget, g_qolShowTarget);
    return n;
}

#define MAX_SUBCLASS 16
static struct { HWND hwnd; WNDPROC orig; } g_sub[MAX_SUBCLASS];
static int g_subCount;
static int g_subPassCount;
static CRITICAL_SECTION g_subCs;
static volatile LONG g_subCsInit;

static void EnsureSubCs(void)
{
    if (InterlockedCompareExchange(&g_subCsInit, 1, 0) == 0)
        InitializeCriticalSection(&g_subCs);
}

static WNDPROC FindOrig(HWND hwnd)
{
    int i;
    WNDPROC p = NULL;
    EnsureSubCs();
    EnterCriticalSection(&g_subCs);
    for (i = 0; i < g_subCount; i++) {
        if (g_sub[i].hwnd == hwnd) { p = g_sub[i].orig; break; }
    }
    LeaveCriticalSection(&g_subCs);
    return p;
}

static LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

static int AddSubclass(HWND hwnd)
{
    WNDPROC orig;
    char cls[128];
    if (!hwnd || !IsWindow(hwnd)) return 0;
    EnsureSubCs();
    EnterCriticalSection(&g_subCs);
    if (g_subCount >= MAX_SUBCLASS) { LeaveCriticalSection(&g_subCs); return 0; }
    {
        int i, dup = 0;
        for (i = 0; i < g_subCount; i++) if (g_sub[i].hwnd == hwnd) dup = 1;
        if (dup) { LeaveCriticalSection(&g_subCs); return 0; }
    }
    orig = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)HookWndProc);
    if (!orig) { LeaveCriticalSection(&g_subCs); return 0; }
    g_sub[g_subCount].hwnd = hwnd;
    g_sub[g_subCount].orig = orig;
    g_subCount++;
    LeaveCriticalSection(&g_subCs);
    cls[0] = 0;
    GetClassNameA(hwnd, cls, sizeof(cls));
    Logf("wndproc| subclassed hwnd=0x%08lX class=%s orig=%p",
         (unsigned long)(ULONG_PTR)hwnd, cls, (void *)orig);
    return 1;
}

void d3d9hook::SubclassGameWindows(void)
{
    HWND top = NULL;
    int added = 0;
    DWORD pid = GetCurrentProcessId();
    EnsureSubCs();
    for (;;) {
        DWORD wpid = 0;
        top = FindWindowExW(NULL, top, NULL, NULL);
        if (!top) break;
        GetWindowThreadProcessId(top, &wpid);
        if (wpid != pid) continue;
        if (GetWindow(top, GW_OWNER)) continue;
        added += AddSubclass(top);
        {
            HWND child = NULL;
            for (;;) {
                child = FindWindowExW(top, child, NULL, NULL);
                if (!child) break;
                added += AddSubclass(child);
            }
        }
    }
    g_subPassCount++;
    if (added || g_subPassCount <= 3)
        Logf("wndproc| subclass pass #%d added=%d total=%d", g_subPassCount, added, g_subCount);
}

void d3d9hook::LogProcessWindows(const char *tag)
{
    HWND top = NULL;
    DWORD pid = GetCurrentProcessId();
    int n = 0;
    for (;;) {
        char cls[128];
        DWORD wpid = 0;
        top = FindWindowExW(NULL, top, NULL, NULL);
        if (!top) break;
        GetWindowThreadProcessId(top, &wpid);
        if (wpid != pid) continue;
        cls[0] = 0;
        GetClassNameA(top, cls, sizeof(cls));
        Logf("window| %s top hwnd=0x%08lX class=%s visible=%d", tag,
             (unsigned long)(ULONG_PTR)top, cls, IsWindowVisible(top) ? 1 : 0);
        n++;
        {
            HWND child = NULL;
            for (;;) {
                child = FindWindowExW(top, child, NULL, NULL);
                if (!child) break;
                cls[0] = 0;
                GetClassNameA(child, cls, sizeof(cls));
                Logf("window| %s child hwnd=0x%08lX class=%s visible=%d", tag,
                     (unsigned long)(ULONG_PTR)child, cls, IsWindowVisible(child) ? 1 : 0);
                n++;
            }
        }
    }
    Logf("window| %s total=%d", tag, n);
}

static void LogClass(HWND hwnd, const char *tag)
{
    char cls[128];
    cls[0] = 0;
    if (hwnd) GetClassNameA(hwnd, cls, sizeof(cls));
    Logf("%s hwnd=0x%08lX class=%s visible=%d", tag, (unsigned long)(ULONG_PTR)hwnd, cls,
         hwnd ? IsWindowVisible(hwnd) : 0);
}

#define IS_KEY_MSG(m) ((m) == WM_KEYDOWN || (m) == WM_KEYUP || (m) == WM_CHAR || \
                       (m) == WM_SYSKEYDOWN || (m) == WM_SYSKEYUP || (m) == WM_SYSCHAR || \
                       (m) == WM_DEADCHAR || (m) == WM_UNICHAR || (m) == WM_IME_CHAR)
#define IS_MOUSE_MSG(m) ((m) == WM_MOUSEMOVE || (m) == WM_LBUTTONDOWN || (m) == WM_LBUTTONUP || \
                         (m) == WM_LBUTTONDBLCLK || (m) == WM_RBUTTONDOWN || (m) == WM_RBUTTONUP || \
                         (m) == WM_RBUTTONDBLCLK || (m) == WM_MBUTTONDOWN || (m) == WM_MBUTTONUP || \
                         (m) == WM_MBUTTONDBLCLK || (m) == WM_MOUSEWHEEL || (m) == WM_MOUSEHWHEEL || \
                         (m) == WM_XBUTTONDOWN || (m) == WM_XBUTTONUP || (m) == WM_XBUTTONDBLCLK || \
                         (m) == WM_MOUSEACTIVATE)

/* ---- P8 QoL input bridges (state injection, no synthetic mouse/keyboard into the game) ---- */

/* Frame 19 keypad: the game reads mouse clicks only, so a numpad digit writes
 * the same "answer" counter a correct click would write. Guards are evaluated
 * live against the game memory because prompt/answer can change within one
 * 250 ms state poll; writing during the prompt==0 gap counts as a wrong answer. */
static int QolKeypadTry(WPARAM vk)
{
    int digit, se = 0, prompt = 0, answer = 0, chain = 0, got = 0;
    if (!g_fw.qolKeypadNumpad) return 0;
    if (vk < VK_NUMPAD1 || vk > VK_NUMPAD9) return 0;
    digit = (int)(vk - VK_NUMPAD0);
    if (state::SceneIndex() != 19) return 0;
    if (!state::ReadCounterLive("Script Event", &se) || se != 607) return 0;
    if (!state::ReadCounterLive("prompt", &prompt) || prompt < 1 || prompt > 9) return 0;
    if (!state::ReadCounterLive("answer", &answer) || answer != 0) return 0;
    if (!state::ReadCounterLive("chain", &chain) || chain >= 10) return 0;
    if (state::ReadCounterLive("got you", &got) && got != 0) return 0;
    if (!state::WriteCounter("answer", digit)) {
        Logf("qol| keypad write failed digit=%d prompt=%d chain=%d", digit, prompt, chain);
        return 0;
    }
    Logf("qol| keypad numpad digit=%d prompt=%d chain=%d vk=0x%02X", digit, prompt, chain, (unsigned)vk);
    return 1;
}

/* The game plays Speaker sample #298 "Doorslam" from the frame-27/31 door
 * events. Our state injection never runs those event actions, so we play the
 * extracted local WAV through winmm instead. The asset is seeded from the
 * user's own game files (<framework>\assets\sfx\Doorslam.wav) and is deliberately
 * not part of a distributable package; when it is missing the hotkeys stay
 * silent but functional. */
static void QolDoorSfx(void)
{
    static int tried, found;
    static char path[MAX_PATH];
    if (!g_fw.qolDoorSfx) return;
    if (!tried) {
        tried = 1;
        if (g_fw.frameworkDir[0]) {
            wsprintfA(path, "%s\\assets\\sfx\\Doorslam.wav", g_fw.frameworkDir);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) found = 1;
            Logf("qol| door sfx path=%s found=%d", path, found);
        }
    }
    if (!found) return;
    if (!PlaySoundA(path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
        Logf("qol| door sfx PlaySound failed err=%lu", GetLastError());
}

static int QolPrivateDoor(const char *door, const char *button, const char *closed)
{
    int isClosed = 0;
    if (!state::ReadCounterLive(closed, &isClosed)) return 0;
    if (isClosed) {
        if (!state::WriteAlterable(door, 0, 0)) return 0;
        if (!state::WriteAlterable(button, 0, 0)) return 0;
        if (!state::WriteCounter(closed, 0)) return 0;
        state::WriteCounter("cooldown", 10);
        /* Force the open sequence (act #17 = 14) so the door sprite follows the
         * logic state instead of snapping; Group 48 finishes the transition. */
        if (state::WriteAnimation(door, 14)) QolDoorSfx();
        else Logf("qol| private %s anim force failed (open)", door);
        Logf("qol| private %s -> open", door);
    } else {
        if (!state::WriteAlterable(door, 0, 2)) return 0;
        if (!state::WriteAlterable(button, 0, 1)) return 0;
        if (!state::WriteCounter(closed, 1)) return 0;
        state::WriteCounter("cooldown", 20);
        /* Force the close sequence (act #17 = 12); Group 47 finishes it and
         * re-applies alterable[0]=2, so visual and logic stay in sync. */
        if (state::WriteAnimation(door, 12)) QolDoorSfx();
        else Logf("qol| private %s anim force failed (close)", door);
        Logf("qol| private %s -> closed", door);
    }
    return 1;
}

/* Frame-27 monitor panel (flip it): the honest way to raise/lower it is to let
 * the game run its own trigger chain. The frame tests a hidden "trigger cursor"
 * Active against "flip trigger" every tick; when they overlap (and "trigger
 * ready" is 1) the game creates Active 8, forces its raise/lower animation and
 * plays Speaker #299/#300 itself. So we move the trigger cursor rectangle into
 * the live trigger rect instead of snapping flip it[0]: visual animation and
 * sound stay 100% native. g_qolFlipTicks re-asserts the position for a few
 * frames because the game may recompute the cursor from the real mouse first. */
static int g_qolFlipTicks;
static int g_qolFlipWant;

static int QolFlipPlaceCursor(void)
{
    int tx = 0, ty = 0, tw = 0, th = 0;
    if (!state::ReadObjectRect("flip trigger", &tx, &ty, &tw, &th)) return 0;
    if (!state::WriteObjectPosition("trigger cursor", tx + tw / 2, ty + th / 2)) return 0;
    return 1;
}

static int QolFlipPanel(void)
{
    int cur = 0;
    if (!state::ReadAlterableLive("flip it", 0, &cur)) {
        Logf("qol| private flip read failed");
        return 0;
    }
    if (cur != 0 && cur != 2) {
        Logf("qol| private flip busy state=%d", cur);
        return 1;                      /* mid-animation: swallow the key */
    }
    if (!state::WriteCounter("trigger ready", 1)) {
        Logf("qol| private flip trigger ready write failed");
        return 0;
    }
    if (!QolFlipPlaceCursor()) return 0;
    g_qolFlipWant = (cur == 0) ? 2 : 0;
    g_qolFlipTicks = 8;
    Logf("qol| private flip request state=%d target=%d", cur, g_qolFlipWant);
    return 1;
}

static void QolFlipTick(void)
{
    int cur = 0;
    if (g_qolFlipTicks <= 0) return;
    --g_qolFlipTicks;
    /* "flip it" is on the state watch list, so the snapshot read is the cheap
     * path; ReadAlterableLive is the fallback for builds where it is not. */
    if (!state::FindObjectAlterable("flip it", 0, &cur) &&
        !state::ReadAlterableLive("flip it", 0, &cur))
        return;
    if (cur == g_qolFlipWant) {
        Logf("qol| private flip done state=%d", cur);
        g_qolFlipTicks = 0;
        return;
    }
    state::WriteCounter("trigger ready", 1);
    QolFlipPlaceCursor();
}

static int QolPrivateTry(WPARAM vk, LPARAM l)
{
    int hour = 0, got = 0, v = 0;
    if (!g_fw.qolPrivateHotkeys) return 0;
    if (l & 0x40000000) return 0;          /* ignore auto-repeat */
    if (state::SceneIndex() != 27) return 0;
    if (state::ReadCounterLive("got you", &got) && got != 0) return 0;
    /* Private Room hour runs 12 -> 1 -> ... -> 6; only 6 is the win time. */
    if (state::ReadCounterLive("hour", &hour) && hour == 6) return 0;
    switch ((int)vk) {
    case 'A': return QolPrivateDoor("left door", "button", "left is closed");
    case 'D': return QolPrivateDoor("right door", "button 2", "right is closed");
    case 'W': return QolPrivateDoor("vent door", "button 3", "top is closed");
    case 'S':
        return QolFlipPanel();
    default:
        return 0;
    }
}

static LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    WNDPROC orig = FindOrig(hwnd);
    if (!orig) orig = DefWindowProcW;

    if (overlay::Ready())
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l);

    /* Lifecycle: the game dies with 0xC0000005 on window close (native defect, same
     * without injection), so DLL_PROCESS_DETACH is not trustworthy. On WM_CLOSE we
     * log it, flag closing (Hook_Present stops rendering) and let the game close. */
    if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP || msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
        QolUpdate();

    if (msg == WM_CLOSE || msg == WM_QUERYENDSESSION || msg == WM_ENDSESSION) {
        if (InterlockedExchange(&g_fw.closing, 1) == 0) {
            InterlockedExchange(&g_fw.guiVisible, 0);
            Logf("lifecycle| close requested msg=0x%04X hwnd=0x%08lX; rendering stopped, state already persisted",
                 msg, (unsigned long)(ULONG_PTR)hwnd);
        }
        return CallWindowProcW(orig, hwnd, msg, w, l);
    }

    /* F9: development-only scene warp menu (dedicated ImGui window; it takes
     * input focus like the main GUI while open). */
    if (msg == WM_KEYDOWN && (int)w == VK_F9) {
        if (l & 0x40000000) return 0;
        overlay::ToggleWarpMenu();
        QolUpdate();
        Logf("warp| menu toggled open=%d", overlay::WarpMenuOpen() ? 1 : 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && (int)w == g_fw.hotkeyVK) {
        /* Keyboard auto-repeat sets lParam bit 30 (previous key state). Toggling on
         * every repeat would flicker the GUI while the key is held, so only the
         * first keydown toggles; repeats are swallowed silently. */
        static LONG s_hotkeyRepeats = 0;
        if (l & 0x40000000) {
            LONG n = InterlockedIncrement(&s_hotkeyRepeats);
            if (n <= 3 || (n % 100) == 0)
                Logf("gui| ignore hotkey auto-repeat count=%ld", (long)n);
            return 0;
        }
        ToggleGui("hotkey-wndproc");
        QolUpdate();
        return 0;
    }
    if (msg == WM_KEYDOWN && !g_fw.guiVisible) {
        if (QolKeypadTry(w)) return 0;
        if (QolPrivateTry(w, l)) return 0;
    }
    if (msg == WM_SYSKEYDOWN && (w == VK_F4 || w == VK_TAB || w == VK_ESCAPE))
        return CallWindowProcW(orig, hwnd, msg, w, l);

    if (g_fw.guiVisible || overlay::WarpMenuOpen()) {
        int pass = 0;
        switch (msg) {
        case WM_CLOSE: case WM_QUIT: case WM_DESTROY: case WM_NCDESTROY:
        case WM_ACTIVATE: case WM_ACTIVATEAPP: case WM_SETFOCUS: case WM_KILLFOCUS:
        case WM_SIZE: case WM_MOVE: case WM_WINDOWPOSCHANGED: case WM_DPICHANGED:
        case WM_PAINT: case WM_NCPAINT: case WM_ERASEBKGND: case WM_SETCURSOR:
        case WM_SYSCOMMAND: case WM_DISPLAYCHANGE: case WM_SHOWWINDOW:
            pass = 1; break;
        default: break;
        }
        if (!pass && IS_KEY_MSG(msg)) {
            LONG n = InterlockedIncrement(&g_fw.swallowedKeys);
            if (n <= 20 || (n % 100) == 0)
                Logf("input| swallow key msg=0x%04X wparam=0x%08lX count=%ld", msg, (unsigned long)w, (long)n);
            return 0;
        }
        if (!pass && IS_MOUSE_MSG(msg)) {
            LONG n = InterlockedIncrement(&g_fw.swallowedMouse);
            if (n <= 20 || (n % 100) == 0)
                Logf("input| swallow mouse msg=0x%04X wparam=0x%08lX count=%ld", msg, (unsigned long)w, (long)n);
            return 0;
        }
    }
    return CallWindowProcW(orig, hwnd, msg, w, l);
}

static void FindGameWindow(D3DPRESENT_PARAMETERS *pp, HWND focus)
{
    HWND hwnd = NULL;
    if (pp && pp->hDeviceWindow && IsWindow(pp->hDeviceWindow)) hwnd = pp->hDeviceWindow;
    else if (focus && IsWindow(focus)) hwnd = focus;
    if (!hwnd) {
        HWND top = NULL;
        DWORD pid = GetCurrentProcessId();
        for (;;) {
            DWORD wpid = 0;
            top = FindWindowExW(NULL, top, NULL, NULL);
            if (!top) break;
            GetWindowThreadProcessId(top, &wpid);
            if (wpid == pid && IsWindowVisible(top)) { hwnd = top; break; }
        }
    }
    g_info.hwnd = hwnd;
    g_fw.hwnd = hwnd;
    LogClass(hwnd, "d3d9| game window");
}

static void HookDevice(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp, HWND focus)
{
    void **vt;
    if (!dev) return;
    g_info.device = dev;
    g_fw.device = dev;
    if (pp) {
        g_info.backW = pp->BackBufferWidth;
        g_info.backH = pp->BackBufferHeight;
        g_info.windowed = pp->Windowed ? 1 : 0;
        g_info.refresh = pp->FullScreen_RefreshRateInHz;
        Logf("d3d9| create_device ok backbuffer=%ux%u windowed=%d refresh=%u hDeviceWindow=0x%08lX focus=0x%08lX",
             pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed ? 1 : 0,
             pp->FullScreen_RefreshRateInHz,
             (unsigned long)(ULONG_PTR)pp->hDeviceWindow, (unsigned long)(ULONG_PTR)focus);
    }
    FindGameWindow(pp, focus);
    d3d9hook::SubclassGameWindows();

    if (g_info.deviceHooked) return;
    vt = *(void ***)dev;
    if (!vt) return;
    if (MH_CreateHook(vt[16], (void *)Hook_Reset, (void **)&g_origReset) == MH_OK)
        MH_EnableHook(vt[16]);
    if (MH_CreateHook(vt[17], (void *)Hook_Present, (void **)&g_origPresent) == MH_OK)
        MH_EnableHook(vt[17]);
    g_info.deviceHooked = (g_origPresent != NULL) ? 1 : 0;
    Logf("d3d9| device vtable hooked present=%p reset=%p origPresent=%p", vt[17], vt[16], (void *)g_origPresent);
}

static HRESULT WINAPI Hook_CreateDevice(IDirect3D9 *self, UINT adapter, D3DDEVTYPE type, HWND focus,
                                        DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr = g_origCreateDevice(self, adapter, type, focus, flags, pp, out);
    Logf("d3d9| CreateDevice adapter=%u type=%d hr=0x%08lX", adapter, (int)type, (unsigned long)hr);
    if (SUCCEEDED(hr) && out && *out) HookDevice(*out, pp, focus);
    return hr;
}

static IDirect3D9 *WINAPI Hook_Direct3DCreate9(UINT sdk)
{
    IDirect3D9 *d3d = g_origCreate9(sdk);
    Logf("d3d9| Direct3DCreate9 sdk=%u -> %p", sdk, (void *)d3d);
    if (d3d && !g_d3d9VtblHooked) {
        void **vt = *(void ***)d3d;
        MH_STATUS st = MH_CreateHook(vt[16], (void *)Hook_CreateDevice, (void **)&g_origCreateDevice);
        if (st == MH_OK) {
            MH_EnableHook(vt[16]);
            g_d3d9VtblHooked = 1;
            g_info.createDeviceHooked = 1;
            Logf("d3d9| IDirect3D9::CreateDevice hooked vt[16]=%p", vt[16]);
        } else {
            Logf("d3d9| CreateDevice hook failed status=%s", MH_StatusToString(st));
        }
    }
    return d3d;
}

static void UpdateFps(void)
{
    static ULONGLONG windowStart;
    static unsigned frames;
    ULONGLONG now = GetTickCount64();
    if (!windowStart) windowStart = now;
    frames++;
    if (now - windowStart >= 500) {
        g_info.fps = (float)frames * 1000.0f / (float)(now - windowStart);
        frames = 0;
        windowStart = now;
    }
}

static HRESULT WINAPI Hook_Present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    HRESULT hr;
    g_info.presents++;
    QolUpdate();
    QolFlipTick();
    g_fw.device = dev;
    if (!g_info.hwnd) {
        g_info.hwnd = wnd;
        g_fw.hwnd = wnd;
    }
    UpdateFps();
    if (!g_fw.closing) {
        if (!overlay::Ready() && g_info.hwnd)
            overlay::Init(dev, g_info.hwnd);
        if (overlay::Ready()) {
            if (g_fw.guiVisible) SetCursor(LoadCursorA(NULL, (LPCSTR)IDC_ARROW));
            overlay::RenderFrame();
        }
    }
    hr = g_origPresent(dev, src, dst, wnd, dirty);
    if (hr == D3DERR_DEVICELOST) overlay::Invalidate();
    return hr;
}

static HRESULT WINAPI Hook_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    HRESULT hr;
    Logf("d3d9| Reset requested pp=%p", (void *)pp);
    overlay::Invalidate();
    hr = g_origReset(dev, pp);
    if (SUCCEEDED(hr) && pp) {
        g_info.backW = pp->BackBufferWidth;
        g_info.backH = pp->BackBufferHeight;
        g_info.windowed = pp->Windowed ? 1 : 0;
        Logf("d3d9| Reset ok backbuffer=%ux%u windowed=%d", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed ? 1 : 0);
        overlay::CreateObjects();
    } else {
        Logf("d3d9| Reset failed hr=0x%08lX", (unsigned long)hr);
    }
    return hr;
}

bool d3d9hook::InstallEarly(void)
{
    MH_STATUS st;
    if (g_info.earlyInstalled) return true;
    g_hD3D9 = LoadLibraryA("d3d9.dll");
    Logf("d3d9| LoadLibrary(d3d9.dll)=0x%08lX err=%lu", (unsigned long)(ULONG_PTR)g_hD3D9, (unsigned long)GetLastError());
    if (!g_hD3D9) return false;
    st = MH_Initialize();
    Logf("minhook| MH_Initialize=%s", MH_StatusToString(st));
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    g_mhInit = 1;
    st = MH_CreateHookApi(L"d3d9.dll", "Direct3DCreate9", (void *)Hook_Direct3DCreate9, (void **)&g_origCreate9);
    if (st != MH_OK) {
        Logf("minhook| hook Direct3DCreate9 failed status=%s", MH_StatusToString(st));
        return false;
    }
    /* MH_CreateHookApi only creates/queues the hook; without MH_EnableHook the
     * detour is inert. This is what made the first Phase-3 run see presents=0:
     * the Direct3DCreate9 trampoline never fired, so no device hooks existed. */
    {
        void *pTarget = (void *)GetProcAddress(g_hD3D9, "Direct3DCreate9");
        MH_STATUS en = MH_EnableHook(pTarget ? pTarget : MH_ALL_HOOKS);
        Logf("minhook| enable Direct3DCreate9 hook status=%s target=%p", MH_StatusToString(en), pTarget);
        if (en != MH_OK) return false;
    }
    QolInstallHooks();
    g_info.earlyInstalled = 1;
    Logf("d3d9| early hook installed: d3d9!Direct3DCreate9 orig=%p", (void *)g_origCreate9);
    return true;
}

void d3d9hook::Shutdown(void)
{
    if (g_qolClipTarget) MH_DisableHook(g_qolClipTarget);
    if (g_qolSetPosTarget) MH_DisableHook(g_qolSetPosTarget);
    if (g_qolShowTarget) MH_DisableHook(g_qolShowTarget);
    g_qolClipTarget = g_qolSetPosTarget = g_qolShowTarget = NULL;
    if (g_info.deviceHooked && g_info.device) {
        void **vt = *(void ***)g_info.device;
        if (vt) {
            MH_DisableHook(vt[16]);
            MH_DisableHook(vt[17]);
        }
        g_info.deviceHooked = 0;
    }
    if (g_mhInit) {
        MH_Uninitialize();
        g_mhInit = 0;
    }
    Logf("d3d9| hooks shut down");
}
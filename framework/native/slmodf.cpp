/* SLModFramework32 - Phase 5 (framework DLL + D3D9 overlay + game state reader)
 * DllMain only creates a worker thread. All work happens in the worker:
 *   log -> sidecar fingerprint -> wait mmfs2.dll -> install D3D9 hooks early
 *   -> module snapshot -> PAMU scan (read-only) -> init marker -> wait renderer modules -> heartbeat
 */
#include "common.h"
#include "util.h"
#include "d3d9hook.h"
#include "scene.h"
#include "state.h"
#include "recorder.h"
#include "audio_hook.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MAX_MODULES 160
#define MMFS2_WAIT_MS 60000
#define HEARTBEAT_MS 5000
#define SCENE_POLL_MS 250   /* scene::Poll() cadence; cached validation is 2 small reads */
#define HEARTBEAT_MAX_MS (12*3600*1000)
#define EXPECTED_SHA256 "c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73"

/* P8 layout B: the framework is installed as <game>\SLModFramework\{bin,...}.
 * The DLL finds its own module file to locate the framework root instead of
 * assuming the legacy <game>\Workspace\framework tree. */
static HINSTANCE g_selfInst;

static void GetFrameworkDir(char *out, size_t cap)
{
    char self[MAX_PATH];
    char *p;
    DWORD n = GetModuleFileNameA(g_selfInst, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { lstrcpynA(out, g_fw.exeDir, (int)cap); return; }
    lstrcpynA(out, self, (int)cap);
    p = strrchr(out, '\\'); if (p) *p = 0; /* drop the DLL name -> ...\bin */
    p = strrchr(out, '\\'); if (p) *p = 0; /* drop bin          -> framework root */
}

static void GetExeInfo(void)
{
    char *p;
    DWORD n = GetModuleFileNameA(NULL, g_fw.exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) lstrcpynA(g_fw.exePath, "<unknown>", MAX_PATH);
    lstrcpynA(g_fw.exeDir, g_fw.exePath, MAX_PATH);
    p = strrchr(g_fw.exeDir, '\\');
    if (p) *p = 0; else lstrcpynA(g_fw.exeDir, ".", MAX_PATH);
    GetFrameworkDir(g_fw.frameworkDir, sizeof(g_fw.frameworkDir));
    wsprintfA(g_fw.logDir, "%s\\logs\\framework", g_fw.frameworkDir);
}

static void LogExeFacts(void)
{
    HANDLE h;
    LARGE_INTEGER size;
    FILETIME ct, at, wt;
    h = CreateFileA(g_fw.exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileSizeEx(h, &size)) {
            g_fw.exeSize = (unsigned long long)size.QuadPart;
            Logf("exe_size=%lld", (long long)size.QuadPart);
        }
        if (GetFileTime(h, &ct, &at, &wt)) {
            SYSTEMTIME st;
            FileTimeToSystemTime(&wt, &st);
            Logf("exe_mtime=%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
        CloseHandle(h);
    }
    if (ReadSidecarFingerprint()) {
        Logf("loader_fp| sha256=%s size=%llu verified=%d", g_fw.sha256, g_fw.exeSize, g_fw.verified);
        if (g_fw.sha256[0] && _stricmp(g_fw.sha256, EXPECTED_SHA256) != 0)
            Logf("loader_fp| WARNING sha256 differs from expected build fingerprint");
    } else {
        Logf("loader_fp| <sidecar not found> (DLL may have been loaded outside launcher)");
    }
}

static void LogModules(void)
{
    HANDLE snap;
    MODULEENTRY32 me;
    int count = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { Logf("modules: snapshot failed err=%lu", (unsigned long)GetLastError()); return; }
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            Logf("module| %-28s base=%08lX size=%08lX", me.szModule,
                 (unsigned long)(ULONG_PTR)me.modBaseAddr, (unsigned long)me.modBaseSize);
            count++;
        } while (count < LOG_MAX_MODULES && Module32Next(snap, &me));
    }
    CloseHandle(snap);
    Logf("modules: count=%d (cap %d)", count, LOG_MAX_MODULES);
}

static HMODULE WaitForMMFS2(void)
{
    HMODULE h = NULL;
    DWORD start = GetTickCount();
    DWORD waitMs = (g_fw.mmfs2WaitMs > 0) ? (DWORD)g_fw.mmfs2WaitMs : MMFS2_WAIT_MS;
    while ((GetTickCount() - start) < waitMs) {
        h = GetModuleHandleA("mmfs2.dll");
        if (h) break;
        Sleep(100);
    }
    Logf("mmfs2| base=%08lX waited_ms=%lu", h ? (unsigned long)(ULONG_PTR)h : 0UL,
         (unsigned long)(GetTickCount() - start));
    return h;
}

/* Read our own committed memory through ReadProcessMemory.  Unlike a raw
 * dereference this cannot raise an access violation when the game frees or
 * reprotects the page between VirtualQuery and the read, which is what killed
 * the worker thread (and with it the init marker) on 16:06/16:24/16:26. */
static int ReadSelf(UINT_PTR addr, void *buf, SIZE_T len)
{
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, buf, len, &got)) return 0;
    return got == len;
}

static int ScanPAMU(unsigned long *outAddr, unsigned *outRuntime, unsigned *outProduct, unsigned *outBuild, int logCandidates)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR addr, maxAddr;
    DWORD start = GetTickCount();
    unsigned long long scanned = 0;
    int regions = 0, readFail = 0;
    const SIZE_T CHUNK = 0x10000;
    unsigned char *buf = (unsigned char *)malloc(CHUNK);
    if (!buf) { Logf("pamu| scan buffer alloc failed"); return 0; }
    GetSystemInfo(&si);
    addr = (UINT_PTR)si.lpMinimumApplicationAddress;
    maxAddr = (UINT_PTR)si.lpMaximumApplicationAddress;
    while (addr < maxAddr) {
        SIZE_T scanLen, off;
        if ((GetTickCount() - start) > 12000) { Logf("pamu| scan deadline hit"); break; }
        if (scanned > (512ULL << 20)) { Logf("pamu| scan byte budget hit"); break; }
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) {
            Logf("pamu| VirtualQuery stop at %08lX err=%lu", (unsigned long)addr, (unsigned long)GetLastError());
            break;
        }
        /* The real CRunApp copy lives on the private heap.  Skipping images,
         * mapped files and shared sections removes the EXE-image false
         * candidate (0x6B378C) and avoids most pages the game mutates. */
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
            continue;
        }
        regions++;
        scanLen = mbi.RegionSize;
        if (scanLen > 0x4000000) scanLen = 0x4000000;
        for (off = 0; off < scanLen; off += (CHUNK - 16)) {
            SIZE_T n = scanLen - off;
            SIZE_T i;
            if (n > CHUNK) n = CHUNK;
            if (n < 24) break;
            if (!ReadSelf((UINT_PTR)mbi.BaseAddress + off, buf, n)) { readFail++; break; }
            for (i = 0; i + 24 <= n; i += 4) {
                if (buf[i] == 'P' && buf[i+1] == 'A' && buf[i+2] == 'M' && buf[i+3] == 'U') {
                    unsigned rt, pv, pb, f4;
                    memcpy(&rt, buf + i + 4, 4);
                    memcpy(&pv, buf + i + 8, 4);
                    memcpy(&pb, buf + i + 12, 4);
                    memcpy(&f4, buf + i + 16, 4);
                    if (logCandidates)
                        Logf("pamu| cand addr=%08lX region_base=%08lX region_size=%08lX rt=0x%X pv=%u pb=%u f4=%u",
                             (unsigned long)((UINT_PTR)mbi.BaseAddress + off + i),
                             (unsigned long)(ULONG_PTR)mbi.BaseAddress,
                             (unsigned long)mbi.RegionSize, rt, pv, pb, f4);
                    if (rt == 0x302 && pb == 286) {
                        *outAddr = (unsigned long)((UINT_PTR)mbi.BaseAddress + off + i);
                        *outRuntime = rt; *outProduct = pv; *outBuild = pb;
                        free(buf);
                        Logf("pamu| scan ok regions=%d bytes=%llu read_fail=%d elapsed_ms=%lu",
                             regions, scanned, readFail, (unsigned long)(GetTickCount() - start));
                        return 1;
                    }
                }
            }
            scanned += n;
        }
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
    free(buf);
    Logf("pamu| scan done regions=%d bytes=%llu read_fail=%d elapsed_ms=%lu",
         regions, scanned, readFail, (unsigned long)(GetTickCount() - start));
    return 0;
}

static void WriteInitMarker(const char *status, const char *detail, unsigned long pamuAddr, int hookOk)
{
    char path[MAX_PATH];
    FILE *f;
    wsprintfA(path, "%s\\init-%lu.done", g_fw.logDir, (unsigned long)g_fw.pid);
    f = fopen(path, "wb");
    if (!f) { Logf("init marker write failed path=%s", path); return; }
    fprintf(f, "status=%s\ndetail=%s\nversion=%s\npid=%lu\npamu=%08lX\nsha256=%s\ngate=%s\nhooks=%d\n",
            status, detail, SLMODF_VERSION, (unsigned long)g_fw.pid, pamuAddr, g_fw.sha256, GateStatusName(), hookOk);
    fclose(f);
    Logf("init marker=%s status=%s detail=%s hooks=%d", path, status, detail, hookOk);
}

static void WaitLogModule(const char *name, DWORD timeoutMs)
{
    DWORD start = GetTickCount();
    HMODULE h = NULL;
    while ((GetTickCount() - start) < timeoutMs) {
        h = GetModuleHandleA(name);
        if (h) break;
        Sleep(50);
    }
    Logf("late_module| %-14s base=%08lX waited_ms=%lu", name,
         h ? (unsigned long)(ULONG_PTR)h : 0UL, (unsigned long)(GetTickCount() - start));
}

static DWORD WINAPI WorkerThread(LPVOID param)
{
    char logPath[MAX_PATH];
    SYSTEMTIME st;
    HMODULE hmmfs2;
    unsigned long pamuAddr = 0;
    unsigned rt = 0, pv = 0, pb = 0;
    int found = 0, hookOk = 0, fpOk;
    DWORD hbStart;
    (void)param;
    g_fw.pid = GetCurrentProcessId();
    GetExeInfo();
    EnsureDirA(g_fw.logDir);
    GetLocalTime(&st);
    wsprintfA(logPath, "%s\\slmodf-%04d%02d%02d-%02d%02d%02d-%lu.log",
              g_fw.logDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (unsigned long)g_fw.pid);
    g_fw.log = fopen(logPath, "ab");
    if (!g_fw.log) return 0;
    LogSetPath(logPath); /* P8.2: enables log rotation + diagnostics bundle */
    Logf("=== %s %s (Phase 8) ===", SLMODF_NAME, SLMODF_VERSION);
    Logf("log=%s", g_fw.logPath);
    Logf("note: DllMain only created this thread; no work in loader lock");
    Logf("exe=%s", g_fw.exePath);
    Logf("framework=%s", g_fw.frameworkDir);
    LogExeFacts();
    LoadConfig();
    fpOk = (g_fw.sha256[0] != 0 && _stricmp(g_fw.sha256, EXPECTED_SHA256) == 0);
    Logf("gate| fingerprint_ok=%d expected=%s", fpOk, EXPECTED_SHA256);
    /* Install the Direct3DCreate9 hook before mmfs2/renderer can call it.
     * Loading d3d9.dll this early is harmless: the detour only logs and forwards. */
    if (g_fw.hookD3D9) {
        hookOk = d3d9hook::InstallEarly() ? 1 : 0;
        Logf("d3d9| early install result=%d (before mmfs2 wait)", hookOk);
    } else {
        Logf("d3d9| hook disabled by config hook_d3d9=0");
    }
    /* Phase 6: install the DirectSound probe before mmfs2 loads and creates
     * its audio buffers. Read-only; a failure is never fatal. */
    audio_install_early();
    hmmfs2 = WaitForMMFS2();
    g_fw.hMmfs2 = hmmfs2;
    if (hmmfs2 && g_fw.hookD3D9 && !hookOk) {
        hookOk = d3d9hook::InstallEarly() ? 1 : 0;
        Logf("d3d9| retry install after mmfs2 loaded result=%d", hookOk);
    }
    LogModules();
    if (hmmfs2) {
        DWORD pamuStart = GetTickCount();
        int attempts = 0;
        for (;;) {
            attempts++;
            found = ScanPAMU(&pamuAddr, &rt, &pv, &pb, attempts == 1);
            if (found && pb == 286) break;
            if ((GetTickCount() - pamuStart) > 15000) break;
            Sleep(300);
        }
        Logf("pamu| attempts=%d elapsed_ms=%lu", attempts, (unsigned long)(GetTickCount() - pamuStart));
    }
    g_fw.pamuAddr = pamuAddr;
    g_fw.rt = rt; g_fw.pv = pv; g_fw.pb = pb;
    if (!hmmfs2) {
        g_fw.gate = 3;
        WriteInitMarker("fail", "mmfs2_missing", 0, hookOk);
    } else if (!fpOk) {
        g_fw.gate = 3;
        Logf("gate| fail: exe fingerprint not the verified build");
        WriteInitMarker("fail", "fingerprint_mismatch", pamuAddr, hookOk);
    } else if (found && pb == 286) {
        g_fw.gate = 1;
        Logf("gate| version_ok build=%u runtime=0x%X product=%u", pb, rt, pv);
        WriteInitMarker("ok", "build286+pamu", pamuAddr, hookOk);
    } else {
        g_fw.gate = 2;
        Logf("gate| degraded: mmfs2 loaded but PAMU build286 not located (found=%d pb=%u)", found, pb);
        WriteInitMarker("degraded", "mmfs2_ok_pamu_unknown", pamuAddr, hookOk);
    }
    scene::Init();
    state::Init();
    recorder::Init();
    WaitLogModule("mmf2d3d9.dll", 60000);
    WaitLogModule("d3d9.dll", 60000);
    WaitLogModule("dsound.dll", 60000);
    Logf("post_render_modules: mmf2d3d9=%08lX d3d9=%08lX mmf2d3d8=%08lX dsound=%08lX",
         (unsigned long)(ULONG_PTR)GetModuleHandleA("mmf2d3d9.dll"),
         (unsigned long)(ULONG_PTR)GetModuleHandleA("d3d9.dll"),
         (unsigned long)(ULONG_PTR)GetModuleHandleA("mmf2d3d8.dll"),
         (unsigned long)(ULONG_PTR)GetModuleHandleA("dsound.dll"));
    d3d9hook::SubclassGameWindows();
    d3d9hook::LogProcessWindows("post_render");
    Logf("ui| hotkey VK=0x%02X hud=%d gui_visible=%ld", g_fw.hotkeyVK, g_fw.hudEnabled, (long)g_fw.guiVisible);
    hbStart = GetTickCount();
    {
        int tick = 0;
        const int ticksPerHeartbeat = HEARTBEAT_MS / SCENE_POLL_MS;
        for (;;) {
            DWORD elapsed;
            Sleep(SCENE_POLL_MS);
            scene::Poll();
            state::Poll();
            recorder::Poll();
            if (++tick < ticksPerHeartbeat) continue;
            tick = 0;
            d3d9hook::SubclassGameWindows();
            elapsed = GetTickCount() - hbStart;
            {
                const SceneInfo *sc = scene::Current();
                unsigned long aCreate = 0, aPlay = 0, aMatch = 0, aScream = 0;
                audio_get_stats(&aCreate, &aPlay, &aMatch, &aScream);
                Logf("alive| uptime_ms=%lu gui=%ld toggles=%ld swallow_k=%ld swallow_m=%ld presents=%llu fps=%.1f device=%p scene=%d:%s audio=%lu/%lu/%lu/%lu",
                     (unsigned long)elapsed, (long)g_fw.guiVisible, (long)g_fw.hotkeyToggles,
                     (long)g_fw.swallowedKeys, (long)g_fw.swallowedMouse,
                     d3d9hook::g_info.presents, (double)d3d9hook::g_info.fps, g_fw.device,
                     sc->valid ? sc->index : -1, sc->valid ? sc->name : "?",
                     aCreate, aPlay, aMatch, aScream);
            }
            {
                char stbuf[256];
                state::FormatSummary(stbuf, sizeof(stbuf));
                Logf("state| %s", stbuf);
            }
            if (elapsed > HEARTBEAT_MAX_MS) break;
        }
    }
    Logf("worker exit (heartbeat cap)");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    HANDLE th;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfInst = inst;
        DisableThreadLibraryCalls(inst);
        th = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
    return TRUE;
}

/* SLModFramework32 - Phase 2 minimal framework DLL
 * DllMain only creates a worker thread; all work happens there.
 * Read-only: logs modules, waits for mmfs2.dll, scans PAMU header, heartbeats.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SLMODF_VERSION "0.1.0-p2"
#define LOG_MAX_MODULES 160
#define MMFS2_WAIT_MS 60000
#define HEARTBEAT_MS 5000
#define HEARTBEAT_MAX_MS (12*3600*1000)

static FILE *g_log = NULL;
static char g_exePath[MAX_PATH];
static char g_exeDir[MAX_PATH];
static char g_logDir[MAX_PATH];
static DWORD g_pid = 0;

static void EnsureDirA(const char *path)
{
    char buf[MAX_PATH];
    size_t n = strlen(path);
    size_t i;
    if (n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);
    for (i = 0; i < n; i++) {
        if (buf[i] == '\\' || buf[i] == '/') {
            char save = buf[i];
            buf[i] = 0;
            if (i > 2 && buf[i - 1] != ':') CreateDirectoryA(buf, NULL);
            buf[i] = save;
        }
    }
    CreateDirectoryA(buf, NULL);
}

static void LogWrite(const char *fmt, ...)
{
    SYSTEMTIME st;
    va_list ap;
    if (!g_log) return;
    GetLocalTime(&st);
    fprintf(g_log, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [pid %lu] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, (unsigned long)g_pid);
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void GetExeInfo(void)
{
    char *p;
    DWORD n = GetModuleFileNameA(NULL, g_exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { lstrcpynA(g_exePath, "<unknown>", MAX_PATH); }
    lstrcpynA(g_exeDir, g_exePath, MAX_PATH);
    p = strrchr(g_exeDir, '\\');
    if (p) *p = 0; else lstrcpynA(g_exeDir, ".", MAX_PATH);
    wsprintfA(g_logDir, "%s\\Workspace\\logs\\framework", g_exeDir);
}

static void LogExeFacts(void)
{
    HANDLE h;
    LARGE_INTEGER size;
    FILETIME ct, at, wt;
    char logPath[MAX_PATH];
    GetExeInfo();
    LogWrite("framework=%s build=%s %s pid=%lu", SLMODF_VERSION, __DATE__, __TIME__, (unsigned long)g_pid);
    LogWrite("exe=%s", g_exePath);
    h = CreateFileA(g_exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        if (GetFileSizeEx(h, &size)) LogWrite("exe_size=%lld", (long long)size.QuadPart);
        if (GetFileTime(h, &ct, &at, &wt)) {
            SYSTEMTIME st;
            FileTimeToSystemTime(&wt, &st);
            LogWrite("exe_mtime=%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
        CloseHandle(h);
    }
    /* loader writes a fingerprint sidecar before injection */
    wsprintfA(logPath, "%s\\launch-%lu.json", g_logDir, (unsigned long)g_pid);
    h = CreateFileA(logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[2048];
        DWORD got = 0;
        if (ReadFile(h, buf, sizeof(buf) - 1, &got, NULL) && got > 0) {
            char *s, *line;
            buf[got] = 0;
            for (s = buf; s && *s; s = line) {
                line = strchr(s, '\n');
                if (line) *line++ = 0;
                if (*s) LogWrite("loader_fp| %s", s);
            }
        }
        CloseHandle(h);
    } else {
        LogWrite("loader_fp| <sidecar not found>");
    }
}

static void LogModules(void)
{
    HANDLE snap;
    MODULEENTRY32 me;
    int count = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { LogWrite("modules: snapshot failed err=%lu", (unsigned long)GetLastError()); return; }
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            LogWrite("module| %-28s base=%08lX size=%08lX",
                     me.szModule, (unsigned long)(ULONG_PTR)me.modBaseAddr, (unsigned long)me.modBaseSize);
            count++;
        } while (count < LOG_MAX_MODULES && Module32Next(snap, &me));
    }
    CloseHandle(snap);
    LogWrite("modules: count=%d (cap %d)", count, LOG_MAX_MODULES);
}

static HMODULE WaitForMMFS2(void)
{
    HMODULE h = NULL;
    DWORD start = GetTickCount();
    while ((GetTickCount() - start) < MMFS2_WAIT_MS) {
        h = GetModuleHandleA("mmfs2.dll");
        if (h) break;
        Sleep(100);
    }
    LogWrite("mmfs2| base=%08lX waited_ms=%lu",
             h ? (unsigned long)(ULONG_PTR)h : 0UL,
             (unsigned long)(GetTickCount() - start));
    return h;
}

/* Read-only PAMU header scan. Bounded: private regions fully, mapped regions only the
 * first 64 KiB, overall deadline and byte budget. */
static int ScanPAMU(unsigned long *outAddr, unsigned *outRuntime, unsigned *outProduct, unsigned *outBuild, int logCandidates)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR addr, maxAddr;
    DWORD start = GetTickCount();
    unsigned long long scanned = 0;
    GetSystemInfo(&si);
    addr = (UINT_PTR)si.lpMinimumApplicationAddress;
    maxAddr = (UINT_PTR)si.lpMaximumApplicationAddress;
    while (addr < maxAddr) {
        SIZE_T scanLen;
        const unsigned char *p;
        SIZE_T i, n;
        if ((GetTickCount() - start) > 12000) { LogWrite("pamu| scan deadline hit"); break; }
        if (scanned > (512ULL << 20)) { LogWrite("pamu| scan byte budget hit"); break; }
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) {
            LogWrite("pamu| VirtualQuery stop at %08lX err=%lu", (unsigned long)addr, (unsigned long)GetLastError());
            break;
        }
        if (mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
            continue;
        }
        scanLen = mbi.RegionSize;
        if (mbi.Type == MEM_MAPPED && scanLen > 0x10000) scanLen = 0x10000;
        if (scanLen > 0x4000000) scanLen = 0x4000000; /* cap single region 64 MiB */
        p = (const unsigned char *)mbi.BaseAddress;
        n = scanLen;
        for (i = 0; i + 24 <= n; i += 4) {
            if (p[i] == 'P' && p[i+1] == 'A' && p[i+2] == 'M' && p[i+3] == 'U') {
                unsigned rt = *(const unsigned *)(p + i + 4);
                unsigned pv = *(const unsigned *)(p + i + 8);
                unsigned pb = *(const unsigned *)(p + i + 12);
                unsigned f4 = *(const unsigned *)(p + i + 16);
                if (logCandidates) {
                    LogWrite("pamu| cand addr=%08lX region_base=%08lX region_size=%08lX rt=0x%X pv=%u pb=%u f4=%u",
                             (unsigned long)(addr + i), (unsigned long)(ULONG_PTR)mbi.BaseAddress,
                             (unsigned long)mbi.RegionSize, rt, pv, pb, f4);
                }
                if (rt == 0x302 && pb == 286) {
                    *outAddr = (unsigned long)(addr + i);
                    *outRuntime = rt; *outProduct = pv; *outBuild = pb;
                    return 1;
                }
            }
        }
        scanned += n;
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
    return 0;
}

static void WriteInitMarker(const char *status, const char *detail, unsigned long pamuAddr)
{
    char path[MAX_PATH];
    FILE *f;
    wsprintfA(path, "%s\\init-%lu.done", g_logDir, (unsigned long)g_pid);
    f = fopen(path, "wb");
    if (!f) { LogWrite("init marker write failed path=%s", path); return; }
    fprintf(f, "status=%s\n", status);
    fprintf(f, "detail=%s\n", detail);
    fprintf(f, "version=%s\n", SLMODF_VERSION);
    fprintf(f, "pid=%lu\n", (unsigned long)g_pid);
    fprintf(f, "pamu=%08lX\n", pamuAddr);
    fprintf(f, "exe=%s\n", g_exePath);
    fclose(f);
    LogWrite("init marker=%s status=%s detail=%s", path, status, detail);
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
    LogWrite("late_module| %-14s base=%08lX waited_ms=%lu", name,
             h ? (unsigned long)(ULONG_PTR)h : 0UL, (unsigned long)(GetTickCount() - start));
}

static DWORD WINAPI WorkerThread(LPVOID param)
{
    char logPath[MAX_PATH];
    SYSTEMTIME st;
    HMODULE hmmfs2;
    unsigned long pamuAddr = 0;
    unsigned rt = 0, pv = 0, pb = 0;
    int found;
    DWORD hbStart;
    (void)param;
    g_pid = GetCurrentProcessId();
    GetExeInfo();
    EnsureDirA(g_logDir);
    GetLocalTime(&st);
    wsprintfA(logPath, "%s\\slmodf-%04d%02d%02d-%02d%02d%02d-%lu.log",
              g_logDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (unsigned long)g_pid);
    g_log = fopen(logPath, "ab");
    if (!g_log) return 0;
    LogWrite("=== SLModFramework %s (Phase 2 minimal, read-only) ===", SLMODF_VERSION);
    LogWrite("note: DllMain only created this thread; no hooks, no writes to game memory");
    LogExeFacts();
    hmmfs2 = WaitForMMFS2();
    LogModules();
    {
        DWORD pamuStart = GetTickCount();
        int attempts = 0;
        for (;;) {
            attempts++;
            found = ScanPAMU(&pamuAddr, &rt, &pv, &pb, attempts == 1);
            if (found && pb == 286) break;
            if ((GetTickCount() - pamuStart) > 15000) break;
            Sleep(300);
        }
        LogWrite("pamu| attempts=%d elapsed_ms=%lu", attempts, (unsigned long)(GetTickCount() - pamuStart));
    }
    if (found && pb == 286) {
        LogWrite("gate| version_ok build=%u runtime=0x%X product=%u", pb, rt, pv);
        WriteInitMarker("ok", "build286+pamu", pamuAddr);
    } else if (hmmfs2) {
        LogWrite("gate| degraded: mmfs2 loaded but PAMU build286 not located (found=%d pb=%u)", found, pb);
        WriteInitMarker("degraded", "mmfs2_ok_pamu_unknown", pamuAddr);
    } else {
        LogWrite("gate| fail: mmfs2.dll not loaded within %d ms", MMFS2_WAIT_MS);
        WriteInitMarker("fail", "mmfs2_missing", 0);
    }
    /* renderer/audio modules load after the app starts frames; log when they appear (evidence for P3/P6) */
    WaitLogModule("mmf2d3d9.dll", 60000);
    WaitLogModule("d3d9.dll", 60000);
    WaitLogModule("dsound.dll", 60000);
    LogWrite("post_render_modules: mmf2d3d9=%08lX d3d9=%08lX mmf2d3d8=%08lX dsound=%08lX",
             (unsigned long)(ULONG_PTR)GetModuleHandleA("mmf2d3d9.dll"),
             (unsigned long)(ULONG_PTR)GetModuleHandleA("d3d9.dll"),
             (unsigned long)(ULONG_PTR)GetModuleHandleA("mmf2d3d8.dll"),
             (unsigned long)(ULONG_PTR)GetModuleHandleA("dsound.dll"));
    hbStart = GetTickCount();
    for (;;) {
        DWORD elapsed;
        Sleep(HEARTBEAT_MS);
        elapsed = GetTickCount() - hbStart;
        if (elapsed > HEARTBEAT_MAX_MS) break;
        LogWrite("alive| uptime_ms=%lu mmfs2=%08lX", (unsigned long)elapsed,
                 (unsigned long)(ULONG_PTR)GetModuleHandleA("mmfs2.dll"));
    }
    LogWrite("worker exit (heartbeat cap)");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    HANDLE th;
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        th = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
    return TRUE;
}

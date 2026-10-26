/* SLModFramework loader (32-bit) - Phase 2
 * 1) verifies the target EXE SHA-256 fingerprint
 * 2) launches it CREATE_SUSPENDED and injects SLModFramework32.dll via LoadLibraryW
 * 3) waits for the framework init marker, optionally auto-closes the game for regression
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"

#define FRAMEWORK_DLL "SLModFramework32.dll"
#define LOADER_VERSION "1.0.0"   /* keep in sync with SLMODF_VERSION */
#define EXPECTED_SHA256 "c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73"
#define DEFAULT_WAIT_MS 30000
#define IO_BUF (1024*1024)

static char g_logPath[MAX_PATH];
static FILE *g_log = NULL;

static void Log(const char *fmt, ...)
{
    SYSTEMTIME st;
    va_list ap;
    va_list ap2;
    GetLocalTime(&st);
    printf("[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_start(ap, fmt);
    va_copy(ap2, ap);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    if (g_log) {
        fprintf(g_log, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        vfprintf(g_log, fmt, ap2);
        fputc('\n', g_log);
        fflush(g_log);
    }
    va_end(ap2);
}

static void EnsureDirA(const char *path)
{
    char buf[MAX_PATH];
    size_t n = strlen(path), i;
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

static void NormalizeSlashes(char *path)
{
    char *p;
    for (p = path; *p; ++p) if (*p == '/') *p = '\\';
}

static void GetLoaderDir(char *out, size_t cap)
{
    char *p;
    if (!GetModuleFileNameA(NULL, out, (DWORD)cap)) { lstrcpynA(out, ".", (int)cap); return; }
    p = strrchr(out, '\\');
    if (p) *p = 0;
}

static void StripOneComponent(char *path)
{
    char *p = strrchr(path, '\\');
    if (p) *p = 0;
}

/* P8 layout B: logs live under the framework root (<game>\SLModFramework\logs\framework). */
static void GetLogDirForFramework(const char *frameworkDir, char *out, size_t cap)
{
    _snprintf(out, cap, "%s\\logs\\framework", frameworkDir);
    out[cap - 1] = 0;
}

static int Sha256File(const char *path, char hex[65], unsigned long long *outSize)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    SHA256_CTX ctx;
    unsigned char *buf;
    DWORD got = 0;
    unsigned long long total = 0, nextReport = 128ULL << 20;
    if (h == INVALID_HANDLE_VALUE) return 0;
    buf = (unsigned char *)malloc(IO_BUF);
    if (!buf) { CloseHandle(h); return 0; }
    sha256_init(&ctx);
    for (;;) {
        if (!ReadFile(h, buf, IO_BUF, &got, NULL)) { free(buf); CloseHandle(h); return 0; }
        if (got == 0) break;
        sha256_update(&ctx, buf, got);
        total += got;
        if (total >= nextReport) { Log("  hashing... %.0f MiB", (double)total / 1048576.0); nextReport += 128ULL << 20; }
    }
    free(buf);
    CloseHandle(h);
    {
        unsigned char hash[32];
        sha256_final(&ctx, hash);
        sha256_hex(hash, hex);
    }
    if (outSize) *outSize = total;
    return 1;
}

typedef struct { DWORD pid; HWND hwnd; } FINDWND;
static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
{
    FINDWND *f = (FINDWND *)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == f->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        f->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND FindMainWindow(DWORD pid)
{
    FINDWND f;
    f.pid = pid; f.hwnd = NULL;
    EnumWindows(EnumProc, (LPARAM)&f);
    return f.hwnd;
}

static int WaitForFile(const char *path, DWORD timeoutMs, char *content, size_t cap)
{
    DWORD start = GetTickCount();
    while ((GetTickCount() - start) < timeoutMs) {
        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD got = 0;
            if (content && cap) {
                if (!ReadFile(h, content, (DWORD)cap - 1, &got, NULL)) got = 0;
                content[got] = 0;
            }
            CloseHandle(h);
            return 1;
        }
        Sleep(100);
    }
    return 0;
}

/* ---- Phase 3 test automation: scheduled hotkey toggle / input injection ---- */
typedef struct { int seconds; int kind; } SchedEvent; /* kind 0=Insert GUI toggle (keydown only, leaves GUI state flipped), 1=space+mouse input burst */
#define MAX_SCHED 32
static SchedEvent g_sched[MAX_SCHED];
static int g_schedCount;
static DWORD g_schedPid;
static int g_toggleVk = VK_INSERT; /* --toggle-key VK (hex or decimal), must match framework.ini hotkey= */

static BOOL CALLBACK FindInputChildProc(HWND hwnd, LPARAM lp)
{
    char cls[64];
    cls[0] = 0;
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strstr(cls, "Mf2EditClassTh")) {
        *(HWND *)lp = hwnd;
        return FALSE;
    }
    return TRUE;
}

/* Real keyboard/mouse input is delivered to the focused window: the Mf2EditClassTh
 * child, not the top-level Mf2MainClassTh. Post to that single window only, because
 * posting one message to the whole tree would toggle the GUI twice (both windows are
 * subclassed). */
static HWND FindInputWindow(HWND main)
{
    HWND child = NULL;
    if (!main) return NULL;
    EnumChildWindows(main, FindInputChildProc, (LPARAM)&child);
    return child ? child : main;
}

static void PostInputMsg(HWND main, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND target = FindInputWindow(main);
    if (target) PostMessageW(target, msg, wp, lp);
}

static DWORD WINAPI SchedThread(LPVOID p)
{
    int i;
    DWORD t0 = GetTickCount();
    (void)p;
    for (i = 0; i < g_schedCount; i++) {
        DWORD target = (DWORD)g_sched[i].seconds * 1000;
        DWORD now;
        HWND main;
        while ((now = GetTickCount() - t0) < target) Sleep(50);
        main = FindMainWindow(g_schedPid);
        Log("sched: t=%lu ms kind=%d main=%p input=%p", (unsigned long)now, g_sched[i].kind,
            (void *)main, (void *)FindInputWindow(main));
        if (!main) continue;
        if (g_sched[i].kind == 0) {
            /* Keydown only: the GUI stays in the new state, so a following
             * --test-input event can verify input swallowing while it is open. */
            Log("sched: posting toggle vk=0x%02X", g_toggleVk);
            PostInputMsg(main, WM_KEYDOWN, (WPARAM)g_toggleVk, 0x00530001);
        } else {
            PostInputMsg(main, WM_KEYDOWN, VK_SPACE, 0x00390001);
            PostInputMsg(main, WM_KEYUP, VK_SPACE, 0xC0390001);
            PostInputMsg(main, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 100));
            PostInputMsg(main, WM_LBUTTONUP, 0, MAKELPARAM(100, 100));
            PostInputMsg(main, WM_MOUSEMOVE, 0, MAKELPARAM(120, 120));
        }
    }
    Log("sched: all events posted");
    return 0;
}

static int ParseCsvSeconds(const char *src, int kind)
{
    int added = 0;
    const char *p = src;
    while (*p && g_schedCount < MAX_SCHED) {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        g_sched[g_schedCount].seconds = (int)v;
        g_sched[g_schedCount].kind = kind;
        g_schedCount++;
        added++;
        p = end;
        if (*p == ',') p++;
    }
    return added;
}

static int ParseVk(const char *s)
{
    char *end = NULL;
    long v = strtol(s, &end, 0); /* base 0: accepts 0x71 and 113 */
    if (end == s || v < 1 || v > 0xFE) return -1;
    return (int)v;
}

static int CmpSched(const void *a, const void *b)
{
    return ((const SchedEvent *)a)->seconds - ((const SchedEvent *)b)->seconds;
}

static void PrintHelp(void)
{
    printf("SLModFramework loader (Phase 3)\n");
    printf("usage: launcher32.exe [--exe PATH] [--dll PATH] [--test-exit SECONDS]\n");
    printf("                      [--toggle-gui 8,20] [--test-input 12]\n");
    printf("                      [--toggle-key 0x71] [--wait-ms N] [--no-verify] [--help]\n");
    printf("--toggle-gui: post the hotkey keydown at the given seconds (flips GUI and holds it,\n");
    printf("              so a later --test-input lands while the GUI is open)\n");
    printf("--toggle-key: VK to post for --toggle-gui, hex (0x71=F2) or decimal; default 0x2D (Insert).\n");
    printf("              Must match hotkey= in <framework>\\config\\framework.ini.\n");
    printf("--test-input: post space+mouse at the given seconds after init (input swallow test)\n");
    printf("layout: launcher must live in <game>\\SLModFramework\\bin\\(framework root = parent,\n");
    printf("        game root = grandparent); default exe: <game>\\SisterLocation.exe\n");
    printf("        (env SL_GAME_EXE overrides the game exe only)\n");
}

int main(int argc, char **argv)
{
    char exePath[MAX_PATH] = {0};
    char dllPath[MAX_PATH] = {0};
    char loaderDir[MAX_PATH];
    char frameworkDir[MAX_PATH];
    char gameDir[MAX_PATH];
    char logDir[MAX_PATH];
    char initPath[MAX_PATH];
    char initContent[2048] = {0};
    char hex[65] = {0};
    unsigned long long fileSize = 0;
    int testExit = -1, noVerify = 0, waitMs = DEFAULT_WAIT_MS;
    int i, rc = 0;
    PROCESS_INFORMATION pi;
    STARTUPINFOW si;
    WCHAR wexe[MAX_PATH], wdll[MAX_PATH], wcmd[MAX_PATH + 4];
    HANDLE hThread;
    LPVOID remoteMem;
    SIZE_T written = 0;
    HMODULE (WINAPI *pLoadLibraryW)(LPCWSTR);
    DWORD remoteResult = 0;
    SYSTEMTIME st;
    char launchJsonPath[MAX_PATH];
    int injected = 0;

    GetLoaderDir(loaderDir, sizeof(loaderDir));
    lstrcpynA(frameworkDir, loaderDir, MAX_PATH);
    StripOneComponent(frameworkDir); /* bin -> SLModFramework (framework root) */
    lstrcpynA(gameDir, frameworkDir, MAX_PATH);
    StripOneComponent(gameDir);      /* SLModFramework -> game dir */
    if (GetEnvironmentVariableA("SL_GAME_EXE", exePath, MAX_PATH) == 0) {
        _snprintf(exePath, MAX_PATH, "%s\\SisterLocation.exe", gameDir);
        exePath[MAX_PATH - 1] = 0;
    }
    _snprintf(dllPath, sizeof(dllPath), "%s\\%s", loaderDir, FRAMEWORK_DLL);
    dllPath[sizeof(dllPath) - 1] = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { PrintHelp(); return 0; }
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) lstrcpynA(exePath, argv[++i], MAX_PATH);
        else if (!strcmp(argv[i], "--dll") && i + 1 < argc) lstrcpynA(dllPath, argv[++i], MAX_PATH);
        else if (!strcmp(argv[i], "--test-exit") && i + 1 < argc) testExit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--toggle-key") && i + 1 < argc) {
            int vk = ParseVk(argv[++i]);
            if (vk < 0) { Log("invalid --toggle-key value: %s", argv[i]); PrintHelp(); return 1; }
            g_toggleVk = vk;
        }
        else if (!strcmp(argv[i], "--toggle-gui") && i + 1 < argc) ParseCsvSeconds(argv[++i], 0);
        else if (!strcmp(argv[i], "--test-input") && i + 1 < argc) ParseCsvSeconds(argv[++i], 1);
        else if (!strcmp(argv[i], "--wait-ms") && i + 1 < argc) waitMs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-verify")) noVerify = 1;
        else if (argv[i][0] != '-' && exePath[0] == 0) lstrcpynA(exePath, argv[i], MAX_PATH);
        else { Log("unknown argument: %s", argv[i]); PrintHelp(); return 1; }
    }
    if (exePath[0] == 0) { Log("no target exe"); PrintHelp(); return 1; }
    /* --exe/--dll may be given with forward slashes; all later path handling and the
     * CreateProcess working-directory split use '\\', so normalize before use. */
    NormalizeSlashes(exePath);
    NormalizeSlashes(dllPath);
    if (GetFileAttributesA(exePath) == INVALID_FILE_ATTRIBUTES) { Log("target exe not found: %s", exePath); return 1; }

    GetLogDirForFramework(frameworkDir, logDir, sizeof(logDir));
    EnsureDirA(logDir);
    GetLocalTime(&st);
    _snprintf(g_logPath, sizeof(g_logPath), "%s\\loader-%04d%02d%02d-%02d%02d%02d.log",
              logDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_logPath[sizeof(g_logPath) - 1] = 0;
    g_log = fopen(g_logPath, "ab");
    Log("=== SLModFramework loader (Phase 3) ===");
    Log("this=%s", loaderDir);
    Log("framework=%s", frameworkDir);
    Log("game_dir=%s", gameDir);
    Log("exe=%s", exePath);
    Log("dll=%s", dllPath);

    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) { Log("framework dll not found: %s", dllPath); if (g_log) fclose(g_log); return 1; }

    Log("sha256: hashing target (%.1f MiB)...", 0.0);
    if (!Sha256File(exePath, hex, &fileSize)) { Log("sha256: FAILED to read target"); if (g_log) fclose(g_log); return 1; }
    Log("sha256=%s size=%llu", hex, fileSize);
    if (strcmp(hex, EXPECTED_SHA256) != 0) {
        Log("FINGERPRINT MISMATCH expected=%s", EXPECTED_SHA256);
        if (!noVerify) {
            Log("refusing to inject (use --no-verify only for deliberate diagnostics)");
            if (g_log) fclose(g_log);
            return 3;
        }
        Log("--no-verify: continuing on unverified build (framework will report degraded)");
    } else {
        Log("fingerprint OK (known build: Fusion 2.5 R286, PC v1.121)");
    }

    MultiByteToWideChar(CP_ACP, 0, exePath, -1, wexe, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, dllPath, -1, wdll, MAX_PATH);
    wsprintfW(wcmd, L"\"%s\"", wexe);
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    {
        WCHAR wdir[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, exePath, -1, wdir, MAX_PATH);
        {
            WCHAR *p = wcsrchr(wdir, L'\\');
            if (p) *p = 0;
        }
        if (!CreateProcessW(wexe, wcmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, wdir, &si, &pi)) {
            Log("CreateProcessW failed err=%lu", (unsigned long)GetLastError());
            if (g_log) fclose(g_log);
            return 1;
        }
    }
    Log("spawned pid=%lu (suspended)", (unsigned long)pi.dwProcessId);

    /* fingerprint sidecar for the framework DLL to log */
    _snprintf(launchJsonPath, sizeof(launchJsonPath), "%s\\launch-%lu.json", logDir, (unsigned long)pi.dwProcessId);
    launchJsonPath[sizeof(launchJsonPath) - 1] = 0;
    {
        FILE *f = fopen(launchJsonPath, "wb");
        if (f) {
            fprintf(f, "loader_version=%s\n", LOADER_VERSION);
            fprintf(f, "exe=%s\n", exePath);
            fprintf(f, "sha256=%s\n", hex);
            fprintf(f, "size=%llu\n", fileSize);
            fprintf(f, "verified=%d\n", strcmp(hex, EXPECTED_SHA256) == 0);
            fclose(f);
        }
    }

    remoteMem = VirtualAllocEx(pi.hProcess, NULL, (wcslen(wdll) + 1) * sizeof(WCHAR), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { Log("VirtualAllocEx failed err=%lu", (unsigned long)GetLastError()); TerminateProcess(pi.hProcess, 9); if (g_log) fclose(g_log); return 1; }
    if (!WriteProcessMemory(pi.hProcess, remoteMem, wdll, (wcslen(wdll) + 1) * sizeof(WCHAR), &written) || written != (wcslen(wdll) + 1) * sizeof(WCHAR)) {
        Log("WriteProcessMemory failed err=%lu", (unsigned long)GetLastError());
        TerminateProcess(pi.hProcess, 9); if (g_log) fclose(g_log); return 1;
    }
    pLoadLibraryW = (HMODULE (WINAPI *)(LPCWSTR))GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!pLoadLibraryW) { Log("GetProcAddress(LoadLibraryW) failed"); TerminateProcess(pi.hProcess, 9); if (g_log) fclose(g_log); return 1; }
    hThread = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryW, remoteMem, 0, NULL);
    if (!hThread) { Log("CreateRemoteThread failed err=%lu", (unsigned long)GetLastError()); TerminateProcess(pi.hProcess, 9); if (g_log) fclose(g_log); return 1; }
    WaitForSingleObject(hThread, 15000);
    GetExitCodeThread(hThread, &remoteResult);
    CloseHandle(hThread);
    VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);
    if (remoteResult == 0) { Log("remote LoadLibraryW returned 0 (injection failed, err=%lu)", (unsigned long)GetLastError()); TerminateProcess(pi.hProcess, 9); if (g_log) fclose(g_log); return 4; }
    injected = 1;
    Log("remote LoadLibraryW ok HMODULE=0x%08lX", (unsigned long)remoteResult);
    ResumeThread(pi.hThread);
    Log("resumed main thread");

    _snprintf(initPath, sizeof(initPath), "%s\\init-%lu.done", logDir, (unsigned long)pi.dwProcessId);
    initPath[sizeof(initPath) - 1] = 0;
    if (WaitForFile(initPath, (DWORD)waitMs, initContent, sizeof(initContent))) {
        int ok = (strstr(initContent, "status=ok") != NULL);
        int degraded = (strstr(initContent, "status=degraded") != NULL);
        Log("framework init marker found:\n%s", initContent);
        if (ok) Log("framework initialized: OK (verified build)");
        else if (degraded) Log("framework initialized: DEGRADED (version gate did not confirm)");
        else { Log("framework initialized: FAIL status reported"); rc = 2; }
    } else {
        Log("framework init marker NOT found within %d ms", waitMs);
        rc = 2;
    }

    if (g_schedCount > 0) {
        qsort(g_sched, g_schedCount, sizeof(SchedEvent), CmpSched);
        g_schedPid = pi.dwProcessId;
        CreateThread(NULL, 0, SchedThread, NULL, 0, NULL);
        Log("sched: %d automated events enabled", g_schedCount);
    }

    if (testExit >= 0) {
        HWND hwnd;
        DWORD wait;
        Log("test-exit: sleeping %d s before WM_CLOSE", testExit);
        Sleep((DWORD)testExit * 1000);
        hwnd = FindMainWindow(pi.dwProcessId);
        Log("test-exit: main window hwnd=%p (pid %lu)", (void *)hwnd, (unsigned long)pi.dwProcessId);
        if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        wait = WaitForSingleObject(pi.hProcess, 20000);
        if (wait == WAIT_TIMEOUT) { Log("test-exit: still alive after WM_CLOSE, terminating"); TerminateProcess(pi.hProcess, 0); WaitForSingleObject(pi.hProcess, 5000); }
    } else {
        Log("waiting for game exit (close the game window to finish)");
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        Log("game exit code=0x%08lX (%lu)", (unsigned long)code, (unsigned long)code);
        if (code == 0xC0000005UL) Log("note: 0xC0000005 on WM_CLOSE is a known native SL defect (no framework involved)");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (injected) Log("done (rc=%d)", rc);
    if (g_log) { fclose(g_log); g_log = NULL; }
    return rc;
}

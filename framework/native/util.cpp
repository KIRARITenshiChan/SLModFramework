#include "common.h"
#include "plugin_host.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

FrameworkState g_fw;
static SRWLOCK g_logLock = SRWLOCK_INIT;
static volatile LONG g_reloadPlugins;

void EnsureDirA(const char *path)
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

static char g_logPath[MAX_PATH];
static unsigned long long g_logRotateBytes = SLMODF_LOG_ROTATE_BYTES_DEFAULT;

/* P8.2: remember the session log path and allow tests to shrink the rotation threshold. */
void LogSetPath(const char *path)
{
    const char *env;
    if (!path) return;
    lstrcpynA(g_logPath, path, MAX_PATH);
    lstrcpynA(g_fw.logPath, path, MAX_PATH);
    env = getenv("SLMODF_LOG_ROTATE_BYTES");
    if (env && env[0]) {
        unsigned long long v = _strtoui64(env, NULL, 10);
        if (v >= 4096) {
            g_logRotateBytes = v;
            Logf("log| rotation threshold overridden by env: %lu bytes", (unsigned long)v);
        }
    }
}

/* Move log -> log.1 -> ... -> log.N (oldest dropped), then reopen a fresh file.
 * Called with g_logLock held; must not call Logf (SRWLock is not recursive). */
static void RotateLogLocked(void)
{
    char oldPath[MAX_PATH], newPath[MAX_PATH];
    SYSTEMTIME st;
    int i;
    if (!g_fw.log || !g_logPath[0]) return;
    fclose(g_fw.log);
    g_fw.log = NULL;
    wsprintfA(oldPath, "%s.%d", g_logPath, SLMODF_LOG_ROTATE_BACKUPS);
    DeleteFileA(oldPath);
    for (i = SLMODF_LOG_ROTATE_BACKUPS - 1; i >= 1; --i) {
        wsprintfA(oldPath, "%s.%d", g_logPath, i);
        wsprintfA(newPath, "%s.%d", g_logPath, i + 1);
        MoveFileExA(oldPath, newPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    }
    wsprintfA(newPath, "%s.1", g_logPath);
    MoveFileExA(g_logPath, newPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    g_fw.log = fopen(g_logPath, "ab");
    if (!g_fw.log) return;
    GetLocalTime(&st);
    fprintf(g_fw.log, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [pid %lu] [log] rotated (threshold %lu bytes, %d backups)\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            (unsigned long)g_fw.pid, (unsigned long)g_logRotateBytes, SLMODF_LOG_ROTATE_BACKUPS);
    fflush(g_fw.log);
}

void Logf(const char *fmt, ...)
{
    SYSTEMTIME st;
    va_list ap;
    long pos;
    if (!g_fw.log) return;
    AcquireSRWLockExclusive(&g_logLock);
    GetLocalTime(&st);
    fprintf(g_fw.log, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [pid %lu] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, (unsigned long)g_fw.pid);
    va_start(ap, fmt);
    vfprintf(g_fw.log, fmt, ap);
    va_end(ap);
    fputc('\n', g_fw.log);
    fflush(g_fw.log);
    pos = ftell(g_fw.log);
    if (g_logRotateBytes > 0 && pos >= 0 && (unsigned long long)pos >= g_logRotateBytes)
        RotateLogLocked();
    ReleaseSRWLockExclusive(&g_logLock);
}

bool ReadSidecarFingerprint(void)
{
    char path[MAX_PATH];
    FILE *f;
    char buf[512];
    wsprintfA(path, "%s\\launch-%lu.json", g_fw.logDir, (unsigned long)g_fw.pid);
    f = fopen(path, "rb");
    if (!f) return false;
    while (fgets(buf, sizeof(buf), f)) {
        char *p;
        size_t len;
        if (strncmp(buf, "sha256=", 7) == 0) {
            p = buf + 7;
            len = strlen(p);
            while (len && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = 0;
            if (len == 64) memcpy(g_fw.sha256, p, 65);
        } else if (strncmp(buf, "size=", 5) == 0) {
            g_fw.exeSize = _strtoui64(buf + 5, NULL, 10);
        } else if (strncmp(buf, "verified=", 9) == 0) {
            /* sidecar writes the loader's own hash comparison; keep it so logs/reports can
             * distinguish "loaded via loader on the verified build" from "--no-verify". */
            g_fw.verified = atoi(buf + 9) ? 1 : 0;
        }
    }
    fclose(f);
    return g_fw.sha256[0] != 0;
}

static int ParseHotkey(const char *v)
{
    if (!_stricmp(v, "insert")) return VK_INSERT;
    if (!_stricmp(v, "f1")) return VK_F1;
    if (!_stricmp(v, "f2")) return VK_F2;
    if (!_stricmp(v, "f3")) return VK_F3;
    if (!_stricmp(v, "f4")) return VK_F4;
    if (!_stricmp(v, "f5")) return VK_F5;
    if (!_stricmp(v, "f6")) return VK_F6;
    if (!_stricmp(v, "f7")) return VK_F7;
    if (!_stricmp(v, "f8")) return VK_F8;
    if (!_stricmp(v, "f9")) return VK_F9;
    if (!_stricmp(v, "f10")) return VK_F10;
    if (!_stricmp(v, "f11")) return VK_F11;
    if (!_stricmp(v, "f12")) return VK_F12;
    if (!_stricmp(v, "home")) return VK_HOME;
    if (!_stricmp(v, "end")) return VK_END;
    if (!_stricmp(v, "delete")) return VK_DELETE;
    if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        int hx = (int)strtol(v + 2, NULL, 16);
        if (hx > 0 && hx < 256) return hx;
        return 0;
    }
    {
        int dec = atoi(v);
        if (dec > 0 && dec < 256) return dec;
    }
    return 0;
}

static void Trim(char **pp)
{
    char *p = *pp;
    char *end;
    while (*p == ' ' || *p == '\t') p++;
    end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
    *pp = p;
}

static void ConfigDirFromPath(const char *cfgPath, char *out, size_t outLen)
{
    const char *p = strrchr(cfgPath, '\\');
    size_t n = p ? (size_t)(p - cfgPath) : 0;
    if (n >= outLen) n = outLen - 1;
    memcpy(out, cfgPath, n);
    out[n] = 0;
}

static void WriteDefaultConfig(const char *path)
{
    char dir[MAX_PATH];
    FILE *f;
    ConfigDirFromPath(path, dir, sizeof(dir));
    EnsureDirA(dir);
    f = fopen(path, "wb");
    if (!f) {
        Logf("config| WARN cannot create default config %s", path);
        return;
    }
    fprintf(f,
        "# SLModFramework configuration\n"
        "# config_version=%d is required; unknown/invalid files are backed up\n"
        "# as framework.ini.bad-<timestamp> and replaced with these defaults.\n"
        "config_version=%d\n"
        "\n"
        "# GUI hotkey: insert | f1..f12 | home | end | delete | 0xNN | decimal VK\n"
        "hotkey=insert\n"
        "\n"
        "# persistent HUD strip (0/1)\n"
        "hud=0\n"
        "\n"
        "# install the D3D9 hooks (0 disables the overlay entirely)\n"
        "hook_d3d9=1\n"
        "\n"
        "# max time to wait for mmfs2.dll after injection (0..120000 ms)\n"
        "wait_mmfs2_ms=60000\n"
        "\n"
        "# write JSONL state snapshots to logs/state/ (0/1; debug / ThreatTelegraphs evidence)\n"
        "record=0\n"
        "\n"
        "# snapshot interval while recording (100..10000 ms; scene changes are always recorded)\n"
        "record_interval_ms=500\n"
        "\n"
        "# Phase 6: read-only DirectSound probe (logs buffer create/play + sample match; 0/1)\n"
        "audio_probe=0\n"
        "\n"
        "# Phase 6: attenuate identified jumpscare scream buffers (0/1; P6b, requires audio_probe)\n"
        "audio_soften=0\n"
        "\n"
        "# Phase 6: target gain percent for screams when audio_soften=1 (1..100)\n"
        "audio_gain_percent=25\n"
        "\n"
        "# QoL: release the mouse cursor when the game is unfocused or the GUI is open (0/1)\n"
        "qol_release_mouse=1\n"
        "\n"
        "# QoL: numpad digits 1..9 answer the frame-19 Baby keypad (0/1)\n"
        "qol_keypad_numpad=1\n"
        "\n"
        "# QoL: A/D/W/S close doors/flip the panel in the frame-27 Private Room (0/1)\n"
        "qol_private_hotkeys=1\n"
        "\n"
        "# QoL: play the local Doorslam WAV when a Private Room hotkey toggles a door (0/1)\n"
        "qol_door_sfx=1\n"
        "\n"
        "# Overlay UI font: full path to a .ttf/.ttc; empty = auto-detect a system CJK font\n"
        "font_path=\n"
        "\n"
        "# Overlay UI base font size in px (10..40; DPI scale is applied on top)\n"
        "font_size=14\n",
        SLMODF_CONFIG_VERSION, SLMODF_CONFIG_VERSION);
    fclose(f);
    Logf("config| wrote default config %s (config_version=%d)", path, SLMODF_CONFIG_VERSION);
}

static int BackupBadConfig(const char *path, char *outBad, size_t outLen)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfA(outBad, "%s.bad-%04d%02d%02d-%02d%02d%02d", path,
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (MoveFileExA(path, outBad, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        return 1;
    outBad[0] = 0;
    return 0;
}

void LoadConfig(void)
{
    char path[MAX_PATH];
    FILE *f;
    long size;
    char *data = NULL;
    int invalidLines = 0, knownKeys = 0, unknownKeys = 0, lineCount = 0;
    int duplicateVersion = 0, binary = 0;
    g_fw.hotkeyVK = VK_INSERT;
    g_fw.hudEnabled = 0;
    g_fw.hookD3D9 = 1;
    g_fw.mmfs2WaitMs = 60000;
    g_fw.recordEnabled = 0;
    g_fw.recordIntervalMs = 500;
    g_fw.audioProbe = 0;
    g_fw.audioSoften = 0;
    g_fw.audioGainPercent = 25;
    g_fw.qolReleaseMouse = 1;
    g_fw.qolKeypadNumpad = 1;
    g_fw.qolPrivateHotkeys = 1;
    g_fw.qolDoorSfx = 1;
    g_fw.fontPath[0] = 0;
    g_fw.fontSize = 14;
    g_fw.configVersion = 0;
    g_fw.configRecovered = 0;
    wsprintfA(path, "%s\\..\\..\\config\\framework.ini", g_fw.logDir);
    lstrcpynA(g_fw.cfgPath, path, MAX_PATH);
    wsprintfA(g_fw.pluginsDir, "%s\\..\\..\\plugins", g_fw.logDir);

    f = fopen(path, "rb");
    if (!f) {
        Logf("config| not found (%s), using defaults: hotkey=Insert hud=0", path);
        WriteDefaultConfig(path);
        g_fw.configVersion = SLMODF_CONFIG_VERSION;
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 65536) {
        fclose(f);
        Logf("config| %s size=%ld is invalid (empty or >64 KiB)", path, size);
        goto recover;
    }
    data = (char *)malloc((size_t)size + 1);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        if (data) free(data);
        fclose(f);
        Logf("config| read failed for %s", path);
        goto recover;
    }
    data[size] = 0;
    fclose(f);
    {
        long i;
        for (i = 0; i < size; i++) {
            if (data[i] == 0) { binary = 1; break; }
        }
    }
    {
        char *line = data;
        while (line && *line) {
            char *next = strchr(line, '\n');
            char *eq, *k, *v;
            if (next) *next++ = 0;
            lineCount++;
            if (line[0] == 0 || line[0] == '\r' || line[0] == '#' || line[0] == ';' || line[0] == '[') {
                line = next;
                continue;
            }
            eq = strchr(line, '=');
            if (!eq) { invalidLines++; line = next; continue; }
            *eq = 0;
            k = line; v = eq + 1;
            Trim(&k); Trim(&v);
            if (!_stricmp(k, "config_version")) {
                int ver = atoi(v);
                if (g_fw.configVersion != 0) duplicateVersion = 1;
                g_fw.configVersion = ver;
                knownKeys++;
            } else if (!_stricmp(k, "hotkey")) {
                int vk = ParseHotkey(v);
                if (vk) g_fw.hotkeyVK = vk;
                else Logf("config| warn invalid hotkey '%s', keeping default VK 0x%02X", v, g_fw.hotkeyVK);
                knownKeys++;
            } else if (!_stricmp(k, "hud")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.hudEnabled = b;
                else Logf("config| warn hud='%s' is not 0/1, keeping %d", v, g_fw.hudEnabled);
                knownKeys++;
            } else if (!_stricmp(k, "hook_d3d9")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.hookD3D9 = b;
                else Logf("config| warn hook_d3d9='%s' is not 0/1, keeping %d", v, g_fw.hookD3D9);
                knownKeys++;
            } else if (!_stricmp(k, "wait_mmfs2_ms")) {
                int ms = atoi(v);
                if (ms >= 0 && ms <= 120000) g_fw.mmfs2WaitMs = ms;
                knownKeys++;
            } else if (!_stricmp(k, "record")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.recordEnabled = b;
                else Logf("config| warn record='%s' is not 0/1, keeping %d", v, g_fw.recordEnabled);
                knownKeys++;
            } else if (!_stricmp(k, "audio_probe")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.audioProbe = b;
                else Logf("config| warn audio_probe='%s' is not 0/1, keeping %d", v, g_fw.audioProbe);
                knownKeys++;
            } else if (!_stricmp(k, "audio_soften")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.audioSoften = b;
                else Logf("config| warn audio_soften='%s' is not 0/1, keeping %d", v, g_fw.audioSoften);
                knownKeys++;
            } else if (!_stricmp(k, "audio_gain_percent")) {
                int g = atoi(v);
                if (g >= 1 && g <= 100) g_fw.audioGainPercent = g;
                else Logf("config| warn audio_gain_percent='%s' outside 1..100, keeping %d", v, g_fw.audioGainPercent);
                knownKeys++;
            } else if (!_stricmp(k, "qol_release_mouse")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.qolReleaseMouse = b;
                else Logf("config| warn qol_release_mouse='%s' is not 0/1, keeping %d", v, g_fw.qolReleaseMouse);
                knownKeys++;
            } else if (!_stricmp(k, "qol_keypad_numpad")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.qolKeypadNumpad = b;
                else Logf("config| warn qol_keypad_numpad='%s' is not 0/1, keeping %d", v, g_fw.qolKeypadNumpad);
                knownKeys++;
            } else if (!_stricmp(k, "qol_private_hotkeys")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.qolPrivateHotkeys = b;
                else Logf("config| warn qol_private_hotkeys='%s' is not 0/1, keeping %d", v, g_fw.qolPrivateHotkeys);
                knownKeys++;
            } else if (!_stricmp(k, "qol_door_sfx")) {
                int b = atoi(v);
                if (b == 0 || b == 1) g_fw.qolDoorSfx = b;
                else Logf("config| warn qol_door_sfx='%s' is not 0/1, keeping %d", v, g_fw.qolDoorSfx);
                knownKeys++;
            } else if (!_stricmp(k, "font_path")) {
                lstrcpynA(g_fw.fontPath, v, MAX_PATH);
                knownKeys++;
            } else if (!_stricmp(k, "font_size")) {
                int fs = atoi(v);
                if (fs >= 10 && fs <= 40) g_fw.fontSize = fs;
                else Logf("config| warn font_size='%s' outside 10..40, keeping %d", v, g_fw.fontSize);
                knownKeys++;
            } else if (!_stricmp(k, "record_interval_ms")) {
                int ms = atoi(v);
                if (ms >= 100 && ms <= 10000) g_fw.recordIntervalMs = ms;
                else Logf("config| warn record_interval_ms='%s' outside 100..10000, keeping %d", v, g_fw.recordIntervalMs);
                knownKeys++;
            } else {
                unknownKeys++;
            }
            line = next;
        }
    }
    free(data);
    data = NULL;

    if (binary || invalidLines >= 3) {
        Logf("config| %s looks corrupt (binary=%d invalid_lines=%d lines=%d)",
             path, binary, invalidLines, lineCount);
        goto recover;
    }
    if (g_fw.configVersion == 0) {
        /* v0 files predate the version field: accept them as v1 (migration, no rewrite). */
        Logf("config| no config_version in %s, assuming v%d (migration)", path, SLMODF_CONFIG_VERSION);
        g_fw.configVersion = SLMODF_CONFIG_VERSION;
    } else if (g_fw.configVersion != SLMODF_CONFIG_VERSION || duplicateVersion) {
        Logf("config| unsupported config_version=%d (duplicate=%d, expected %d)",
             g_fw.configVersion, duplicateVersion, SLMODF_CONFIG_VERSION);
        goto recover;
    }
    if (lineCount > 0 && knownKeys == 0 && unknownKeys > 0) {
        Logf("config| %s has %d unknown keys and no known key", path, unknownKeys);
        goto recover;
    }
    Logf("config| %s version=%d hotkey=VK 0x%02X hud=%d hook_d3d9=%d wait_mmfs2_ms=%d record=%d record_interval_ms=%d audio_probe=%d audio_soften=%d audio_gain_percent=%d qol_release_mouse=%d qol_keypad_numpad=%d qol_private_hotkeys=%d qol_door_sfx=%d font_path='%s' font_size=%d known=%d unknown=%d",
         path, g_fw.configVersion, g_fw.hotkeyVK, g_fw.hudEnabled, g_fw.hookD3D9,
         g_fw.mmfs2WaitMs, g_fw.recordEnabled, g_fw.recordIntervalMs,
         g_fw.audioProbe, g_fw.audioSoften, g_fw.audioGainPercent, g_fw.qolReleaseMouse,
         g_fw.qolKeypadNumpad, g_fw.qolPrivateHotkeys, g_fw.qolDoorSfx, g_fw.fontPath,
         g_fw.fontSize, knownKeys, unknownKeys);
    return;

recover:
    if (data) free(data);
    {
        char bad[MAX_PATH];
        int ok = BackupBadConfig(path, bad, sizeof(bad));
        Logf("config| RECOVERED: bad config moved to %s", ok ? bad : "<move failed>");
    }
    g_fw.hotkeyVK = VK_INSERT;
    g_fw.hudEnabled = 0;
    g_fw.hookD3D9 = 1;
    g_fw.mmfs2WaitMs = 60000;
    g_fw.recordEnabled = 0;
    g_fw.recordIntervalMs = 500;
    g_fw.audioProbe = 0;
    g_fw.audioSoften = 0;
    g_fw.audioGainPercent = 25;
    g_fw.qolReleaseMouse = 1;
    g_fw.qolKeypadNumpad = 1;
    g_fw.qolPrivateHotkeys = 1;
    g_fw.qolDoorSfx = 1;
    g_fw.fontPath[0] = 0;
    g_fw.fontSize = 14;
    g_fw.configVersion = SLMODF_CONFIG_VERSION;
    g_fw.configRecovered = 1;
    WriteDefaultConfig(path);
    Logf("config| defaults restored: hotkey=VK 0x%02X hud=%d hook_d3d9=%d wait_mmfs2_ms=%d",
         g_fw.hotkeyVK, g_fw.hudEnabled, g_fw.hookD3D9, g_fw.mmfs2WaitMs);
}

const char *GateStatusName(void)
{
    switch (g_fw.gate) {
    case 1: return "ok";
    case 2: return "degraded";
    case 3: return "fail";
    default: return "unknown";
    }
}

void ToggleGui(const char *source)
{
    InterlockedIncrement(&g_fw.hotkeyToggles);
    InterlockedXor(&g_fw.guiVisible, 1);
    Logf("gui| toggled by %s -> visible=%ld toggles=%ld", source,
         (long)g_fw.guiVisible, (long)g_fw.hotkeyToggles);
}

void RequestReloadPlugins(void) { InterlockedExchange(&g_reloadPlugins, 1); }
int ConsumeReloadPlugins(void) { return InterlockedExchange(&g_reloadPlugins, 0); }


/* ---------------- P8.2: config reset + diagnostics bundle ---------------- */

static void CopyFileToDir(const char *src, const char *dir)
{
    char dst[MAX_PATH];
    const char *name;
    if (!src || !src[0] || !dir) return;
    name = strrchr(src, '\\');
    name = name ? name + 1 : src;
    wsprintfA(dst, "%s\\%s", dir, name);
    CopyFileA(src, dst, FALSE);
}

static void DiagCopyPattern(const char *dir, const char *pattern, int maxFiles)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pat[MAX_PATH];
    int copied = 0;
    wsprintfA(pat, "%s\\%s", g_fw.logDir, pattern);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char src[MAX_PATH];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wsprintfA(src, "%s\\%s", g_fw.logDir, fd.cFileName);
        CopyFileToDir(src, dir);
        copied++;
    } while (copied < maxFiles && FindNextFileA(h, &fd));
    FindClose(h);
}

static void DiagCopyPluginManifests(const char *baseDir)
{
    char dirPath[MAX_PATH], sub[MAX_PATH], pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    wsprintfA(dirPath, "%s\\plugin-manifests", baseDir);
    EnsureDirA(dirPath);
    wsprintfA(pat, "%s\\*", g_fw.pluginsDir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char dst[MAX_PATH];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        wsprintfA(sub, "%s\\%s\\plugin.ini", g_fw.pluginsDir, fd.cFileName);
        wsprintfA(dst, "%s\\%s-plugin.ini", dirPath, fd.cFileName);
        CopyFileA(sub, dst, FALSE);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void DiagWriteSummary(const char *dir)
{
    char path[MAX_PATH];
    FILE *f;
    SYSTEMTIME st;
    int i, n;
    wsprintfA(path, "%s\\summary.txt", dir);
    f = fopen(path, "wb");
    if (!f) return;
    GetLocalTime(&st);
    fprintf(f, "SLModFramework diagnostics\n");
    fprintf(f, "version=%s api=%d\n", SLMODF_VERSION, SLMODF_PLUGIN_API_VERSION);
    fprintf(f, "generated=%04d-%02d-%02d %02d:%02d:%02d\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    fprintf(f, "pid=%lu\n", (unsigned long)g_fw.pid);
    fprintf(f, "exe=%s\nsha256=%s\nsize=%lu\n", g_fw.exePath, g_fw.sha256, (unsigned long)g_fw.exeSize);
    fprintf(f, "framework=%s\nlog_dir=%s\nconfig=%s\nplugins=%s\n",
            g_fw.frameworkDir, g_fw.logDir, g_fw.cfgPath, g_fw.pluginsDir);
    fprintf(f, "gate=%s verified=%d config_version=%d config_recovered=%d\n",
            GateStatusName(), g_fw.verified, g_fw.configVersion, g_fw.configRecovered);
    fprintf(f, "config: hotkey=VK 0x%02X hud=%d hook_d3d9=%d wait_mmfs2_ms=%d record=%d interval=%d\n",
            g_fw.hotkeyVK, g_fw.hudEnabled, g_fw.hookD3D9, g_fw.mmfs2WaitMs,
            g_fw.recordEnabled, g_fw.recordIntervalMs);
    fprintf(f, "config: audio_probe=%d audio_soften=%d gain=%d qol_mouse=%d keypad=%d private=%d door_sfx=%d\n",
            g_fw.audioProbe, g_fw.audioSoften, g_fw.audioGainPercent, g_fw.qolReleaseMouse,
            g_fw.qolKeypadNumpad, g_fw.qolPrivateHotkeys, g_fw.qolDoorSfx);
    fprintf(f, "config: font_path='%s' font_size=%d\n", g_fw.fontPath, g_fw.fontSize);
    fprintf(f, "plugins: %s\n", plugins::Summary());
    n = plugins::Count();
    for (i = 0; i < n; i++) {
        const plugins::View *v = plugins::Get(i);
        if (!v) continue;
        fprintf(f, "  - id=%s name=%s version=%s api=%d enabled=%d status=%d updates=%d errors=%d%s\n",
                v->id ? v->id : "?", v->name ? v->name : "?", v->version ? v->version : "?",
                v->apiVersion, v->enabled, v->status, v->updates, v->errors,
                v->error && v->error[0] ? v->error : "");
    }
    fclose(f);
}

void ResetConfigAndReload(void)
{
    SYSTEMTIME st;
    char bak[MAX_PATH];
    BOOL ok;
    GetLocalTime(&st);
    wsprintfA(bak, "%s.bak-%04d%02d%02d-%02d%02d%02d", g_fw.cfgPath,
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    ok = CopyFileA(g_fw.cfgPath, bak, FALSE);
    Logf("config| reset requested: backup=%s ok=%d", bak, ok ? 1 : 0);
    WriteDefaultConfig(g_fw.cfgPath);
    LoadConfig();
    Logf("config| reset applied: hotkey=VK 0x%02X hud=%d qol(mouse=%d keypad=%d private=%d sfx=%d) audio(probe=%d soften=%d gain=%d)",
         g_fw.hotkeyVK, g_fw.hudEnabled, g_fw.qolReleaseMouse, g_fw.qolKeypadNumpad,
         g_fw.qolPrivateHotkeys, g_fw.qolDoorSfx, g_fw.audioProbe, g_fw.audioSoften,
         g_fw.audioGainPercent);
}

const char *ExportDiagnostics(void)
{
    static char out[MAX_PATH];
    SYSTEMTIME st;
    char name[64], dir[MAX_PATH], root[MAX_PATH], tar[MAX_PATH], zip[MAX_PATH];
    char cmd[2048];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD waitRc = WAIT_FAILED;
    GetLocalTime(&st);
    wsprintfA(name, "diagnostics-%04d%02d%02d-%02d%02d%02d",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wsprintfA(dir, "%s\\%s", g_fw.logDir, name);
    EnsureDirA(dir);
    DiagWriteSummary(dir);
    CopyFileToDir(g_fw.cfgPath, dir);
    DiagCopyPattern(dir, "slmodf-*.log*", 24);
    DiagCopyPattern(dir, "harness-*.log*", 12);
    DiagCopyPattern(dir, "loader-*.log", 8);
    DiagCopyPattern(dir, "launch-*.json", 8);
    DiagCopyPattern(dir, "init-*.done", 8);
    DiagCopyPluginManifests(dir);

    wsprintfA(root, "C:\\Windows");
    GetEnvironmentVariableA("SystemRoot", root, MAX_PATH);
    wsprintfA(tar, "%s\\System32\\tar.exe", root);
    if (GetFileAttributesA(tar) != INVALID_FILE_ATTRIBUTES) {
        wsprintfA(zip, "%s\\%s.zip", g_fw.logDir, name);
        wsprintfA(cmd, "\"%s\" -a -cf \"%s\" -C \"%s\" \"%s\"", tar, zip, g_fw.logDir, name);
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            waitRc = WaitForSingleObject(pi.hProcess, 20000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        if (GetFileAttributesA(zip) != INVALID_FILE_ATTRIBUTES) {
            lstrcpynA(out, zip, MAX_PATH);
            Logf("diag| saved %s (tar wait=%lu)", out, (unsigned long)waitRc);
            return out;
        }
        Logf("diag| tar zip failed (tar=%s wait=%lu); folder kept", tar, (unsigned long)waitRc);
    } else {
        Logf("diag| tar.exe not found; folder kept");
    }
    lstrcpynA(out, dir, MAX_PATH);
    Logf("diag| saved %s", out);
    return out;
}


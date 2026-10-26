#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>

#define SLMODF_NAME "SLModFramework"
#define SLMODF_VERSION "1.0.0"

/* config file schema version (see config/framework.ini.example + docs/PLUGIN_API_V0.md) */
#define SLMODF_CONFIG_VERSION 1
/* plugin API version implemented by this build */
#define SLMODF_PLUGIN_API_VERSION 1

/* P8.2: session log rotation (default 4 MiB x 5 backups; env SLMODF_LOG_ROTATE_BYTES overrides
 * the threshold downward for tests, minimum 4096) */
#define SLMODF_LOG_ROTATE_BYTES_DEFAULT (4u * 1024u * 1024u)
#define SLMODF_LOG_ROTATE_BACKUPS 5

/* P8.2: a plugin that raises this many consecutive Lua errors is auto-disabled and its
 * manifest is rewritten with enabled=0 so the next launch does not crash-loop. */
#define SLMODF_PLUGIN_AUTO_DISABLE_ERRORS 30

struct FrameworkState {
    FILE *log;
    DWORD pid;
    char exePath[MAX_PATH];
    char exeDir[MAX_PATH];
    char frameworkDir[MAX_PATH]; /* P8 layout B: <game>\SLModFramework */
    char logDir[MAX_PATH];
    char logPath[MAX_PATH]; /* current session log file (set by LogSetPath; used for rotation) */
    char cfgPath[MAX_PATH];
    char pluginsDir[MAX_PATH];
    char sha256[65];
    unsigned long long exeSize;
    int verified;
    HMODULE hMmfs2;
    unsigned long pamuAddr;
    unsigned rt, pv, pb;
    int gate; /* 0 unknown, 1 ok, 2 degraded, 3 fail */
    int hotkeyVK;
    int hudEnabled;
    int hookD3D9;
    int mmfs2WaitMs;
    int audioProbe;          /* install read-only DirectSound probe (config: audio_probe=0/1) */
    int audioSoften;         /* P6b: attenuate identified jumpscare scream buffers (0/1) */
    int audioGainPercent;    /* P6b target gain for screams, 1..100 */
    int qolReleaseMouse;     /* P8: release ClipCursor while unfocused/GUI open (config: qol_release_mouse=0/1) */
    int qolKeypadNumpad;     /* P8: numpad digits drive the frame-19 keypad (config: qol_keypad_numpad=0/1) */
    int qolPrivateHotkeys;   /* P8: A/D/W/S door hotkeys in the frame-27 Private Room (config: qol_private_hotkeys=0/1) */
    int qolDoorSfx;          /* P8: play the local Doorslam WAV when Private Room hotkeys toggle a door (config: qol_door_sfx=0/1) */
    char fontPath[MAX_PATH]; /* overlay font override; empty = auto-detect a system CJK font (config: font_path=...) */
    int fontSize;            /* overlay base font size in px, DPI scale applied on top (config: font_size=10..40) */
    int recordEnabled;       /* write JSONL state snapshots (config: record=0/1) */
    int recordIntervalMs;    /* snapshot interval while enabled */
    int configVersion;      /* parsed config_version; 0 = missing */
    int configRecovered;    /* 1 = an unreadable/unknown config was backed up and replaced */
    volatile LONG guiVisible;
    volatile LONG swallowedKeys;
    volatile LONG swallowedMouse;
    volatile LONG hotkeyToggles;
    volatile LONG closing; /* set on WM_CLOSE/WM_QUERYENDSESSION: stop drawing, no exit callback */
    HWND hwnd;
    void *device;
};

extern FrameworkState g_fw;

void Logf(const char *fmt, ...);
/* P8.2: tell the logger which file it is appending to (enables rotation + diagnostics). */
void LogSetPath(const char *path);
/* P8.2: back up framework.ini as .bak-<ts>, rewrite defaults and reload them in-process. */
void ResetConfigAndReload(void);
/* P8.2: write <framework>\logs\diagnostics-<ts>\ + best-effort zip via Windows tar.exe.
 * Returns the zip path, or the folder path when tar is unavailable. */
const char *ExportDiagnostics(void);
void EnsureDirA(const char *path);
bool ReadSidecarFingerprint(void);
void LoadConfig(void);
const char *GateStatusName(void);
void ToggleGui(const char *source);
/* Deferred UI action shared between the ImGui draw thread and the worker. */
void RequestReloadPlugins(void);
int ConsumeReloadPlugins(void);

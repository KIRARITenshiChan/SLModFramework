#pragma once
#include <windows.h>

/* Phase 4 plugin host (API v0). All functions are called from the render thread
 * (the thread that owns the D3D9 device / ImGui context). The worker thread only
 * touches g_fw fields and logging. */
namespace plugins {

enum Status {
    kDisabled = 0,   /* manifest enabled=0: not loaded */
    kLoaded = 1,
    kApiMismatch = 2,
    kError = 3,       /* syntax/runtime error while loading (plugin_info/entry) */
    kInvalid = 4      /* missing/invalid plugin.ini */
};

struct View {
    const char *dir;
    const char *id;
    const char *name;
    const char *version;
    int apiVersion;
    int enabled;
    int status;
    int updates;      /* on_update calls made */
    int errors;       /* captured Lua errors for this plugin */
    int autoDisabled; /* P8.2: disabled automatically after repeated consecutive errors */
    const char *error;/* last error, "" when none */
};

void SetRootForTest(const char *dir); /* harness: override <framework>\plugins */
void EnsureLoaded(void);              /* lazy scan+load, idempotent */
void ReloadAll(void);
void OnUpdate(void);                  /* per frame, before ImGui::NewFrame */
void DrawUi(void);                    /* optional plugin-drawn UI (inside ImGui window) */
void DrawOverlay(void);               /* optional free-screen overlay (called every frame) */
void Shutdown(void);                  /* on_shutdown + close Lua state */

int Count(void);
const View *Get(int i);
const char *Summary(void);

/* Persist enabled=0/1 into the plugin manifest (atomic replace) and request a reload. */
int SetEnabled(int i, int enabled);

} // namespace plugins

/* recorder.cpp -- writes timestamped JSONL snapshots of scene + counters.
 * Runs on the framework worker thread after state::Poll(); relies on the
 * read-only scene/state modules. Disabled unless record=1 in framework.ini or
 * the GUI checkbox turns it on at runtime.
 */
#include "common.h"
#include "recorder.h"
#include "scene.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

static FILE *g_file;
static char g_path[MAX_PATH];
static int g_enabled;
static DWORD g_lastWrite;
static DWORD g_sceneWaitSince;
static int g_lastScene = -2;
static unsigned long g_lines;
static unsigned long g_flushes;

static void JsonEscape(const char *in, char *out, size_t outLen)
{
    size_t o = 0;
    size_t i;
    if (!outLen) return;
    for (i = 0; in[i] && o + 2 < outLen; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
}

static void OpenFile(void)
{
    SYSTEMTIME st;
    char dir[MAX_PATH];
    if (g_file) return;
    GetLocalTime(&st);
    wsprintfA(dir, "%s\\..\\..\\logs\\state", g_fw.logDir);
    EnsureDirA(dir);
    wsprintfA(g_path, "%s\\state-%04d%02d%02d-%02d%02d%02d-%lu.jsonl", dir,
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
              (unsigned long)g_fw.pid);
    g_file = fopen(g_path, "wb");
    if (!g_file) {
        Logf("recorder| ERROR cannot open %s", g_path);
        g_enabled = 0;
        return;
    }
    Logf("recorder| file=%s interval_ms=%d scene_changes=always", g_path, g_fw.recordIntervalMs);
}

static void WriteSnapshot(const SceneInfo *sc, int sceneChanged)
{
    SYSTEMTIME st;
    char name[96];
    int i, n, written = 0;
    if (!g_file || !sc || !sc->valid) return;
    GetLocalTime(&st);
    JsonEscape(sc->name, name, sizeof(name));
    fprintf(g_file,
            "{\"t\":%lu,\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d.%03d\",\"scene\":%d,"
            "\"scene_name\":\"%s\",\"w\":%u,\"h\":%u,\"layers\":%u,\"instances\":%u,"
            "\"rh\":\"0x%08lX\",\"scene_changed\":%d,\"counters\":{",
            (unsigned long)GetTickCount(), st.wYear, st.wMonth, st.wDay, st.wHour,
            st.wMinute, st.wSecond, st.wMilliseconds, sc->index, name,
            sc->w, sc->h, sc->layers, sc->instances, scene::RunHeaderPtr(),
            sceneChanged ? 1 : 0);
    n = state::CounterCount();
    if (n > 64) n = 64;
    for (i = 0; i < n; ++i) {
        const char *cn;
        char cname[96];
        int dup = 0, j;
        if (!state::CounterValid(i)) continue;
        cn = state::CounterName(i);
        if (!cn || !cn[0]) continue;
        /* The game reuses object names ("compute" x16 in Breaker Room): make the
         * JSON keys unique by suffixing the 2nd, 3rd, ... occurrence. */
        for (j = 0; j < i; ++j) {
            const char *prev;
            if (!state::CounterValid(j)) continue;
            prev = state::CounterName(j);
            if (prev && strcmp(prev, cn) == 0) ++dup;
        }
        JsonEscape(cn, cname, sizeof(cname));
        if (dup) {
            char base[80];
            size_t bl;
            lstrcpynA(base, cname, sizeof(base));
            bl = strlen(base);
            wsprintfA(base + bl, "#%d", dup + 1);
            lstrcpynA(cname, base, sizeof(cname));
        }
        fprintf(g_file, "%s\"%s\":%d", written ? "," : "", cname, state::CounterValue(i));
        written++;
    }
    fprintf(g_file, "}}\n");
    g_lines++;
    /* Flush scene changes and every few periodic lines: the file stays useful
     * if the game is killed without a clean shutdown. */
    if (sceneChanged || (g_lines % 5) == 0) {
        fflush(g_file);
        g_flushes++;
    }
    if (sceneChanged) {
        Logf("recorder| scene %d \"%s\" counters=%d line=%lu", sc->index, sc->name,
             written, g_lines);
    }
}

void recorder::Init(void)
{
    g_enabled = g_fw.recordEnabled ? 1 : 0;
    g_lastScene = -2;
    g_lastWrite = 0;
    g_sceneWaitSince = 0;
    g_lines = 0;
    if (g_enabled)
        Logf("recorder| enabled by config (record_interval_ms=%d)", g_fw.recordIntervalMs);
}

void recorder::SetEnabled(int on, const char *source)
{
    if (!on) {
        if (g_enabled && g_file) {
            fflush(g_file);
            fclose(g_file);
            g_file = NULL;
            Logf("recorder| closed file=%s lines=%lu flushes=%lu source=%s",
                 g_path, g_lines, g_flushes, source ? source : "?");
        } else if (g_enabled) {
            Logf("recorder| disabled (no file opened) source=%s", source ? source : "?");
        }
        g_enabled = 0;
        g_lastScene = -2;
        g_sceneWaitSince = 0;
        return;
    }
    if (g_enabled) return;
    g_enabled = 1;
    g_lastScene = -2;
    g_lastWrite = 0;
    g_sceneWaitSince = 0;
    Logf("recorder| enabled at runtime source=%s interval_ms=%d",
         source ? source : "?", g_fw.recordIntervalMs);
}

void recorder::Poll(void)
{
    const SceneInfo *sc;
    DWORD now;
    if (!g_enabled) return;
    sc = scene::Current();
    if (!sc || !sc->valid) return;
    now = GetTickCount();
    if (sc->index != g_lastScene) {
        /* Wait for the state module to rebuild its OI/object map for the new
         * frame: a scene change is published before state::Poll() has remapped
         * (rebuild costs 200-350 ms), and a scene_changed line without counters
         * is much less useful for the ThreatTelegraphs evidence trail. Force a
         * write after 3 s so a scene that never maps still gets recorded. */
        if (!g_sceneWaitSince) g_sceneWaitSince = now ? now : 1;
        if (!(state::Status() == 1 && state::MapScene() == sc->index &&
              state::ValuesReady()) &&
            (DWORD)(now - g_sceneWaitSince) < 3000)
            return;
        OpenFile();
        if (!g_file) return;
        WriteSnapshot(sc, 1);
        g_lastScene = sc->index;
        g_lastWrite = now;
        g_sceneWaitSince = 0;
        return;
    }
    g_sceneWaitSince = 0;
    if (g_fw.recordIntervalMs > 0 && (now - g_lastWrite) >= (DWORD)g_fw.recordIntervalMs) {
        WriteSnapshot(sc, 0);
        g_lastWrite = now;
    }
}

int recorder::Enabled(void) { return g_enabled; }
const char *recorder::Path(void) { return g_file ? g_path : ""; }

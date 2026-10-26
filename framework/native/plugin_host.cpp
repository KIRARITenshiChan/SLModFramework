/* Phase 4 plugin host: API v0 (Lua 5.4, sandboxed).
 *
 * Plugin layout: <framework>\plugins\<dir>\plugin.ini + entry .lua
 *   plugin.ini: id, name, version, api_version, enabled, entry (default main.lua)
 *
 * Only the render thread touches this module (ImGui context affinity). The Lua
 * state is created lazy on the first presented frame. Each plugin gets its own
 * _ENV table (globals are read-only via __index); no io/os/package/debug, and
 * dofile/loadfile/load/require are removed. Callbacks run under lua_pcall and
 * errors are captured per plugin, so a broken plugin cannot take the game down.
 */
#include "common.h"
#include "plugin_host.h"
#include "d3d9hook.h"
#include "scene.h"
#include "state.h"
#include "audio_hook.h"
#include "imgui.h"
/* Lua headers are plain C: this header has no extern "C" guard of its own. */
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <string.h>
#include <stdlib.h>
#include <float.h>

namespace plugins {

#define MAX_PLUGINS 32

struct Plugin {
    char dir[MAX_PATH];
    char manifest[MAX_PATH];
    char entry[MAX_PATH];
    char id[64];
    char name[96];
    char version[32];
    int apiVersion;
    int enabled;
    int status;
    char err[240];
    int envRef;
    int hasLoad, hasUpdate, hasDraw, hasOverlay, hasShutdown;
    int updates;
    int errors;
    int consecErrors;   /* P8.2: reset on any successful callback */
    int autoDisabled;   /* P8.2: 1 = disabled automatically after repeated errors */
};

/* defined below; used by error paths that must persist enabled=0 */
static void WriteEnabled(Plugin *p, int enabled);

static Plugin g_plugins[MAX_PLUGINS];
static int g_count;
static lua_State *g_L;
static char g_root[MAX_PATH];
static char g_summary[512];
static int g_loaded;
static unsigned long long g_frameId;

/* ---------- small helpers ---------- */

static void Trim(char *s)
{
    char *p = s;
    char *end;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
}

static int FileExists(const char *p)
{
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static void JoinPath(char *out, size_t outLen, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    if (n >= outLen - 2) n = outLen - 2;
    memcpy(out, dir, n);
    out[n] = '\\';
    lstrcpynA(out + n + 1, name, (int)(outLen - n - 1));
}

static void SetError(Plugin *p, int status, const char *what, const char *msg)
{
    char flat[240];
    size_t j = 0;
    const char *s;
    p->status = status;
    for (s = msg; s && *s && j < sizeof(flat) - 1; ++s)
        flat[j++] = (*s == '\n' || *s == '\r') ? ' ' : *s;
    flat[j] = 0;
    wsprintfA(p->err, "%s: %s", what, flat);
    Logf("plugin| %s id=%s %s", status == kError ? "ERROR" : "MISMATCH", p->id[0] ? p->id : p->dir, p->err);
}

/* ---------- Lua sandbox ---------- */

static int sl_log(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    const char *lvl = lua_gettop(L) >= 2 ? luaL_optstring(L, 2, "info") : "info";
    Logf("plugin| [%s] %s", lvl, msg);
    return 0;
}

static void OpenSandboxLibs(lua_State *L)
{
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
    /* No filesystem, OS, package or debug access in v0. */
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "load");
    lua_pushnil(L); lua_setglobal(L, "require");
}

/* Push a fresh _ENV table: __index -> real globals (read-only view), _G = itself,
 * sl = framework API. Returns the absolute stack index of the env table. */
static int PushSandboxEnv(lua_State *L)
{
    int env, sl;
    lua_newtable(L);
    env = lua_gettop(L);
    lua_newtable(L);                                   /* metatable */
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_setfield(L, -2, "__index");                    /* read-only view of globals */
    lua_setmetatable(L, env);
    lua_pushvalue(L, env);
    lua_setfield(L, env, "_G");
    lua_newtable(L);
    sl = lua_gettop(L);
    lua_pushstring(L, "0.1");
    lua_setfield(L, sl, "api");
    lua_pushstring(L, SLMODF_VERSION);
    lua_setfield(L, sl, "framework_version");
    lua_pushinteger(L, SLMODF_PLUGIN_API_VERSION);
    lua_setfield(L, sl, "api_version");
    lua_pushcfunction(L, sl_log);
    lua_setfield(L, sl, "log");
    lua_pushcfunction(L, sl_log);
    lua_setfield(L, sl, "warn");
    lua_setfield(L, env, "sl");                        /* pops sl; stack: [env] */
    return env;
}

static int l_audio_set_soften(lua_State *L)
{
    int enabled = lua_toboolean(L, 1) ? 1 : 0;
    int gain = (int)luaL_optinteger(L, 2, 25);
    audio_set_soften(enabled, gain);
    lua_pushboolean(L, audio_is_probe_installed());
    return 1;
}
static int l_audio_reset_soften(lua_State *L)
{
    (void)L;
    audio_reset_soften();
    return 0;
}
static int l_audio_set_names(lua_State *L)
{
    char buf[16][80];
    const char *names[16];
    int n = 0;
    lua_Integer i, len;
    if (lua_istable(L, 1)) {
        len = (lua_Integer)luaL_len(L, 1);
        for (i = 1; i <= len && n < 16; ++i) {
            size_t k;
            lua_rawgeti(L, 1, (int)i);
            if (lua_type(L, -1) == LUA_TSTRING) {
                lstrcpynA(buf[n], lua_tostring(L, -1), (int)sizeof(buf[n]));
                for (k = 0; buf[n][k]; ++k) { /* trim trailing spaces */ }
                names[n] = buf[n];
                n++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pushinteger(L, audio_set_whitelist(names, n));
    return 1;
}
static int l_audio_clear_names(lua_State *L)
{
    lua_pushinteger(L, audio_set_whitelist(NULL, 0));
    return 1;
}

/* ctx.audio: read-only snapshot + runtime soften controls (Phase 6b). */
static void PushAudioContext(lua_State *L)
{
    unsigned long creates = 0, plays = 0, matched = 0, screams = 0;
    int soften = 0, gain = 25;
    SlmodfAudioMatch m;
    audio_get_stats(&creates, &plays, &matched, &screams);
    audio_get_soften(&soften, &gain);
    ZeroMemory(&m, sizeof(m));
    audio_get_last_match(&m);
    lua_newtable(L);                                   /* ctx.audio */
    lua_pushboolean(L, audio_is_probe_installed());
    lua_setfield(L, -2, "supported");
    lua_pushboolean(L, soften);
    lua_setfield(L, -2, "soften_enabled");
    lua_pushinteger(L, gain);
    lua_setfield(L, -2, "gain_percent");
    lua_newtable(L);                                   /* ctx.audio.last */
    lua_pushboolean(L, m.valid);
    lua_setfield(L, -2, "has");
    if (m.valid) {
        lua_pushstring(L, m.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, (lua_Integer)m.handle);
        lua_setfield(L, -2, "handle");
        lua_pushinteger(L, m.chunk);
        lua_setfield(L, -2, "chunk");
        lua_pushboolean(L, m.scream);
        lua_setfield(L, -2, "scream");
        lua_pushboolean(L, m.helper);
        lua_setfield(L, -2, "helper");
        lua_pushinteger(L, m.how);
        lua_setfield(L, -2, "how");
        lua_pushinteger(L, (lua_Integer)m.plays);
        lua_setfield(L, -2, "plays");
        lua_pushinteger(L, (lua_Integer)m.seq);
        lua_setfield(L, -2, "seq");
    }
    lua_setfield(L, -2, "last");
    lua_newtable(L);                                   /* ctx.audio.stats */
    lua_pushinteger(L, (lua_Integer)creates); lua_setfield(L, -2, "creates");
    lua_pushinteger(L, (lua_Integer)plays);   lua_setfield(L, -2, "plays");
    lua_pushinteger(L, (lua_Integer)matched); lua_setfield(L, -2, "matched");
    lua_pushinteger(L, (lua_Integer)screams); lua_setfield(L, -2, "screams");
    lua_setfield(L, -2, "stats");
    lua_pushcfunction(L, l_audio_set_soften);   lua_setfield(L, -2, "set_soften");
    lua_pushcfunction(L, l_audio_reset_soften); lua_setfield(L, -2, "reset_soften");
    lua_pushcfunction(L, l_audio_set_names);    lua_setfield(L, -2, "set_names");
    lua_pushcfunction(L, l_audio_clear_names);  lua_setfield(L, -2, "clear_names");
    lua_setfield(L, -2, "audio");
}

/* Phase 9: on-demand object reads for scene rules (Bonnet body position,
 * Freddy phase alterables). These read live memory through the state module;
 * every call validates the object and returns nil instead of raw addresses. */
static int l_game_object_rect(lua_State *L)
{
    int x = 0, y = 0, w = 0, h = 0;
    const char *name = luaL_checkstring(L, 1);
    if (!name || !state::ReadObjectRect(name, &x, &y, &w, &h)) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushinteger(L, x); lua_setfield(L, -2, "x");
    lua_pushinteger(L, y); lua_setfield(L, -2, "y");
    lua_pushinteger(L, w); lua_setfield(L, -2, "w");
    lua_pushinteger(L, h); lua_setfield(L, -2, "h");
    return 1;
}

static int l_game_alterable(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int index = (int)luaL_checkinteger(L, 2);
    int value = 0;
    if (!name || index < 0 || index > 25 ||
        !state::ReadAlterableLive(name, index, &value)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, value);
    return 1;
}

static void PushGameContext(lua_State *L)
{
    /* GameAdapter v0 (Phase 5): read-only scene + counter snapshot. Plugins
     * never see raw addresses; unknown state is reported as supported=false. */
    const SceneInfo *sc = scene::Current();
    int supported = (sc && sc->valid && g_fw.gate == 1) ? 1 : 0;
    lua_newtable(L);                                   /* ctx */
    lua_newtable(L);                                   /* ctx.game */
    lua_pushboolean(L, supported);
    lua_setfield(L, -2, "supported");
    lua_pushinteger(L, (lua_Integer)g_frameId);
    lua_setfield(L, -2, "frame_id");
    lua_pushinteger(L, supported ? (lua_Integer)sc->index : -1);
    lua_setfield(L, -2, "scene_id");
    lua_pushstring(L, supported ? sc->name : "");
    lua_setfield(L, -2, "scene_name");
    lua_pushinteger(L, supported ? (lua_Integer)sc->w : 0);
    lua_setfield(L, -2, "scene_width");
    lua_pushinteger(L, supported ? (lua_Integer)sc->h : 0);
    lua_setfield(L, -2, "scene_height");
    lua_pushboolean(L, state::Status() == 1 ? 1 : 0);
    lua_setfield(L, -2, "counters_ready");
    lua_pushnumber(L, (lua_Number)d3d9hook::g_info.fps);
    lua_setfield(L, -2, "fps");
    lua_pushinteger(L, (lua_Integer)d3d9hook::g_info.presents);
    lua_setfield(L, -2, "presents");
    {
        int i, n = state::CounterCount();
        if (n > 64) n = 64;
        lua_newtable(L);                               /* ctx.game.counters */
        for (i = 0; i < n; ++i) {
            const char *nm;
            if (!state::CounterValid(i)) continue;
            nm = state::CounterName(i);
            if (!nm || !nm[0]) continue;
            lua_pushinteger(L, state::CounterValue(i));
            lua_setfield(L, -2, nm);
        }
        lua_setfield(L, -2, "counters");
    }
    {
        /* Phase 7: read-only Active alterable values for the state watch list.
         * Zero-based keys keep the game's alterable index: Ennard[3] is the
         * door/attack state. Unwatched objects do not appear here. */
        int i, j, n = state::ObjectInfoCount();
        int supported = (n > 0 && state::Status() == 1) ? 1 : 0;
        lua_pushboolean(L, supported);
        lua_setfield(L, -2, "objects_ready");
        lua_newtable(L);                               /* ctx.game.alterables */
        for (i = 0; i < n; ++i) {
            const char *nm = state::ObjectInfoName(i);
            int have = 0, val = 0;
            if (!nm || !nm[0] || state::ObjectInfoType(i) != 2) continue;
            lua_newtable(L);
            for (j = 0; j < 26; ++j) {
                if (!state::ObjectInfoAlterable(i, j, &val)) continue;
                lua_pushinteger(L, (lua_Integer)j);
                lua_pushinteger(L, (lua_Integer)val);
                lua_rawset(L, -3);
                have = 1;
            }
            if (have) lua_setfield(L, -2, nm);
            else lua_pop(L, 1);
        }
        lua_setfield(L, -2, "alterables");
    }
    /* Phase 9: generic read-only helpers; nil when the object is absent. */
    lua_pushcfunction(L, l_game_object_rect); lua_setfield(L, -2, "object_rect");
    lua_pushcfunction(L, l_game_alterable);   lua_setfield(L, -2, "alterable");
    lua_setfield(L, -2, "game");
    PushAudioContext(L);
    lua_pushstring(L, SLMODF_VERSION);
    lua_setfield(L, -2, "framework_version");
}

static int ui_text(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    ImGui::TextUnformatted(s);
    return 0;
}
static int ui_text_disabled(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    ImGui::TextDisabled("%s", s);
    return 0;
}
static int ui_separator(lua_State *L) { (void)L; ImGui::Separator(); return 0; }
static int ui_same_line(lua_State *L) { (void)L; ImGui::SameLine(); return 0; }
static int ui_checkbox(lua_State *L)
{
    const char *label = luaL_checkstring(L, 1);
    int v = lua_toboolean(L, 2);
    bool b = v != 0;
    if (ImGui::Checkbox(label, &b)) { lua_pushboolean(L, b ? 1 : 0); return 1; }
    lua_pushboolean(L, v ? 1 : 0);
    return 1;
}
static int ui_slider_int(lua_State *L)
{
    const char *label = luaL_checkstring(L, 1);
    int v = (int)luaL_checkinteger(L, 2);
    int lo = (int)luaL_checkinteger(L, 3);
    int hi = (int)luaL_checkinteger(L, 4);
    ImGui::SliderInt(label, &v, lo, hi);
    lua_pushinteger(L, v);
    return 1;
}
static int ui_button(lua_State *L)
{
    const char *label = luaL_checkstring(L, 1);
    lua_pushboolean(L, ImGui::Button(label) ? 1 : 0);
    return 1;
}

/* ---------- Phase 7: free-screen overlay primitives ----------
 * Coordinates are physical client pixels. The plugin calls ui.viewport() first
 * and scales anchors by that size, so 1080p/1440p/4K and DPI changes all work.
 * Colors are ImU32 (ABGR, same value as ImGui's IM_COL32 helper). */
static ImU32 LuaColor(lua_State *L, int idx, ImU32 def)
{
    if (lua_isnoneornil(L, idx)) return def;
    return (ImU32)(unsigned long long)luaL_checkinteger(L, idx);
}
static int ui_viewport(lua_State *L)
{
    ImGuiIO &io = ImGui::GetIO();
    lua_pushnumber(L, (lua_Number)io.DisplaySize.x);
    lua_pushnumber(L, (lua_Number)io.DisplaySize.y);
    return 2;
}
static int ui_text_size(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    float sz = (float)luaL_optnumber(L, 2, 0);
    if (sz <= 0.0f) sz = ImGui::GetFontSize();
    ImVec2 v = ImGui::GetFont()->CalcTextSizeA(sz, FLT_MAX, 0.0f, s);
    lua_pushnumber(L, (lua_Number)v.x);
    lua_pushnumber(L, (lua_Number)v.y);
    return 2;
}
static int ui_text_at(lua_State *L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    const char *s = luaL_checkstring(L, 3);
    ImU32 col = LuaColor(L, 4, IM_COL32(255, 255, 255, 255));
    float sz = (float)luaL_optnumber(L, 5, 0);
    if (sz <= 0.0f) sz = ImGui::GetFontSize();
    ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), sz, ImVec2(x, y), col, s);
    return 0;
}
static int ui_rect_filled(lua_State *L)
{
    float x = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3), h = (float)luaL_checknumber(L, 4);
    ImU32 col = LuaColor(L, 5, IM_COL32(255, 255, 255, 255));
    float rounding = (float)luaL_optnumber(L, 6, 0.0);
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), col, rounding);
    return 0;
}
static int ui_rect(lua_State *L)
{
    float x = (float)luaL_checknumber(L, 1), y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3), h = (float)luaL_checknumber(L, 4);
    ImU32 col = LuaColor(L, 5, IM_COL32(255, 255, 255, 255));
    float thickness = (float)luaL_optnumber(L, 6, 1.0);
    float rounding = (float)luaL_optnumber(L, 7, 0.0);
    ImGui::GetForegroundDrawList()->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), col, rounding, 0, thickness);
    return 0;
}
static int ui_line(lua_State *L)
{
    float x1 = (float)luaL_checknumber(L, 1), y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3), y2 = (float)luaL_checknumber(L, 4);
    ImU32 col = LuaColor(L, 5, IM_COL32(255, 255, 255, 255));
    float thickness = (float)luaL_optnumber(L, 6, 1.0);
    ImGui::GetForegroundDrawList()->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), col, thickness);
    return 0;
}

static void PushUiTable(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, ui_text);            lua_setfield(L, -2, "text");
    lua_pushcfunction(L, ui_text_disabled);   lua_setfield(L, -2, "text_disabled");
    lua_pushcfunction(L, ui_separator);       lua_setfield(L, -2, "separator");
    lua_pushcfunction(L, ui_same_line);       lua_setfield(L, -2, "same_line");
    lua_pushcfunction(L, ui_checkbox);        lua_setfield(L, -2, "checkbox");
    lua_pushcfunction(L, ui_slider_int);      lua_setfield(L, -2, "slider_int");
    lua_pushcfunction(L, ui_button);          lua_setfield(L, -2, "button");
}

static void PushOverlayUi(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, ui_viewport);        lua_setfield(L, -2, "viewport");
    lua_pushcfunction(L, ui_text_size);       lua_setfield(L, -2, "text_size");
    lua_pushcfunction(L, ui_text_at);         lua_setfield(L, -2, "text_at");
    lua_pushcfunction(L, ui_rect_filled);     lua_setfield(L, -2, "rect_filled");
    lua_pushcfunction(L, ui_rect);            lua_setfield(L, -2, "rect");
    lua_pushcfunction(L, ui_line);            lua_setfield(L, -2, "line");
}

/* ---------- callbacks ---------- */

enum ArgKind { ARG_NONE = 0, ARG_CTX = 1, ARG_UI = 2, ARG_OVERLAY = 3 };

static void HandleLuaError(Plugin *p, const char *fn)
{
    lua_State *L = g_L;
    const char *s = lua_tostring(L, -1);
    char flat[240];
    size_t j = 0;
    p->errors++;
    p->consecErrors++;
    if (!s) s = "(non-string Lua error)";
    for (; *s && j < sizeof(flat) - 1; ++s) flat[j++] = (*s == '\n' || *s == '\r') ? ' ' : *s;
    flat[j] = 0;
    lstrcpynA(p->err, flat, sizeof(p->err));
    if (p->errors <= 3 || (p->errors % 600) == 0)
        Logf("plugin| ERROR id=%s %s: %s (errors=%d consec=%d)", p->id, fn, flat, p->errors, p->consecErrors);
    lua_pop(L, 1);
    if (!p->autoDisabled && p->consecErrors >= SLMODF_PLUGIN_AUTO_DISABLE_ERRORS) {
        p->autoDisabled = 1;
        p->status = kError;
        p->enabled = 0;
        Logf("plugin| AUTO-DISABLED id=%s after %d consecutive errors (last=%s); enabled=0 written to manifest",
             p->id[0] ? p->id : p->dir, p->consecErrors, flat);
        WriteEnabled(p, 0);
    }
}

/* Returns 1 when the callback existed and succeeded, 0 when missing, -1 on error. */
static int CallPlugin(Plugin *p, const char *fn, ArgKind kind)
{
    lua_State *L = g_L;
    int rc;
    if (!L || !p->envRef) return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, p->envRef);
    lua_getfield(L, -1, fn);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    if (kind == ARG_CTX) PushGameContext(L);
    else if (kind == ARG_UI) PushUiTable(L);
    else if (kind == ARG_OVERLAY) PushOverlayUi(L);
    rc = lua_pcall(L, kind == ARG_NONE ? 0 : 1, 0, 0);
    if (rc != LUA_OK) { HandleLuaError(p, fn); return -1; }
    p->consecErrors = 0;
    return 1;
}

/* ---------- manifest ---------- */

/* Returns 1 on success, 0 when the manifest is missing/invalid. */
static int ParseManifest(Plugin *p)
{
    FILE *f;
    char line[512];
    int hasId = 0, hasApi = 0;
    p->enabled = 1;
    p->apiVersion = 0;
    lstrcpynA(p->version, "0.0.0", sizeof(p->version));
    lstrcpynA(p->entry, "main.lua", sizeof(p->entry));
    f = fopen(p->manifest, "rb");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        char *eq, *k, *v;
        if (line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        k = line; v = eq + 1;
        Trim(k); Trim(v);
        if (!_stricmp(k, "id")) { lstrcpynA(p->id, v, sizeof(p->id)); hasId = 1; }
        else if (!_stricmp(k, "name")) lstrcpynA(p->name, v, sizeof(p->name));
        else if (!_stricmp(k, "version")) lstrcpynA(p->version, v, sizeof(p->version));
        else if (!_stricmp(k, "api_version")) { p->apiVersion = atoi(v); hasApi = 1; }
        else if (!_stricmp(k, "enabled")) p->enabled = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "entry")) lstrcpynA(p->entry, v, sizeof(p->entry));
    }
    fclose(f);
    if (!p->name[0]) lstrcpynA(p->name, p->id[0] ? p->id : p->dir, sizeof(p->name));
    if (!hasId) { SetError(p, kInvalid, "manifest", "missing id"); return 0; }
    if (!hasApi) { SetError(p, kInvalid, "manifest", "missing api_version"); return 0; }
    return 1;
}

/* Copy enabled=0/1 into the manifest with an atomic replace. */
static void WriteEnabled(Plugin *p, int enabled)
{
    FILE *in = fopen(p->manifest, "rb");
    char tmp[MAX_PATH];
    FILE *out;
    char line[512];
    int written = 0;
    if (!in) { SetError(p, kError, "manifest", "cannot reopen for write"); return; }
    JoinPath(tmp, sizeof(tmp), p->dir, "plugin.ini.tmp");
    out = fopen(tmp, "wb");
    if (!out) { fclose(in); SetError(p, kError, "manifest", "cannot create temp"); return; }
    while (fgets(line, sizeof(line), in)) {
        char *eq = strchr(line, '=');
        int isEnabled = 0;
        if (eq && line[0] != '#' && line[0] != ';' && line[0] != '[') {
            char key[64];
            size_t n = (size_t)(eq - line);
            if (n >= sizeof(key)) n = sizeof(key) - 1;
            memcpy(key, line, n);
            key[n] = 0;
            Trim(key);
            if (!_stricmp(key, "enabled")) isEnabled = 1;
        }
        if (isEnabled) { fprintf(out, "enabled=%d\n", enabled); written = 1; }
        else fputs(line, out);
    }
    fclose(in);
    if (!written) fprintf(out, "enabled=%d\n", enabled);
    fclose(out);
    if (!MoveFileExA(tmp, p->manifest, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(tmp);
        SetError(p, kError, "manifest", "atomic replace failed");
        return;
    }
    Logf("plugin| set enabled=%d id=%s manifest=%s", enabled, p->id, p->manifest);
}

/* ---------- load ---------- */

static void ParsePluginInfoTable(Plugin *p, int idx)
{
    const char *s;
    lua_getfield(g_L, idx, "id");
    if ((s = lua_tostring(g_L, -1)) && s[0]) lstrcpynA(p->id, s, sizeof(p->id));
    lua_pop(g_L, 1);
    lua_getfield(g_L, idx, "name");
    if ((s = lua_tostring(g_L, -1)) && s[0]) lstrcpynA(p->name, s, sizeof(p->name));
    lua_pop(g_L, 1);
    lua_getfield(g_L, idx, "version");
    if ((s = lua_tostring(g_L, -1)) && s[0]) lstrcpynA(p->version, s, sizeof(p->version));
    lua_pop(g_L, 1);
    lua_getfield(g_L, idx, "api_version");
    if (lua_isinteger(g_L, -1)) p->apiVersion = (int)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
}

static void LoadOne(int idx)
{
    Plugin *p = &g_plugins[idx];
    int envIdx;
    if (!ParseManifest(p)) return;
    Logf("plugin| manifest dir=%s id=%s name=%s version=%s api=%d enabled=%d entry=%s",
         p->dir, p->id, p->name, p->version, p->apiVersion, p->enabled, p->entry);
    if (!p->enabled) {
        p->status = kDisabled;
        Logf("plugin| disabled by manifest id=%s", p->id);
        return;
    }
    if (p->apiVersion != SLMODF_PLUGIN_API_VERSION) {
        char msg[128];
        wsprintfA(msg, "plugin api_version=%d, framework supports %d", p->apiVersion, SLMODF_PLUGIN_API_VERSION);
        SetError(p, kApiMismatch, "api", msg);
        return;
    }
    {   /* JoinPath cannot alias its output with the plugin's entry buffer. */
        char full[MAX_PATH];
        JoinPath(full, sizeof(full), p->dir, p->entry);
        lstrcpynA(p->entry, full, sizeof(p->entry));
    }
    if (!FileExists(p->entry)) {
        SetError(p, kError, "entry", "entry script missing");
        return;
    }
    envIdx = PushSandboxEnv(g_L);                      /* [env] */
    if (luaL_loadfilex(g_L, p->entry, NULL) != LUA_OK) { /* [env][err] */
        HandleLuaError(p, "loadfile");
        p->status = kError;
        lua_settop(g_L, envIdx - 1);
        return;
    }
    /* [env][func]: push a copy of the sandbox table and bind it as the chunk's
     * first upvalue (_ENV); lua_setupvalue pops that copy, so push it last. */
    lua_pushvalue(g_L, envIdx);                        /* [env][func][envcopy] */
    if (!lua_setupvalue(g_L, -2, 1)) {                 /* [env][func] */
        SetError(p, kError, "env", "chunk has no _ENV upvalue");
        lua_settop(g_L, envIdx - 1);
        return;
    }
    if (lua_pcall(g_L, 0, 0, 0) != LUA_OK) {           /* [env][err] */
        HandleLuaError(p, "chunk");
        p->status = kError;
        lua_settop(g_L, envIdx - 1);
        return;
    }
    lua_getfield(g_L, envIdx, "plugin_info");          /* [env][plugin_info] */
    if (!lua_isfunction(g_L, -1)) {
        lua_pop(g_L, 1);
        SetError(p, kError, "plugin_info", "function plugin_info() is required");
        lua_settop(g_L, envIdx - 1);
        return;
    }
    if (lua_pcall(g_L, 0, 1, 0) != LUA_OK || !lua_istable(g_L, -1)) { /* [env][ret|err] */
        HandleLuaError(p, "plugin_info");
        p->status = kError;
        lua_settop(g_L, envIdx - 1);
        return;
    }
    ParsePluginInfoTable(p, lua_gettop(g_L));
    lua_pop(g_L, 1);                                   /* [env] */
    if (p->apiVersion != SLMODF_PLUGIN_API_VERSION) {
        char msg[128];
        wsprintfA(msg, "plugin_info api_version=%d, framework supports %d", p->apiVersion, SLMODF_PLUGIN_API_VERSION);
        SetError(p, kApiMismatch, "api", msg);
        lua_settop(g_L, envIdx - 1);
        return;
    }
    {
        const char *names[5] = { "on_load", "on_update", "on_draw_ui", "on_draw_overlay", "on_shutdown" };
        int *flags[5] = { &p->hasLoad, &p->hasUpdate, &p->hasDraw, &p->hasOverlay, &p->hasShutdown };
        int i;
        for (i = 0; i < 5; i++) {
            lua_getfield(g_L, envIdx, names[i]);
            *flags[i] = lua_isfunction(g_L, -1) ? 1 : 0;
            lua_pop(g_L, 1);
        }
    }
    lua_pushvalue(g_L, envIdx);
    p->envRef = luaL_ref(g_L, LUA_REGISTRYINDEX);
    lua_settop(g_L, envIdx - 1);
    p->status = kLoaded;
    Logf("plugin| loaded id=%s name=%s version=%s api=%d entry=%s callbacks load=%d update=%d draw=%d overlay=%d shutdown=%d",
         p->id, p->name, p->version, p->apiVersion, p->entry,
         p->hasLoad, p->hasUpdate, p->hasDraw, p->hasOverlay, p->hasShutdown);
    if (p->hasLoad && CallPlugin(p, "on_load", ARG_CTX) < 0) {
        p->status = kError;
        Logf("plugin| disabled after on_load error id=%s", p->id);
    }
}

static void CountStatus(int *loaded, int *disabled, int *mismatch, int *error, int *invalid)
{
    int i;
    *loaded = *disabled = *mismatch = *error = *invalid = 0;
    for (i = 0; i < g_count; i++) {
        switch (g_plugins[i].status) {
        case kLoaded: (*loaded)++; break;
        case kDisabled: (*disabled)++; break;
        case kApiMismatch: (*mismatch)++; break;
        case kError: (*error)++; break;
        case kInvalid: (*invalid)++; break;
        default: break;
        }
    }
}

static void LoadAll(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH];
    int loaded, disabled, mismatch, error, invalid;
    g_count = 0;
    if (!g_root[0]) {
        if (g_fw.pluginsDir[0]) lstrcpynA(g_root, g_fw.pluginsDir, sizeof(g_root));
        else if (g_fw.frameworkDir[0]) wsprintfA(g_root, "%s\\plugins", g_fw.frameworkDir);
        else wsprintfA(g_root, "%s\\..\\..\\plugins", g_fw.logDir);
    }
    g_loaded = 1;
    Logf("plugin| scan root=%s", g_root);
    JoinPath(pattern, sizeof(pattern), g_root, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        Logf("plugin| root missing or empty (%s) err=%lu", g_root, (unsigned long)GetLastError());
        lstrcpynA(g_summary, "root missing", sizeof(g_summary));
        return;
    }
    do {
        Plugin *p;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        if (g_count >= MAX_PLUGINS) { Logf("plugin| WARN plugin cap %d reached", MAX_PLUGINS); break; }
        p = &g_plugins[g_count];
        memset(p, 0, sizeof(*p));
        JoinPath(p->dir, sizeof(p->dir), g_root, fd.cFileName);
        JoinPath(p->manifest, sizeof(p->manifest), p->dir, "plugin.ini");
        if (!FileExists(p->manifest)) {
            Logf("plugin| skip %s (no plugin.ini)", fd.cFileName);
            continue;
        }
        g_count++;
        LoadOne(g_count - 1);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    CountStatus(&loaded, &disabled, &mismatch, &error, &invalid);
    wsprintfA(g_summary, "total=%d loaded=%d disabled=%d api_mismatch=%d error=%d invalid=%d",
              g_count, loaded, disabled, mismatch, error, invalid);
    Logf("plugin| summary %s", g_summary);
}

static void ShutdownAll(void)
{
    int i;
    if (!g_L) return;
    for (i = 0; i < g_count; i++) {
        Plugin *p = &g_plugins[i];
        if (p->status == kLoaded && p->hasShutdown) CallPlugin(p, "on_shutdown", ARG_NONE);
        if (p->envRef) { luaL_unref(g_L, LUA_REGISTRYINDEX, p->envRef); p->envRef = 0; }
    }
    lua_close(g_L);
    g_L = NULL;
    g_count = 0;
    g_loaded = 0;
}

/* ---------- public API ---------- */

void SetRootForTest(const char *dir)
{
    lstrcpynA(g_root, dir ? dir : "", sizeof(g_root));
    if (g_L) ShutdownAll();
    g_loaded = 0;
}

void EnsureLoaded(void)
{
    if (g_loaded) return;
    g_L = luaL_newstate();
    if (!g_L) {
        Logf("plugin| luaL_newstate failed");
        lstrcpynA(g_summary, "lua alloc failed", sizeof(g_summary));
        return;
    }
    OpenSandboxLibs(g_L);
    Logf("plugin| Lua %s.%s state created (safe libs only: base/table/string/math/coroutine/utf8)",
         LUA_VERSION_MAJOR, LUA_VERSION_MINOR);
    LoadAll();
}

void ReloadAll(void)
{
    Logf("plugin| reload requested (had %d entries)", g_count);
    if (g_L) ShutdownAll();
    EnsureLoaded();
}

void OnUpdate(void)
{
    int i;
    g_frameId++;
    for (i = 0; i < g_count; i++) {
        Plugin *p = &g_plugins[i];
        if (p->status != kLoaded || !p->hasUpdate) continue;
        p->updates++;
        CallPlugin(p, "on_update", ARG_CTX);
    }
}

void DrawUi(void)
{
    int i, drew = 0;
    for (i = 0; i < g_count; i++) {
        Plugin *p = &g_plugins[i];
        if (p->status != kLoaded || !p->hasDraw) continue;
        if (drew) ImGui::Separator();
        ImGui::PushID(i);
        CallPlugin(p, "on_draw_ui", ARG_UI);
        ImGui::PopID();
        drew = 1;
    }
}

/* Called every frame after the debug window/HUD, still inside ImGui::NewFrame /
 * Render. It only draws into the foreground draw list: no input capture, so the
 * game keeps receiving keys/mouse while warnings are on screen. */
void DrawOverlay(void)
{
    int i;
    for (i = 0; i < g_count; i++) {
        Plugin *p = &g_plugins[i];
        if (p->status != kLoaded || !p->hasOverlay) continue;
        ImGui::PushID(i);
        CallPlugin(p, "on_draw_overlay", ARG_OVERLAY);
        ImGui::PopID();
    }
}

void Shutdown(void)
{
    ShutdownAll();
}

int Count(void) { return g_count; }

const View *Get(int i)
{
    static View v;
    Plugin *p;
    if (i < 0 || i >= g_count) return NULL;
    p = &g_plugins[i];
    v.dir = p->dir;
    v.id = p->id;
    v.name = p->name;
    v.version = p->version;
    v.apiVersion = p->apiVersion;
    v.enabled = p->enabled;
    v.status = p->status;
    v.updates = p->updates;
    v.errors = p->errors;
    v.autoDisabled = p->autoDisabled;
    v.error = p->err;
    return &v;
}

const char *Summary(void) { return g_summary; }

int SetEnabled(int i, int enabled)
{
    if (i < 0 || i >= g_count) return 0;
    g_plugins[i].enabled = enabled ? 1 : 0;
    WriteEnabled(&g_plugins[i], g_plugins[i].enabled);
    RequestReloadPlugins();
    return 1;
}

} // namespace plugins

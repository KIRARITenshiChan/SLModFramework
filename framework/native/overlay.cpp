#include "common.h"
#include "overlay.h"
#include "d3d9hook.h"
#include "plugin_host.h"
#include "scene.h"
#include "state.h"
#include "recorder.h"
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include <d3d9.h>
#include <stdio.h>

static IDirect3DDevice9 *g_dev;
static HWND g_hwnd;
static bool g_ctxCreated, g_win32Init, g_dx9Init, g_objectsOk;
static bool g_styleScaled;
static float g_dpiScale = 1.0f;

static float GetWindowDpiScale(HWND hwnd)
{
    typedef UINT (WINAPI *GetDpiForWindow_t)(HWND);
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (u32 && hwnd) {
        GetDpiForWindow_t fn = (GetDpiForWindow_t)GetProcAddress(u32, "GetDpiForWindow");
        if (fn) {
            UINT dpi = fn(hwnd);
            if (dpi >= 72 && dpi <= 480) return (float)dpi / 96.0f;
        }
    }
    return 1.0f;
}

static const char *PluginStatusName(int st)
{
    switch (st) {
    case plugins::kLoaded:     return "已加载";
    case plugins::kDisabled:   return "已禁用";
    case plugins::kApiMismatch:return "API 不匹配";
    case plugins::kError:      return "错误";
    case plugins::kInvalid:    return "清单无效";
    default:                   return "未知";
    }
}

static ImVec4 PluginStatusColor(int st)
{
    switch (st) {
    case plugins::kLoaded:     return ImVec4(0.45f, 0.90f, 0.50f, 1.0f);
    case plugins::kDisabled:   return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    case plugins::kApiMismatch:return ImVec4(1.00f, 0.75f, 0.30f, 1.0f);
    case plugins::kError:      return ImVec4(1.00f, 0.45f, 0.45f, 1.0f);
    case plugins::kInvalid:    return ImVec4(1.00f, 0.35f, 0.55f, 1.0f);
    default:                   return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

static void DrawMainWindow(void)
{
    ImGuiStyle &st = ImGui::GetStyle();
    /* Scale the panel with the window DPI: ImGui 1.91 does not scale explicit
     * sizes, so on the 150% desktop a 560x470 panel would cover only ~22% of
     * the screen and long paths would clip. */
    const float s = (g_dpiScale > 1.01f) ? g_dpiScale : 1.0f;
    /* P8.5: on a 150% desktop the content is taller than the panel. Clamp the
     * window to the viewport every frame so the bottom sections stay reachable
     * with the mouse wheel, and seed the size to a viewport-fitting value. */
    const ImGuiIO &io = ImGui::GetIO();
    const float maxH = (io.DisplaySize.y > 200.0f * s) ? io.DisplaySize.y - 60.0f * s : 720.0f * s;
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f * s, 260.0f * s), ImVec2(760.0f * s, maxH));
    ImGui::SetNextWindowSize(ImVec2(640.0f * s, maxH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40.0f * s, 40.0f * s), ImGuiCond_FirstUseEver);
    ImGui::Begin("SLModFramework - 状态面板", NULL, ImGuiWindowFlags_NoCollapse);
    ImGui::Text("%s %s", SLMODF_NAME, SLMODF_VERSION);
    ImGui::SameLine();
    ImGui::TextDisabled("(Phase 8 QoL / plugins / threat overlay)");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("游戏与门禁", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("EXE: %s", g_fw.exePath);
        ImGui::TextWrapped("SHA-256: %s", g_fw.sha256[0] ? g_fw.sha256 : "<无 sidecar / 未校验>");
        ImGui::Text("大小: %.0f MB (%llu bytes)", (double)g_fw.exeSize / 1048576.0, g_fw.exeSize);
        ImGui::Text("版本门禁: %s", GateStatusName());
        ImGui::Text("Fusion build: pb=%u runtime=0x%X product=%u", g_fw.pb, g_fw.rt, g_fw.pv);
        ImGui::Text("mmfs2.dll base: 0x%08lX", (unsigned long)(ULONG_PTR)g_fw.hMmfs2);
        ImGui::Text("PAMU (内存副本): 0x%08lX", g_fw.pamuAddr);
        {
            const SceneInfo *sc = scene::Current();
            if (sc && sc->valid)
                ImGui::Text("场景: [%d] %s  %ux%u layers=%u inst=%u  base=0x%08lX",
                            sc->index, sc->name, sc->w, sc->h, sc->layers, sc->instances, sc->base);
            else
                ImGui::TextDisabled("场景: 未知（等待帧签名）");
        }
    }
    if (ImGui::CollapsingHeader("场景状态 (DebugInspector)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("状态: %s  frame_index=%d", state::StatusName(), state::SceneIndex());
        ImGui::Text("对象数: %d  计数器: %d  rh(只读): 0x%08lX",
                    state::ObjectCount(), state::CounterCount(), scene::RunHeaderPtr());
        {
            int n = state::CounterCount();
            if (n == 0) {
                ImGui::TextDisabled("（本帧没有已映射计数器，或状态读取不可用）");
            } else {
                for (int i = 0; i < n; ++i) {
                    const char *nm = state::CounterName(i);
                    if (!nm) continue;
                    if (state::CounterValid(i)) {
                        ImGui::Text("%s = %d", nm, state::CounterValue(i));
                    } else {
                        ImGui::TextDisabled("%s = ?", nm);
                    }
                    if ((i % 3) != 2 && i + 1 < n) ImGui::SameLine(0.0f, 26.0f);
                }
            }
        }
        ImGui::Text("对象表: %d（Active alterable 仅监视 Ennard / flip it）", state::ObjectInfoCount());
        {
            static const char *kAltNames[] = {"Ennard", "flip it"};
            int a, b;
            for (a = 0; a < 2; ++a) {
                int idx = state::FindObjectInfo(kAltNames[a]);
                if (idx < 0) continue;
                ImGui::Text("%s[0..3]:", kAltNames[a]);
                for (b = 0; b < 4; ++b) {
                    int v = 0;
                    ImGui::SameLine();
                    if (state::ObjectInfoAlterable(idx, b, &v)) ImGui::Text("%d", v);
                    else ImGui::TextDisabled("?");
                }
            }
        }
        ImGui::TextDisabled("只读: 计数器 = ~int16(obj+0x20A)；Active 值 = rVal CValue[]；不写入游戏内存，未知即未知");
        ImGui::Separator();
        {
            bool rec = recorder::Enabled() != 0;
            if (ImGui::Checkbox("状态录制 (JSONL)", &rec))
                recorder::SetEnabled(rec ? 1 : 0, "gui");
            if (recorder::Enabled()) {
                const char *p = recorder::Path();
                ImGui::TextDisabled("输出: %s", (p && p[0]) ? p : "(等待有效场景后创建文件)");
            } else {
                ImGui::TextDisabled("配置: framework.ini record=1（默认 500 ms 一条，换场景立即记）");
            }
        }
    }
    if (ImGui::CollapsingHeader("渲染与设备", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("D3D9 设备: %p", g_fw.device);
        ImGui::Text("窗口 hwnd: 0x%08lX", (unsigned long)(ULONG_PTR)g_fw.hwnd);
        ImGui::Text("后台缓冲: %ux%u windowed=%d refresh=%u Hz",
                    d3d9hook::g_info.backW, d3d9hook::g_info.backH,
                    d3d9hook::g_info.windowed, d3d9hook::g_info.refresh);
        ImGui::Text("Present 计数: %llu  FPS: %.1f", d3d9hook::g_info.presents, d3d9hook::g_info.fps);
        ImGui::Text("DPI 缩放: %.3f", (double)g_dpiScale);
        ImGui::Text("create9_hook=%d createdevice_hook=%d device_hook=%d",
                    d3d9hook::g_info.earlyInstalled, d3d9hook::g_info.createDeviceHooked, d3d9hook::g_info.deviceHooked);
    }
    if (ImGui::CollapsingHeader("输入", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("热键: VK 0x%02X (Insert=0x2D)", g_fw.hotkeyVK);
        ImGui::Text("GUI 可见: %ld  切换次数: %ld", (long)g_fw.guiVisible, (long)g_fw.hotkeyToggles);
        ImGui::Text("已吞按键: %ld  已吞鼠标: %ld", (long)g_fw.swallowedKeys, (long)g_fw.swallowedMouse);
        ImGui::TextDisabled("GUI 打开时键盘/鼠标消息被拦截，不传给游戏窗口过程");
    }
    if (ImGui::CollapsingHeader("框架/插件", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("插件 API v%d | %s", SLMODF_PLUGIN_API_VERSION, plugins::Summary());
        if (ImGui::Button("重新扫描插件")) RequestReloadPlugins();
        ImGui::SameLine();
        ImGui::TextDisabled("根目录: %s", g_fw.pluginsDir[0] ? g_fw.pluginsDir : "<未配置>");
        {
            int n = plugins::Count();
            if (n == 0) ImGui::TextDisabled("未发现插件（放入 plugin.ini 的目录后点重新扫描）");
            for (int i = 0; i < n; ++i) {
                const plugins::View *v = plugins::Get(i);
                if (!v) continue;
                ImGui::PushID(i);
                bool en = v->enabled != 0;
                if (ImGui::Checkbox("##enabled", &en)) plugins::SetEnabled(i, en ? 1 : 0);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("启用/禁用会写回 plugin.ini 并重载");
                ImGui::SameLine();
                ImGui::TextColored(PluginStatusColor(v->status), "%-12s", PluginStatusName(v->status));
                ImGui::SameLine();
                ImGui::TextUnformatted(v->name ? v->name : "(no name)");
                ImGui::SameLine();
                ImGui::TextDisabled("v%s api=%d upd=%d err=%d", v->version ? v->version : "?",
                                    v->apiVersion, v->updates, v->errors);
                if (v->autoDisabled) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.30f, 1.0f), "已自动禁用");
                }
                if (v->error && v->error[0] &&
                    (v->status == plugins::kApiMismatch || v->status == plugins::kError ||
                     v->status == plugins::kInvalid || v->errors > 0))
                    ImGui::TextWrapped("  -> %s", v->error);
                ImGui::PopID();
            }
        }
        if (plugins::Count() > 0) {
            ImGui::Separator();
            ImGui::TextDisabled("插件自绘界面 (on_draw_ui):");
            plugins::DrawUi();
        }
        ImGui::Separator();
        bool hud = g_fw.hudEnabled != 0;
        if (ImGui::Checkbox("HUD 常驻显示 (config: hud=1)", &hud)) {
            g_fw.hudEnabled = hud ? 1 : 0;
            Logf("ui| hud set to %d (not persisted)", g_fw.hudEnabled);
        }
        if (ImGui::Button("隐藏 GUI (Insert)")) ToggleGui("gui-button");
        ImGui::SameLine();
        ImGui::TextDisabled("窗口坐标/尺寸可拖动");
        ImGui::Separator();
        if (ImGui::Button("导出诊断包")) {
            const char *p = ExportDiagnostics();
            Logf("ui| diagnostics export -> %s", p ? p : "<failed>");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("收集日志/配置/插件清单到 logs\\diagnostics-<ts>.zip（排障时发给作者）");
        ImGui::SameLine();
        if (ImGui::Button("恢复默认配置")) {
            ResetConfigAndReload();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("先备份为 framework.ini.bak-<ts>，再写回默认值；部分开关需重启游戏生效");
    }
    ImGui::Separator();
    ImGui::Text("配置: %s", g_fw.cfgPath);
    ImGui::Text("日志: %s", g_fw.logDir);
    ImGui::Separator();
    ImGui::TextDisabled("关闭方式: 再按热键隐藏 GUI；框架随游戏进程退出");
    ImGui::End();
    (void)st;
}

static void DrawHud(void)
{
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    HWND hwnd = g_hwnd;
    float w = 800.0f, h = 600.0f;
    if (hwnd) {
        RECT rc;
        if (GetClientRect(hwnd, &rc)) { w = (float)(rc.right - rc.left); h = (float)(rc.bottom - rc.top); }
    }
    char line[160];
    wsprintfA(line, "SLModFramework %s  |  Insert=设置  |  PAMU=0x%08lX", SLMODF_VERSION, g_fw.pamuAddr);
    float scale = g_dpiScale > 1.0f ? g_dpiScale : 1.0f;
    dl->AddRectFilled(ImVec2(0, h - 30 * scale), ImVec2(w, h), IM_COL32(0, 0, 0, 90));
    dl->AddText(ImVec2(14 * scale, h - 24 * scale), IM_COL32(190, 220, 255, 220), line);
    dl->AddRectFilled(ImVec2(0, h - 30 * scale), ImVec2(4 * scale, h), IM_COL32(90, 170, 255, 220));
}

static bool g_cjkFontLoaded;
static ImFont *g_cjkFont;

static bool FontHasGlyph(ImFont *f, unsigned int cp)
{
    if (!f || cp > 0xFFFF) return false;
    return f->FindGlyphNoFallback((ImWchar)cp) != NULL;
}

static bool TryLoadCjkFont(const char *path, int baseSize, ImGuiIO *io)
{
    ImFontConfig cfg;
    ImFont *f;
    if (!io || !path || !path[0]) return false;
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return false;
    cfg.FontNo = 0;              /* TTC collection: face 0 */
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    f = io->Fonts->AddFontFromFileTTF(path, (float)baseSize, &cfg,
                                      io->Fonts->GetGlyphRangesChineseFull());
    if (!f) {
        Logf("overlay| CJK font load failed: %s", path);
        return false;
    }
    g_cjkFontLoaded = true;
    g_cjkFont = f;
    Logf("overlay| CJK font loaded: %s size=%.1f glyph_oxygen=%d (full 21000-glyph range)",
         path, (double)baseSize, FontHasGlyph(f, 0x6C27) ? 1 : 0);
    return true;
}

/* The default ImGui font (ProggyClean) has no CJK glyphs, so Chinese labels
 * were rendered as '?'. Load a CJK font with the FULL Simplified Chinese glyph
 * range (about 21000 ideographs) so rare characters such as U+6C27 氧 used by
 * the custom-night oxygen warning exist; config font_path selects the file and
 * font_size the base pixel size, otherwise system fonts are auto-detected.
 * Fall back to the default font (logged) if nothing is available. */
static void LoadCjkFont(void)
{
    ImGuiIO &io = ImGui::GetIO();
    static const char *cands[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",    /* Microsoft YaHei (Win7+) */
        "C:\\Windows\\Fonts\\msyhbd.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",  /* SimHei */
        "C:\\Windows\\Fonts\\simsun.ttc",  /* SimSun */
        "C:\\Windows\\Fonts\\Deng.ttf",    /* DengXian */
        "C:\\Windows\\Fonts\\msjh.ttc",    /* Microsoft JhengHei */
        NULL
    };
    const int baseSize = (g_fw.fontSize >= 10 && g_fw.fontSize <= 40) ? g_fw.fontSize : 14;
    int i;

    if (g_fw.fontPath[0] != 0) {
        Logf("overlay| font_path configured: %s", g_fw.fontPath);
        if (TryLoadCjkFont(g_fw.fontPath, baseSize, &io)) return;
        Logf("overlay| font_path unusable, trying system candidates");
    }
    for (i = 0; cands[i]; ++i) {
        if (TryLoadCjkFont(cands[i], baseSize, &io)) return;
    }
    g_cjkFont = NULL;
    Logf("overlay| CJK font not found, falling back to default font (Chinese renders as '?')");
    io.Fonts->AddFontDefault();
}

namespace overlay {

bool Ready(void) { return g_win32Init && g_dx9Init; }

bool Init(IDirect3DDevice9 *dev, HWND hwnd)
{
    if (!dev || !hwnd) return false;
    if (g_win32Init) return true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    g_ctxCreated = true;
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 4.0f;
    st.FrameRounding = 3.0f;
    st.WindowBorderSize = 1.0f;
    g_dpiScale = GetWindowDpiScale(hwnd);
    if (!g_styleScaled && g_dpiScale > 1.01f) {
        st.ScaleAllSizes(g_dpiScale);
        g_styleScaled = true;
    }
    io.FontGlobalScale = (g_dpiScale > 1.01f) ? g_dpiScale : 1.0f;
    LoadCjkFont();
    /* Build now (not on the first frame) so glyph coverage can be verified and
     * logged; the D3D9 backend reuses the built atlas. */
    io.Fonts->Build();
    if (!ImGui_ImplWin32_Init(hwnd)) {
        Logf("overlay| ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        g_ctxCreated = false;
        return false;
    }
    g_win32Init = true;
    if (!ImGui_ImplDX9_Init(dev)) {
        Logf("overlay| ImGui_ImplDX9_Init failed");
        ImGui_ImplWin32_Shutdown();
        g_win32Init = false;
        ImGui::DestroyContext();
        g_ctxCreated = false;
        return false;
    }
    g_dx9Init = true;
    g_dev = dev;
    g_hwnd = hwnd;
    Logf("overlay| ImGui %s init ok device=%p hwnd=0x%08lX dpi_scale=%.3f font_atlas=%dx%d fonts=%d cjk=%d glyph_oxygen=%d", IMGUI_VERSION,
         dev, (unsigned long)(ULONG_PTR)hwnd, (double)g_dpiScale,
         io.Fonts->TexWidth, io.Fonts->TexHeight, io.Fonts->Fonts.Size, g_cjkFontLoaded ? 1 : 0,
         g_cjkFont ? (FontHasGlyph(g_cjkFont, 0x6C27) ? 1 : 0) : -1);
    return true;
}

/* Offline UI-only path: builds the very same UI without a D3D9 device. It lets the
 * harness validate ImGui usage (Begin/End balance, tables, asserts) while the
 * session is locked, i.e. when no D3D9 device can be created. */
bool InitUiOnlyForTest(HWND hwnd)
{
    if (g_win32Init) return true;
    if (!hwnd) return false;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    g_ctxCreated = true;
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 4.0f;
    st.FrameRounding = 3.0f;
    st.WindowBorderSize = 1.0f;
    g_dpiScale = GetWindowDpiScale(hwnd);
    if (!g_styleScaled && g_dpiScale > 1.01f) {
        st.ScaleAllSizes(g_dpiScale);
        g_styleScaled = true;
    }
    io.FontGlobalScale = (g_dpiScale > 1.01f) ? g_dpiScale : 1.0f;
    LoadCjkFont();
    io.Fonts->Build();
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
    Logf("overlay| UI-only font atlas=%dx%d cjk=%d glyph_oxygen=%d", io.Fonts->TexWidth, io.Fonts->TexHeight,
         g_cjkFontLoaded ? 1 : 0, g_cjkFont ? (FontHasGlyph(g_cjkFont, 0x6C27) ? 1 : 0) : -1);
    if (!ImGui_ImplWin32_Init(hwnd)) {
        Logf("overlay| UI-only ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        g_ctxCreated = false;
        return false;
    }
    g_win32Init = true;
    g_hwnd = hwnd;
    Logf("overlay| ImGui %s UI-only init ok hwnd=0x%08lX dpi_scale=%.3f (no D3D9 device)",
         IMGUI_VERSION, (unsigned long)(ULONG_PTR)hwnd, (double)g_dpiScale);
    return true;
}

/* ---- P8 dev: hotkey scene warp menu (F9) ---------------------------------
 * Development-only. Lets the operator jump straight to any Fusion frame so
 * scene-specific rules (ThreatTelegraphs / QoL) can be exercised without
 * replaying a whole night. Uses scene::WarpToHandle() (verified p5.0b
 * protocol). Never part of the plugin API; not shipped in normal play. */
static bool g_warpMenuOpen;
static DWORD g_warpNamesTick;
static char g_warpNames[64][80];

bool WarpMenuOpen(void) { return g_warpMenuOpen; }
void ToggleWarpMenu(void)
{
    g_warpMenuOpen = !g_warpMenuOpen;
    if (g_warpMenuOpen) g_warpNamesTick = 0; /* refresh names on open */
}

static void RefreshWarpNames(void)
{
    DWORD now = GetTickCount();
    int h, n;
    if (g_warpNamesTick && (DWORD)(now - g_warpNamesTick) < 2000) return;
    g_warpNamesTick = now ? now : 1;
    n = scene::HandleCount();
    if (n > 64) n = 64;
    for (h = 0; h < n; ++h) {
        const char *nm = scene::HandleFrameName(h);
        snprintf(g_warpNames[h], sizeof(g_warpNames[h]), "%s", nm ? nm : "(未知)");
    }
}

static void WarpFromMenu(int handle)
{
    int idx = scene::HandleToFrameIndex(handle);
    const char *nm = scene::HandleFrameName(handle);
    int rc = scene::WarpToHandle(handle);
    Logf("warp| menu request handle=%d index=%d name=\"%s\" rc=%d", handle, idx, nm ? nm : "?", rc);
    if (rc > 0) g_warpMenuOpen = false; /* target frame is loading: close so it does not cover the scene */
}

static void DrawWarpMenu(void)
{
    const float s = (g_dpiScale > 1.01f) ? g_dpiScale : 1.0f;
    const SceneInfo *cur;
    if (!g_warpMenuOpen) return;
    RefreshWarpNames();
    cur = scene::Current();
    ImGui::SetNextWindowSize(ImVec2(640.0f * s, 660.0f * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(720.0f * s, 40.0f * s), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("场景跳转（开发，F9 开关）", &g_warpMenuOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("仅开发测试用：直接写 Fusion 运行时的 Jump to frame 请求（handle）。"
                       "点击目标后立即跳帧，菜单自动关闭；当前未存档进度会丢失。");
    ImGui::Text("当前场景: [%d] %s", cur->index, cur->name[0] ? cur->name : "?");
    ImGui::Separator();
    {
        static const int quickHandles[] = { 11, 0, 4, 7, 8, 9, 10, 20, 28, 26, 29, 31 };
        int i;
        for (i = 0; i < (int)(sizeof(quickHandles) / sizeof(quickHandles[0])); ++i) {
            int h = quickHandles[i];
            char lbl[96];
            snprintf(lbl, sizeof(lbl), "%s", g_warpNames[h]);
            ImGui::PushID(h);
            if (ImGui::Button(lbl, ImVec2(295.0f * s, 26.0f * s))) WarpFromMenu(h);
            ImGui::PopID();
            if ((i & 1) == 0) ImGui::SameLine();
        }
    }
    ImGui::Separator();
    if (ImGui::CollapsingHeader("全部 36 帧（handle -> index / name）")) {
        int n = scene::HandleCount(), h;
        if (ImGui::BeginChild("warp-all", ImVec2(0, 300.0f * s), true)) {
            for (h = 0; h < n; ++h) {
                char lbl[112];
                int idx = scene::HandleToFrameIndex(h);
                snprintf(lbl, sizeof(lbl), "h%02d -> %d  %s", h, idx, g_warpNames[h]);
                ImGui::PushID(1000 + h);
                if (ImGui::Button(lbl, ImVec2(300.0f * s, 24.0f * s))) WarpFromMenu(h);
                ImGui::PopID();
                if ((h & 1) == 0) ImGui::SameLine();
            }
        }
        ImGui::EndChild();
    }
    ImGui::Separator();
    if (ImGui::Button("关闭（F9）", ImVec2(160.0f * s, 26.0f * s))) g_warpMenuOpen = false;
    ImGui::SameLine();
    ImGui::TextDisabled("跳帧请求写入 RunHeader 后由运行时接管");
    ImGui::End();
}

int RenderUiOnlyForTest(void)
{
    if (!g_win32Init) return 0;
    plugins::EnsureLoaded();
    if (ConsumeReloadPlugins()) plugins::ReloadAll();
    plugins::OnUpdate();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawWarpMenu();
    if (g_fw.guiVisible) DrawMainWindow();
    if (g_fw.hudEnabled) DrawHud();
    plugins::DrawOverlay();
    ImGui::Render();
    ImDrawData *dd = ImGui::GetDrawData();
    return dd ? dd->TotalVtxCount : 0;
}

static int g_drawFrames;
static int g_lastVtx;
static int g_lastCmds;
static int g_prevVtx;

void RenderFrame(void)
{
    ImDrawData *dd;
    if (!g_win32Init || !g_dx9Init) return;
    /* Plugins are hosted here, on the render thread, before ImGui::NewFrame: the
     * Lua state and every callback stay on one thread and never block the worker. */
    plugins::EnsureLoaded();
    if (ConsumeReloadPlugins()) plugins::ReloadAll();
    plugins::OnUpdate();
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    {
        ImGuiIO &dio = ImGui::GetIO();
        if (dio.MouseClicked[0])
            Logf("input| imgui click at %.1f,%.1f display=%.1fx%.1f gui=%d", dio.MousePos.x,
                 dio.MousePos.y, dio.DisplaySize.x, dio.DisplaySize.y, g_fw.guiVisible);
    }
    if (g_fw.guiVisible) DrawMainWindow();
    if (g_fw.hudEnabled) DrawHud();
    plugins::DrawOverlay();
    DrawWarpMenu();
    ImGui::Render();
    dd = ImGui::GetDrawData();
    if (dd) { g_lastVtx = dd->TotalVtxCount; g_lastCmds = dd->CmdListsCount; }
    ImGui_ImplDX9_RenderDrawData(dd);
    g_drawFrames++;
    /* Draw telemetry: proves the ImGui geometry actually reached the device. The
     * Phase-3 acceptance uses vtx>0; 600 frames keep the log small (about every
     * 10 s at 60 fps). Rising edges (vtx 0 -> >0) are always logged so a GUI that
     * opens and closes between two periodic samples still leaves proof behind. */
    if (g_drawFrames <= 3 || (g_drawFrames % 600) == 0 || (g_lastVtx > 0 && g_prevVtx == 0))
        Logf("draw| frame=%d vtx=%d cmds=%d gui=%d hud=%d presents=%d",
             g_drawFrames, g_lastVtx, g_lastCmds, g_fw.guiVisible, g_fw.hudEnabled,
             (int)d3d9hook::g_info.presents);
    g_prevVtx = g_lastVtx;
}

int LastVtx(void) { return g_lastVtx; }
int RenderedFrames(void) { return g_drawFrames; }

void Invalidate(void)
{
    if (!g_dx9Init) return;
    ImGui_ImplDX9_InvalidateDeviceObjects();
    g_objectsOk = false;
    Logf("overlay| device objects invalidated");
}

void CreateObjects(void)
{
    if (!g_dx9Init) return;
    g_objectsOk = ImGui_ImplDX9_CreateDeviceObjects();
    Logf("overlay| device objects recreated ok=%d", g_objectsOk ? 1 : 0);
}

void Shutdown(void)
{
    plugins::Shutdown();
    if (g_dx9Init) { ImGui_ImplDX9_Shutdown(); g_dx9Init = false; }
    if (g_win32Init) { ImGui_ImplWin32_Shutdown(); g_win32Init = false; }
    if (g_ctxCreated) { ImGui::DestroyContext(); g_ctxCreated = false; }
    g_dev = NULL; g_hwnd = NULL;
}
} // namespace overlay

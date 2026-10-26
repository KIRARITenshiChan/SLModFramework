# SLModFramework（Phase 5/6/7/8：Overlay + 插件宿主 + 运行时状态/DebugInspector + 音频 Hook + 威胁预警 + QoL）

32 位进程内框架：指纹门禁 → 等 `mmfs2.dll` → 扫描 PAMU 校验 Fusion build 286 → D3D9 Hook → Dear ImGui 状态面板 + 热键开关 + 输入屏蔽。

P2 的只读骨架（`native/slmodf.c`）保留作历史，不再编译。当前版本字符串 `0.7.0-p8`（插件 API 版本 `SLMODF_PLUGIN_API_VERSION = 1`）。【v1.20】安装布局 B：运行目录独立为 `<game>\SLModFramework\`，框架根 = `bin` 的父目录、游戏根 = 框架根父目录；config/plugins/logs/sfx 全部相对框架根（详见 `../docs/INSTALL_LAYOUT.md`）。

## 构建

```bash
"/d/Scoop/apps/git/current/bin/bash.exe" build.sh
```

- 编译器：winlibs i686-mingw-w64 gcc/g++ 16.2.0（`D:/devtools/winlibs-i686/mingw32`），输出 PE32 x86。
- 产物：`bin/SLModFramework32.dll`、`bin/launcher32.exe`；离线测试台 `bin/harness32.exe`（见下）。
- 部署：`bash deploy.sh`（可选 `--with-docs --with-sfx`）把 bin + 配置模板 + HelloPlugin 复制到 `<game>\SLModFramework\`，校验游戏根存在；真机测试一律启动安装目录的 launcher。
- 依赖已 vendor：`../third_party/imgui`（v1.91.9b, MIT）、`../third_party/minhook`（v1.3.4, BSD-2）、`../third_party/lua-5.4.7`（MIT，只编 src/ 下的 C 源码，排除 lua.c/luac.c）。DLL 静态链接，不依赖 libstdc++/libwinpthread。

## 运行

```bash
bin/launcher32.exe                              # 启动游戏并注入，等游戏退出
bin/launcher32.exe --test-exit 20               # 初始化后 20 秒自动 WM_CLOSE（回归用）
bin/launcher32.exe --test-exit 40 --toggle-gui 12,22 --test-input 16
bin/launcher32.exe --test-exit 35 --toggle-gui 12,22 --toggle-key 0x71 --test-input 16   # 配置了 hotkey=f2 时必须同步改 --toggle-key
bin/launcher32.exe --exe <路径> --no-verify     # 诊断用，日志标注 unverified
```

- `--toggle-gui <csv 秒>`：到点向游戏**有焦点的子窗口**（`Mf2EditClassTh`，找不到则退回顶层）PostMessage 一次 `VK_INSERT` **按下**（不发抬起），模拟玩家按键并把 GUI 留在新状态，便于后续 `--test-input` 在 GUI 打开时验证吞噬。不要向顶层+子窗口广播同一条按键：两处都被子类化会 toggle 两次（16:08 实测）。
- `--test-input <秒>`：到点向同一个焦点子窗口 PostMessage 空格按下/抬起 + 鼠标左键按下/抬起 + 鼠标移动，验证 GUI 打开时输入被吞掉（框架日志 `input| swallow …`）。
- `--toggle-key <VK>`：`--toggle-gui` 投递的虚拟键，默认 `0x2D`（Insert，对应 `hotkey=insert`）；若 `framework.ini` 改成了 F2 等，必须同步传 `--toggle-key 0x71`（也接受十进制 `113`）。
- 目标 EXE 默认按 loader 位置推导（布局 B）：`<game>/SLModFramework/bin/launcher32.exe` → 框架根 `<game>/SLModFramework` → 游戏根 `<game>` → `<game>/SisterLocation.exe`（并校验存在）；可用环境变量 `SL_GAME_EXE` 或 `--exe` 覆盖。旧布局 `Workspace/framework/bin` 已不再用于运行测试。

## 配置（Phase 3 新增）

`<game>/SLModFramework/config/framework.ini`（**首次运行若不存在会自动写入默认文件**；任何行都可省略）。格式示例见 `../config/framework.ini.example`（运行目录内为 `SLModFramework\config\framework.ini.example`）。带版本字段与损坏恢复：`config_version` 缺失按 v1 迁移（不重写文件），非法/未知版本/二进制/无效行过多 → 备份为 `framework.ini.bad-<timestamp>` 后写回默认值（日志 `config| RECOVERED: …`）。

```ini
hotkey=insert          ; insert | f1..f12 | home | end | delete | 0..9 | 0xNN（如 0x71=F2；改这里时 launcher 的 --toggle-key 要同步）
hud=0                  ; 0/1；1 = GUI 关闭时仍在屏幕底部画状态条
hook_d3d9=1            ; 0/1；0 = 完全不安装 D3D9 Hook（隔离排障用）
wait_mmfs2_ms=60000    ; 等 mmfs2.dll 的上限毫秒（0..120000，超范围保持默认）
# P5：状态记录
record=0               ; 0/1；场景状态 JSONL 录制（默认关，GUI 也可切换）
record_interval_ms=500 ; 100..10000
# P6：音频 Hook（默认全关，不影响原版音效）
audio_probe=0          ; 0/1；DirectSound 探针 + 样本指纹匹配（只读，只写日志）
audio_soften=0         ; 0/1；对识别到的 jumpscare scream 衰减音量（需 audio_probe=1）
audio_gain_percent=25  ; 1..100；目标增益（25 ≈ -12 dB）
# P8：QoL（默认开；均可单独关闭）
qol_release_mouse=1    ; 0/1；失焦或 GUI 打开时释放鼠标（ClipCursor/SetCursorPos/ShowCursor）
qol_keypad_numpad=1    ; 0/1；帧 19 密码盘用小键盘 1..9 直接输入
qol_private_hotkeys=1  ; 0/1；帧 27 Private Room 用 A/D/W/S 操作左门/右门/通风口/翻面板
qol_door_sfx=1         ; 0/1；注入开关门时播放本机 Doorslam 素材（素材不入发布包）
# v1.17：Overlay 字体
font_path=             ; 空=自动候选 msyh.ttc 等；也可指定任意 TTF/TTC
font_size=14           ; 10..40；CJK 基础字号（DPI 另乘系数）
```

校验规则（16:58 加入，实测见 `docs/OVERLAY_INPUT.md` §4.2）：未知/非法值不会被静默接受——`hotkey=bogus` 保留默认 Insert 并打 `config| warn invalid hotkey …`；`hud`/`hook_d3d9` 只接受 0/1，否则保持旧值并 warn；`wait_mmfs2_ms` 超出 0..120000 直接保持默认。

## Phase 3 行为

1. **早期 Hook**：worker 线程在等 `mmfs2.dll` 之前就 `LoadLibraryA("d3d9.dll")` + MinHook `d3d9!Direct3DCreate9`，创建后立即 `MH_EnableHook`（只创建不启用是 P3 第一版 presents=0 的根因）。
2. **Device 级 Hook**：`Direct3DCreate9` 返回对象后 Hook `IDirect3D9::CreateDevice`；`CreateDevice` 成功后 Hook `IDirect3DDevice9::Reset`(/vtable #16) 与 `Present`(#17)，记录 `D3DPRESENT_PARAMETERS`。
3. **Overlay**：`Present` 里懒初始化 ImGui Win32+DX9 后端，绘制状态面板（指纹/门禁/module/PAMU/D3D9 参数/Present 计数/FPS/DPI/热键/吞键统计；按 DPI 缩放为 600×520 逻辑并在 150% 下 1.5 倍），可再叠 HUD。**中文字体**：默认 ProggyClean 无 CJK 字形（首版全显示 `????`），`overlay.cpp` 的 `LoadCjkFont()` 在构建图集前加载 `C:\Windows\Fonts\msyh.ttc`（回退 msyhbd/simhei/simsun/Deng/msjh → 再回退默认），日志 `overlay| CJK font loaded: …`，`ui| hotkey … hud=… gui_visible=…` 记录初始状态。**v1.17 字体**：改用 `GetGlyphRangesChineseFull()` 全字库（约 2 万字，修复「氧」等缺字），新增配置 `font_path`（空=自动候选，可自定义 TTF/TTC）与 `font_size=14`（10–40）；加载后 `io.Fonts->Build()` 再探测 `氧`(U+6C27)，日志 `font_atlas=2048x4096 glyph_oxygen=1`，详见 `../docs/OVERLAY_INPUT.md` §8。
4. **窗口与输入**：子类化本进程所有顶层窗口（实测游戏为 `Mf2MainClassTh`）及其子窗口（`Mf2EditClassTh`，真正的渲染/输入面）；`WM_KEYDOWN == hotkey` 切换 GUI 并吞掉该键；GUI 可见时除白名单（CLOSE/QUIT/DESTROY/ACTIVATE/SIZE/MOVE/DPICHANGED/PAINT/SETCURSOR/SYSCOMMAND 与 Alt+F4/Tab/Esc）外，键盘/鼠标消息全部吞掉并计数。
5. **设备丢失/重置**：`Reset` 时 `ImGui_ImplDX9_InvalidateDeviceObjects` → 原 Reset → 成功后 `CreateDeviceObjects`；`Present` 返回 `D3DERR_DEVICELOST` 时 Invalidate。

## Phase 4 行为：插件宿主与 API v0（2026-10-25 完成）

- 插件根目录 `<game>\SLModFramework\plugins\<目录>\`（`plugin.ini` + 入口 .lua，默认 `main.lua`）；上限 32 个。
- Lua 5.4 **沙箱**：仅 base/table/string/math/coroutine/utf8；`io/os/package/debug` 与 `dofile/loadfile/load/require` 全部移除；每个插件独立只读 `_ENV`，共用同一个 Lua state（全在渲染线程）。
- 全局 `sl`：`sl.api`、`sl.api_version`、`sl.framework_version`、`sl.log()/sl.warn()`。
- 生命周期：`plugin_info()`（必需）→ `on_load(ctx)` → `on_update(ctx)`（每帧）→ `on_draw_ui(ui)`（GUI 打开时每帧，画在框架面板内）→ `on_draw_overlay(ui)`（Phase 7；每帧，画在屏幕前景，不要求 GUI 打开）→ `on_shutdown()`。
- `ctx.game`（2026-10-26 01:48 起接入 Phase 5 只读状态）：`supported`、`scene_id`、`scene_name`、`scene_width/height`、`counters_ready`、`counters`（名字→整数快照，最多 64 项）、`frame_id`、`fps`、`presents`；Phase 7 增 `objects_ready` 与 `alterables`（按对象名的 0-based Active alterable 快照，当前监视 `Ennard`/`flip it`）。v0.5.0 增 `object_rect(name)`（`{x,y,w,h}`）与 `alterable(name,index)`（任意 Active 对象实时值），用于读事件创建的动态对象（`Active 19`）与 Freddy 精确状态；插件仍拿不到任何原始地址。已知限制：同名计数器（如 `compute`）在 Lua 表里只留最后一个。详见 `../docs/PLUGIN_API_V0.md` §4/§4.2 与 `../docs/RUNTIME_STATE.md` §9。
- `ui`：面板回调 `on_draw_ui` 有 `text/text_disabled/separator/same_line/checkbox/slider_int/button` 七个原语（后两个 P6 新增）；Phase 7 新增前景回调 `on_draw_overlay` 及其独立原语 `viewport/text_size/text_at/rect_filled/rect/line`（详见 `../docs/PLUGIN_API_V0.md` §5.1）。
- 状态：已禁用/已加载/API 不匹配/错误/清单无效；所有 Lua 调用走 `lua_pcall`，单插件异常不影响游戏与其它插件（错误日志前 3 次逐条、之后每 600 次一条）。
- GUI "框架/插件" 区显示 `插件 API v1 | total=… loaded=…`，逐插件复选框启停：写回 `plugin.ini`（原子替换 tmp+MoveFileEx）并重载；"重新扫描插件"按钮重载全部。
- `ctx.audio`（2026-10-26 04:0x 起接入 Phase 6 音频探针）：`supported/soften_enabled/gain_percent`、`last{name,handle,chunk,scream,…}`、`stats{creates,plays,matched,screams}`、`set_soften/reset_soften/set_names/clear_names`；需 config `audio_probe=1`。详见 `../docs/PLUGIN_API_V0.md` §4.1。
- 完整 API 文档：`../docs/PLUGIN_API_V0.md`；样板插件：`../plugins/HelloPlugin/`（最小生命周期 + 中文 UI）、`../plugins/SoftJumpscares/`（ctx.audio 实战：开关/滑条/白名单/统计）与 `../plugins/ThreatTelegraphs/`（Phase 7：ctx.game.alterables + on_draw_overlay 实战）。

### Phase 4 验收结果（2026-10-25 21:14）

```powershell
# 离线（6 个夹具：good/mismatch/syntax/disabled/runtime/sandbox）
cd Workspace/framework/bin; ./harness32.exe --plugins   # rc=0，SUMMARY4 pass=1 total=6 vtx=2586
# 真实游戏（Windows 会话需已解锁）
Workspace/framework/bin/launcher32.exe --test-exit 40 --toggle-gui 10
```

- 离线夹具 `SUMMARY4 pass=1 total=6 vtx=2586 good=1/3/0 mismatch=1 syntax=1 disabled=1 runtime_errors=3 sandbox=1/0`（`../logs/framework/harness-20261025-205954.log`）。
- 真实游戏 pid 3588/23104/20904/19188：HelloPlugin 加载、面板自绘 UI 中文正常；GUI 勾选框禁用 → `plugin| set enabled=0` → 清单回写、重启保持禁用、再启用恢复（截图 `../logs/probe/p4-final-plugins.png`、`p4-final-plugin-disabled.png`）。
- Alt+Tab 回归（GUI 打开时切走再切回）：presents 237→2038、fps 58.0、device 不变、无 DEVICELOST。

## Phase 5 行为：运行时状态读取 / DebugInspector（2026-10-26 首版完成）

- 新增 `native/scene.cpp`（P5.0b）+ `native/state.cpp`：
  - `scene`：识别当前 Fusion 帧（帧号/名字/尺寸/层数/实例数/RunHeader）。**快速路径（2026-10-26 01:40）**：缓存 CRunApp 后每 tick 只读 `app+0x1D8 (m_Frame)`，正常运行时零内存扫描；缓存来源 = RunHeader+0x18，或节流 1.5 s 的全内存签名扫描（`(maxIndex=36, maxHandle≥36, handle→index 表指针)`）。实测三次跳帧检测延迟 1.7–3.2 s（等于帧加载本身的时间），日志 `scene| fast idx=…`、启动时 `scene| fast path ready app=… via runheader`；帧对象地址会被复用，身份只能靠名字+尺寸。
  - `state`：实时读当前帧的 **对象表与计数器值**（只读）。构建路径：`scene` 给 CRunFrame → `frame+0x7C` 取 RunHeader → `rh+0x54/+0x58` 取 OI 表 → 全内存搜 `dword==rh` 枚举对象并校验 → 计数器值 = `~(int16)*(obj+0x20A)`。
  - 帧内存复用安全：除 frame base 外还比较 RunHeader，变化即重扫（日志 `state| frame memory reused, rebuilding map`）。
- GUI 新增「场景状态 (DebugInspector)」：状态/frame_index/对象数/计数器数/RunHeader + 逐计数器 `名字 = 值`（每行 3 个；无效 `?`）。帧 1/9/27/31 有场景专用 watch 列表（option、Freddy、danger level 4、hour、power left、final stage、side、count seconds 等）。
- 日志：`state| map frame=… rh=… oi=… objs=… counters=… regions=… bytes=… ms=…`、`state| scene=… objs=… ctr=… …`（每 5 s）。
- 实测（2026-10-26 01:23–01:28）：
  - 标题页 option 与菜单高亮同步 1→2→3（截图 `../logs/probe/p5state5-option2.png`、`p5state5-option3.png`）。
  - Final Encounter：hour=12、power left 随 POWER 条下降、final stage 随 fade-in 推进、side=2、count seconds 递增（截图 `../logs/probe/p5state4-gui-fe.png`）。
  - Breaker Room：对象 104 / 计数器 30，Freddy/danger level 4/got you/beat/interface 可读（截图 `../logs/probe/p5state4-gui-breaker2.png`）。
  - 全表构建耗时 230–360 ms、扫描 350–780 MB，只在帧切换/rh 变化时发生（worker 线程）。
- 一键验收：`pwsh -File ../tools/p5_state_check.ps1 -Tag p5state5`（启动→开 GUI→warp 帧 9→截图→warp 帧 27→截图→写报告）。

### 状态记录器 recorder（2026-10-26 01:58 完成）

- 源码 `native/recorder.h`/`recorder.cpp`；配置 `record=0/1`（默认 0）+ `record_interval_ms=100..10000`（默认 500）；GUI「场景状态」底部勾选「状态录制 (JSONL)」可运行时开关。
- 输出 `<game>\SLModFramework\logs\state\state-YYYYMMDD-HHMMSS-<pid>.jsonl`，每行：`t/time/scene/scene_name/w/h/layers/instances/rh/scene_changed/counters{}`；场景切换必写，同场景按周期写，每 5 行 flush。
- 场景切换行会等 state 的 map+values 就绪（`state::MapScene()`/`state::ValuesReady()`，最长 3 s）再写，因此 `scene_changed=1` 行自带完整计数器；同名计数器写成 `compute#2`、`compute#3`…。
- 实测：`../logs/state/state-20261026-015801-2904.jsonl` 95 行，scene 0/1/9/27 四行分别 3/10/30/19 个计数器，无重复 JSON 键；日志 `recorder| scene 9 "Breaker Room" counters=30 line=21`；截图 `../logs/probe/p5state11-gui-recorder.png`。
- 用途：Phase 7 ThreatTelegraphs 的「攻击前置状态时间线」证据（双轨证据规则的机器可读部分）。warp 进入的帧同样会记录，但结论仍需自然游玩复核。
- 快速路径验收：`pwsh -File ../tools/p5_fastpath_check.ps1`（启动→等标题页→跳 Extras(18)/Breaker(9)/Final(27)，从日志测每次检测延迟；输出 `DETECT/MISS` 与 `RESULT PASS`，报告 `../logs/probe/p5fast-report.txt`）。
- 详细偏移/证据/限制见 `../docs/RUNTIME_STATE.md`。

## Phase 6 行为：音频 Hook 预研（SoftJumpscares，2026-10-26 02:50）

- 源码 `native/audio_hook.cpp`（+ `sound_table.h` 自动生成）：MinHook 链 `DirectSoundCreate` → `IDirectSound::CreateSoundBuffer` → 每个 buffer 的 `Play`(vt12)/`SetVolume`(vt15)；primary/secondary 的 Play 实现地址不同，因此按目标地址安装多份薄 detour（Play 8 / SetVolume 6 个变体）。
- **流式 chunk 事实**：Fusion 不把整段样本放进 buffer，而是 1.2 s 一块（44100/mono/16 = 105840 B、48000 = 115200 B、44100/stereo = 211680 B）；chunk #0 与样本 PCM 前缀逐字节一致（dump 验证）。匹配用 `gen_sound_table.py` 生成的 FNV-1a 64 前缀/chunk 指纹（178 条样本；scream 7 条：86/112/120/148/257/265/303）。
- 实测识别（PID 19712，Breaker Room 强制死亡链）：`audio| play #12 … name="scream op5-7" how=3 chunk=0 scream=1`；柔化（PID 24028，`audio_soften=1`）：同毫秒 `audio| soften … gain=25% off_cb=-1204 volume=0 -> -1204 (readback=-1204)`；回滚（PID 15276，`audio_soften=0`）：仍识别但 0 条 soften。普通音效（pop1/Static2 等）从不柔化。
- 帧 27（PID 23104，Night 5 Final Encounter 强制 got you=1）：`name="scream op5-6" handle=303 chunk=0 scream=1` + 同毫秒 `soften … readback=-1204`；dump `logs/audio/dump-23104-007-*.bin`。
- 插件化（PID 20980，`audio_soften=0` 只开 probe）：SoftJumpscares 加载后 `audio| plugin set_soften=1 gain=25%`，帧 27 scream 被柔化（readback=-1204）→ 插件可独立控制柔化；GUI 截图 `logs/probe/p6e-plugin-ui.png`。
- 配置：`audio_probe`（默认 0）、`audio_soften`（默认 0）、`audio_gain_percent`（1..100，默认 25）。**默认全关 = 与原版音效完全一致。**
- 一键验证：`pwsh -File ../tools/p6a_scream_test.ps1 -TestExit 300 -WatchSec 60`（启动→跳帧 9→写 Freddy=6 触发原生死亡链→统计 scream 匹配；退出码 0=识别成功、7=未识别）；帧 27 用 `pwsh -File ../tools/p6c_final_test.ps1 -Mode forced`；插件化用 `pwsh -File ../tools/p6d_plugin_test.ps1 -TestExit 300 -WatchSec 75`。dev 写内存工具 `tools/p6_trigger_scare.py` 不属于框架功能。
- 音频 dump 在 `../logs/audio/`（含游戏音频片段，仅本地取证）。完整报告/限制见 `../docs/AUDIO_HOOK.md`。

## Phase 7 行为：ThreatTelegraphs 攻击预警（2026-10-26 06:10 首版；v0.2.0 补帧 9；v0.3.0 全游戏矩阵；v0.4.0 Ballora/多通道/安全提示；v0.4.1 时序修正；v0.4.3 门控修复；v0.4.4 Foxy 名字截断修复；v0.5.0/v0.5.1 帧 31 Freddy 精确门/Bonnet 倒计时/状态字幕/动态对象）

- 新增 **前景绘制回调** `on_draw_overlay(ui)`：每帧在 `DrawHud()` 之后、`ImGui::Render()` 之前调用，不要求 GUI 打开；插件用独立前景 DrawList 原语（`ui.viewport/text_size/text_at/rect_filled/rect/line`）画预警，坐标=物理客户区像素，自动盖在 GUI 之上。`plugin| loaded … callbacks … overlay=1` 表示发现该回调。
- 新增 **Active alterable 读取**：state 模块从对象 `rVal` 复制 26 个 CValue（type0 long / type2 double），Phase 7 监视 `Ennard` / `flip it`；`ctx.game.alterables.Ennard[3]` 是 Night 5 Final Encounter 的路径状态（0 idle；1–4 左；5–6 通风口；7–10 右；99 攻击），插件据此生成低警/高警/攻击提示。
- **v0.5.0 扩展**：除白名单快照外，新增 `ctx.game.alterable(name,index)` 实时读任意 Active 对象（Freddy 的 0/1/2/3/6、`flip it[0]`、`Active 19[1]/[2]`），并配合 `object_rect` 读动态对象屏幕/场景坐标；Published 快照滞后问题由 `rhObjectList` 回退 + live 读解决。
- 首个插件 `../plugins/ThreatTelegraphs/`：默认 **关闭**；帧 27 规则为 4/6/10 且对应门未关→高警“左门/通风口/右门 + 关门”，1–3/5/7–9→低警，99 或 got you>=1→攻击；门已关时不提示。GUI 内可启用、调大小、看调试数据、预览 3 秒。
- **实机验证（pid 24336，2026-10-26 06:06–06:10）**：warp 到帧 27 后 `Ennard[3]` 0→1→0→5→6→99；低警“通风口靠近”（截图 `../logs/probe/p7-gui-scroll-24336.png`）、高警“通风口 + 关门”红牌+顶部红条（`../logs/probe/p7-threat-high-vent-24336.png`）、攻击日志 `alert=attack/center` 在切 death 前 0.77 s。日志 `../logs/framework/slmodf-20261026-060601-24336.log`。
- 默认关闭验证：`plugins/ThreatTelegraphs/main.lua` 的 `CONFIG.enabled=false`；pid 19872 加载日志 `ThreatTelegraphs loaded enabled=false`，规则关闭时 `on_draw_overlay` 直接 return（不绘制）。
- **帧 9（Night 2 Breaker Room）规则（v0.2.0，2026-10-26 06:30–06:39）**：`Freddy` 是 0..6 危险值，自然局实测序列 **1,2,4,6**（会跳级）；阈值因此取 `>=2` 低警、`>=4` 高警、`>=6` 或 `got you>=1` 攻击。`beat>=1`（interface>=800 通关）时插件静默，但 G58 攻击判定没有 beat 条件——`beat` 只是显示规则、不是护盾（开发期强写 beat=1+Freddy=6 仍会死，见 `THREAT_RULES` §6.4）。
- **帧 9 实机验证（2026-10-26 06:30–06:37）**：pid 19272 写 `Freddy=4` 得到高警截图 `../logs/probe/p7n2-high-freddy4-19272.png`（顶部红框“Freddy 即将出现 4/6”+“准备应对”），写 `Freddy=2` 得到低警 `p7n2-low-freddy2-19272.png`；pid 13216 写 `Freddy=6` 用新工具 `../tools/p7_attack_capture.py` 连拍，`p7n2-attack-13216_d150.png`/`_d350.png` 是有效攻击证据（中心“Freddy 已抓住你 / 本局结束”+红边），`_d550.png` 已切 death 无效。攻击窗口实测 0.748–1.0 秒（含 state 轮询延迟）。
- 默认关闭（v0.2.0）验证：pid 3584 加载日志 `ThreatTelegraphs loaded enabled=false debug=false`；warp 帧 9 后写 `Freddy=4` 无任何预警/overlay，截图 `../logs/probe/p7-default-off-3584.png`。
- **全游戏矩阵 v0.3.0（2026-10-26 16:20）**：`../plugins/ThreatTelegraphs/main.lua` 扩到 8 个可读场景——帧 8（Ballora `distance/progress/jumpscare`）、9（Freddy/beat/interface）、10（Funtime Aud. `distance/recharge/planned death`）、11（timer/ready/win，超时条件 Script Event>=164）、13（`Counter/steps`，默认关闭待标定）、19（密码盘 `death timer/chain` + 回合 timer）、27（`Ennard[3]` + 门 + `side`）、31（Custom Night：Foxy 3/4/5、Bidy 3/4、Freddy 12/5、Oxygen<=30、Minireena、Ennard 4/6/10）。帧 16（唯一连续量 `windpad` 不在 alterable 白名单）与帧 26（碰撞瞬时，只有 died）**不做**，仅保留通用锁存 `got you/jumpscare/died/planned death`。同一帧只显示最高优先级一条（attack>high>low）；默认 `CONFIG.enabled=false`、`scene13_enabled=false`。离线 50 条断言 + `luac -p` 通过；除帧 9/27 外仍待自然采样校准。规则/证据/限制：`../docs/THREAT_RULES.md`（v0.3.0，270 行）。已知：攻击档提示太晚（约 0.75–1.0 s），真正有效的是到门/>=4 高警；state 未就绪时不报（宁漏勿误）。
- **v0.4.0（2026-10-27 00:1x，用户 m10402 三项改进）**：①Ballora 实时位置与到门倒计时——帧 31 用 `Ennard[3]` 路径状态 + `Ballora count`（每段约 10 s，ETA≈count/5）+ `current volume`，显示「Ballora 在左路 · 音乐 5/50 · 约 10s 到门」，danger/door 段转红色高警/攻击「左门…未关！立即按 A 关门」；帧 8 显示「Ballora 距离 d/600 · 正在回退/还在靠近 · 约 Xs 到死亡线」。②多通道告警并存（按方向各一条，attack 全屏 + 其它方向低警同屏）+ 威胁消失后 2.5 s 绿色安全提示（「左门安全」），配置 `safe_notice_seconds`。③字体全字库修复「氧」缺字（见 Phase 3 Overlay 节）。实机证据：`../logs/probe/p9-sc31-low-002058.png`、`p9-sc31-high-002107.png`、`p9-sc31-attack-002117.png`（多通道同屏）、`p9-sc31-safe-002759.png`（安全提示）、`p9-sc31-oxygen_d350.png`（氧字+位置行）、`p9-sc8-low_d150.png`/`p9-sc8-high_d150.png`；日志 `slmodf-20261027-001644-20584.log`；离线断言 `../tools/tt_offline_check.lua` pass=10。限制：帧 31 的 A/D/W 仍只在帧 27 生效（场景 31 需内存写门状态）；warp 场景 31 会唤醒 AI，约 40–90 s 后死亡。
- **v0.4.1（2026-10-27 01:0x，corrects v0.4.0）**：按 task-4 时序研究 `../research/p7-global/ballora-timing.md` 修正——脉冲周期 `(50 − Ballora Diff)×0.2 s`、剩余 `clamp(count−Diff,0,50−Diff)×0.2 s`（Diff3→9.4 s 实测 9.273–9.528）；到门 4/10 + 门开不再报 attack，而是 **high + 实时关门倒计时**（≤2 s 升级「马上关门」），因为下一脉冲才会 99，最长约 9.4 s 关门窗口；3/9→high；2/8、1/7→low；通风口 5/6 在帧 31 是死代码；`attack` 只保留 `got you≥1`；入场 `Ballora Diff` 伪值（实测 12464 持续约 47 s）用连续 0.5 s 稳定 1..20 门控。离线断言 `../tools/tt_offline_check.lua` **30 项全过**；无 native 改动。文档 `../docs/THREAT_RULES.md` §14。
- **v0.4.3（2026-10-27 01:3x，修 v0.4.2 假安全）**：用户反馈帧31 后半段关门后提示「安全」，再开门即被秒杀。根因是 v0.4.2 的稳定门控写死 `Ballora Diff <= 20`，而 4–5 AM 时 Diff=25/30，整条 Ballora 规则被禁用后退回 generic 兜底（只在门开时报 high，关门即清空）→ 假安全；门虽然关了，但 Ennard[3] 仍停在 4/10，下一脉冲才结算攻击。门控改 `<= 60`（Diff 每小时 +5、5 AM≈30、12464 为入场伪值），离线新增 E5/E6 两条，**42 项全过**。
- **v0.4.4（2026-10-27 01:4x，修 Foxy 静默）**：自动化仿真发现 Funtime Foxy 规则自 v0.3.0 起从未触发——融合 OI 计数器名被截断成 `Funtime Foxy progressio`（23 字符），插件按全名 `Funtime Foxy progression` 精确查 `ctx.game.counters` 永远取不到。新增 `resolve_name()`：精确匹配失败后双向唯一前缀匹配（共享前缀 ≥8 字符，多义放弃），`num()/has()` 全部改走它。离线新增 P/P2/Q/Q2/R 五条，**47 项全过**；实机 pid 12092 日志 `01:40:14 foxyProg=4 ... alert=high dir=right`、`01:40:23 foxyProg=5 gotYou=2 alert=attack`。Foxy 关门语义=立即重置 progression（G414），不是 hold；Bidybab 关通风口同样立即归零（G271/G272）。仿真研究 `../research/p7-global/enemy-simulation-20261027.md`，工具 `../tools/p9_attack_probe.py`（dev-only，100 ms 全计数器采样+按时注入）。Freddy 精确方向在 v0.5.0 已通过 `ctx.game.alterable("Freddy",i)` 实现（无需扩 `kAlterableWatch`，见 §17）。文档 `../docs/THREAT_RULES.md` §16。
- **v0.5.0 / v0.5.1（2026-10-27 02:5x–03:1x，用户 m11423 帧 31 精确模型）**：①**native 新 API**：`ctx.game.object_rect(name)` → `{x,y,w,h}`（`state::ReadObjectRect`，读 `hoX@0x4C/hoY@0x54/hoImgWidth@0x60/hoImgHeight@0x64`）、`ctx.game.alterable(name,index)` → int（`state::ReadAlterableLive`，任意 Active 对象、非仅白名单）；②**动态对象回退**：Published 对象表只在场景切换扫描，事件 Create 的 `Active 19`/`Bonnie scare` 不可见——新增 `RunHeader.rhObjectList`（`rh+0x1278`）+ `rhNObjects`（`rh+0x3C`）遍历（每项 8 字节 `{objPtr,routine}`，用 `hoOi@0x12` 查 OI 名；`STATE_MAX_LIVE_OBJS 4096`，日志 `state| live rect fallback name=Active 19 ...` 预算 8 行）；偏移用 `#pragma pack(2)` 镜像 `Cncf.h` + 已知锚点（rhApp=0x18/rhFrame=0x1C/rhQuit=0x30/rhQuitParam=0x38）校准，实机 251 项与全内存扫描一致；③**ThreatTelegraphs v0.5.1**：帧 31 **Funtime Freddy 精确侧别/阶段**（alt0 侧别、alt6=3→同侧/5→对侧、alt1 语音→状态字幕、门关 hold/取消）、**Bonnet 行走倒计时**（`Active 19` 实时 x、速度 180×N px/s、ETA=(x+230)/speed、点鼻提示、live `flip it` 读取修复 published 快照滞后）、**状态字幕**（Freddy 语音映射，底部居中；Phase 9 框架提案已写入计划）；④离线 `../tools/tt_offline_check.lua` **64 项全过**；实机 pid 23520 `alert=high dir=left reason=Freddy alt6=3 alt0=1 closed=0` + 字幕截图 `../logs/probe/p9-v050-freddy-subtitle2.png`，pid 29848 `alert=high dir=center reason=Bonnet x=1201 eta=2.0 speed=720`；⑤限制：Bonnet 生成随机（random(30)<Diff）、行走窗口 1–6 s 无视觉截图、Freddy 语音文本依赖 dump 映射；文档 `../docs/THREAT_RULES.md` §17、研究 `../research/p7-global/freddy-bonnet-20261027.md`。完整文档 `../docs/THREAT_RULES.md` §17。

## Phase 8-QoL 行为：鼠标释放 / 密码盘小键盘 / Private Room 快捷键（2026-10-26 23:14 补齐门动画与面板原生链）

- 源码：`native/d3d9hook.cpp`（QoL 块）+ `native/state.cpp`（`ReadCounterLive/WriteCounter/WriteAlterable/WriteAnimation/ReadObjectRect/WriteObjectPosition/ReadAlterableLive`）；配置见上（`qol_release_mouse/qol_keypad_numpad/qol_private_hotkeys/qol_door_sfx`，默认全 1）。
- **失焦解锁鼠标**：hook `user32!ClipCursor/SetCursorPos/ShowCursor`；`QolShouldRelease = qolReleaseMouse && (guiVisible || !foreground)`。释放时保存原裁剪矩形并 `ClipCursor(NULL)`、强制 `ShowCursor(TRUE)`、吞掉后续 `SetCursorPos`/隐藏光标；回到前台或关闭 GUI 恢复并 `ShowCursor(FALSE)`。日志 `qol| cursor hooks installed=3/3 …`、`qol| mouse released gui=1 foreground=1`、`qol| mouse captured again`。实测 pid 29632（Insert 开 GUI 释放、关闭恢复；Alt-Tab 到 DSH 后 `gui=0 foreground=0`）。
- **密码盘小键盘**：仅在 `scene_id==19` 且实时读 `Script Event==607`、`prompt 1..9`、`answer==0`、`chain<10`、`got you==0` 时，把 `VK_NUMPAD1..9` 转成写 `answer`；不满足条件不拦截（交给游戏）。日志 `qol| keypad numpad digit=5 prompt=5 chain=0 vk=0x65`。实测 pid 19316：`chain 0→1`、prompt 被游戏消费。NumLock 必须开。
- **Private Room 快捷键**：`scene_id==27`、`got you==0`、`hour==6` 静默（注意 12 AM 不是 6 AM）——`A`/`D`/`W` 切换左门/右门/通风口：写 door/button alterable + `* is closed` + cooldown 20/10，并用 `state::WriteAnimation(door,12|14)` 写 `RunObject.rAni.raAnimForced`（对象基址 +0x1CE）触发原版关门/开门动画，同时 `qol_door_sfx` 播放本机 `Workspace/assets/sfx/Doorslam.wav`；`S` 走**原生 trigger 链**：写 `trigger cursor` 位置到 `flip trigger` 矩形并置 `trigger ready=1`，游戏自行 Create `Active 8`、播放 #299 `flipup2`/#300 `putdown` 并完成面板动画（不再直接写 `flip it[0]`）。日志 `qol| anim force …`、`qol| private flip request …`、`winmm| PlaySoundA …`、`audio| play … handle=299/300`。实测 pid 22180（门动画 forced 12→13→14、lamp compare 截图）、pid 9908（flip 0→2 + flipup2、2→0 + putdown，截图 `p8-flip-native-9908.png`）。**仍不做合成点击**：游戏每帧把光标重置到客户区中心，SendInput/PostMessage 均不可靠；门与面板走内存/游戏触发器注入。
- 限制/风险：只覆盖帧 19/27；密码盘输入与鼠标点击同 tick 可能被游戏判错；私室注入与 `Ennard[3]==99` 判定存在竞态；鼠标 hook 未装成时仅日志 `installed=0/3` 静默失效；Doorslam 素材发布包不带，缺失时静音但门功能正常。完整文档 `../docs/QOL.md`。

## 离线测试台（不启动游戏）

```bash
"/d/Scoop/apps/git/current/bin/bash.exe" tests/build_harness.sh
bin/harness32.exe                # D3D9 全链路（锁屏时 exit 2 = SKIP）
bin/harness32.exe --wndproc-only # 只测输入分流，不需要 D3D9 设备；PASS 退出码 0
bin/harness32.exe --imgui-only   # 只构建 ImGui 状态面板+HUD（无设备），校验 UI 代码不崩；PASS 退出码 0
bin/harness32.exe --plugins      # 插件宿主：扫描 tests/fixtures/plugins 下 6 个夹具，校验加载/禁用/API 不匹配/语法错/运行时错/沙箱；PASS 退出码 0（P6 起 harness 链接 audio_hook.o，SUMMARY4 vtx=2630）
```

`--wndproc-only` 建一个普通窗口并走同一 `HookWndProc`：Insert→GUI 打开、空格/鼠标全吞、再按 Insert 关闭、关闭后空格穿回原窗口过程；之后 Post `WM_CLOSE` 验证 `closing=1` 且 GUI 关闭。日志判定行 `SUMMARY2 pass=1`、`SUMMARY2-ext closing=1`。这条不受锁屏影响，16:04/16:16 已 PASS。

`--imgui-only` 只初始化 ImGui 上下文 + win32 后端（`overlay::InitUiOnlyForTest`，不建 D3D9 设备），连续构建数帧“状态面板+HUD”并检查 `ImGui::GetDrawData()->TotalVtxCount>0`；判定行 `SUMMARY3 pass=1`。它用于锁屏期间验证 ImGui 代码路径（Begin/End、表格、断言），16:20 PASS（gui_hud_vtx=2478、hud_only_vtx=200）。

默认模式在自身进程里创建 1280×720 的 D3D9 窗口化设备，走真实 Hook 链（Direct3DCreate9→CreateDevice→Present/Reset）并模拟 Insert/空格/鼠标左键。输出 `../logs/framework/harness-<ts>.log`，判定行 `SUMMARY pass=...`。

退出码：`0` PASS、`1` FAIL（管线问题）、`2` SKIP（当前桌面拿不到 D3D9 设备，通常是 Windows 会话被锁屏，见下）。

## Phase 3 验收结果（2026-10-25 20:41）

一键复核（带 keep-awake，用户已授权任务进行时阻止锁屏）：

```powershell
pwsh -File Workspace/tools/p3_final_check.ps1   # 离线 harness → D3D9 harness → 真实游戏 40 s + 截图；0=PASS/1=FAIL/2=锁屏
pwsh -File Workspace/tools/p3_hud_check.ps1     # 临时 hud=1，GUI 关闭后仍绘制 HUD
pwsh -File Workspace/tools/p3_repeat_exit_check.ps1 -Runs 5 -Seconds 20
```

- 真实设备：`d3d9| device vtable hooked present=6A0B02A0 reset=6A0B06A0`；Present 2319、FPS 60.0、`draw_vtx=1984`；`p3-final-gui.png`（面板叠加画面）、`p3-cjk-gui.png`（中文正常）。
- HUD：`p3_hud_check.ps1` `RESULT PASS`（GUI 关闭时 `draw| … gui=0 hud=1 vtx=200`），`logs/probe/p3-hud-live.png` 底部左侧可读到 `SLModFramework 0.2.0-p3 | Insert=设置 | PAMU=…`。
- Reset：`d3d9| Reset ok backbuffer=1258x664 → 2560x1440`，恢复后继续绘制。
- 脚本注意：不要对游戏窗口 `ShowWindow(SW_RESTORE)`（会把后台缓冲重设为 1280x720）；`FindWindow` 对本游戏失效，脚本统一用 `EnumWindows`+pid。

## 已知缺陷 / 环境限制

- **锁屏会话不能创建 D3D9 设备**：锁屏（安全桌面）下 `IDirect3D9::CreateDevice` 对 HAL 和 REF 都返回 `0x8876086A (D3DERR_NOTAVAILABLE)`，实测游戏本身也一样（`mmf2d3d9.dll` 仍会加载并调用 `Direct3DCreate9`，只是建不出设备，窗口保持黑屏）。这是环境限制，不是框架/Hook 问题；解锁后重测即可。见 `../docs/OVERLAY_INPUT.md`。
- 退出时游戏可能以 `0xC0000005` 结束（WM_CLOSE 触发，原版无框架同样如此，见 `../docs/LOADING_AND_LIFECYCLE.md`），框架不依赖退出回调。
- **PAMU 扫描引起的启动挂起（16:28 已修复）**：旧 `ScanPAMU` 用裸指针遍历全部已提交内存，游戏启动期页面被并发释放/重保护时 worker 线程会触发未处理 AV；锁屏下 WER 会挂起整个进程，表现为"注入成功但游戏永不建窗、loader 等不到 init 标记"。现在只扫 MEM_PRIVATE 并用 `ReadProcessMemory` 分块读取（不可读返回 FALSE），恢复后连续 8 次运行正常；日志新增 `pamu| scan ok/done regions=… bytes=… read_fail=…`。详见 `../docs/LOADING_AND_LIFECYCLE.md` §3.2。
- **攻击提示窗口极短**：帧 27 `Ennard[3]==99` 到切 death 实测约 0.77 s；帧 9 写 `Freddy=6` 到切 death 实测约 0.75–1.0 s（含 state 轮询）。截图/观察都可能错过；这是游戏机制而非绘制问题。真正有反应价值的是帧 27 的 4/6/10 到门高警、帧 9 的 `Freddy>=4` 高警。
- **`on_draw_overlay` 只在 D3D9 Present 内绘制**：D3D9 设备未建立（锁屏）时不存在画面，预警自然也不可见；与 Phase 3 的环境限制一致。
- **QoL 私室快捷键跳过门动画**：直接写 door/button alterable 与 `* is closed`，门状态瞬间变化、无开关门动画；指示灯由游戏逻辑同步（pid 29536 实测红→绿）。只改状态，不改计时/AI/胜负；自然游玩复核仍未做。
- **QoL 密码盘小键盘依赖前置状态**：仅帧 19、`Script Event==607`、`prompt 1..9`、`answer==0`、`chain<10`、`got you==0` 且 NumLock 开时才接管；不满足时按键交给游戏（可能判错或无效）。
- **QoL 鼠标 hook 未装成会静默失效**：若 `user32!ClipCursor/SetCursorPos/ShowCursor` 被 AV/其他 overlay 占用，`qol| cursor hooks installed` 会小于 3/3，此时失焦仍锁鼠标；失败只记日志不崩溃，可用 `qol_release_mouse=0` 关闭。

## 日志

`<game>\SLModFramework\logs\framework\`：`loader-*.log`、`slmodf-*-<pid>.log`、`init-<pid>.done`、`launch-<pid>.json`、`harness-*.log`（开发期 `Workspace/logs/framework` 仍是离线 harness/构建日志位置；`Workspace/logs/runtime` 是指向安装目录 logs 的 junction）。

关键行：`minhook| enable Direct3DCreate9 hook status=…`、`d3d9| Direct3DCreate9 …`、`d3d9| IDirect3D9::CreateDevice hooked …`、`d3d9| CreateDevice … hr=…`、`d3d9| device vtable hooked …`、`alive| … presents=… toggles=… swallow_k=…`。
PAMU/扫描相关：`pamu| scan ok regions=… bytes=… read_fail=… elapsed_ms=…`、`pamu| attempts=…`（旧日志里可能还有 `pamu| cand addr=006B378C …` 假候选行）。

绘制遥测：`draw| frame=… vtx=… cmds=… gui=… hud=… presents=…`（`overlay::RenderFrame` 前 3 帧 + 每 600 帧 + vtx 0→>0 上升沿；`vtx>0` 表示 ImGui 几何已提交 D3D9；`--imgui-only` 用 `overlay::RenderUiOnlyForTest` 返回顶点数）。
热键自动重复：`gui| ignore hotkey auto-repeat count=N`（19:20 加入；按住热键只会切换一次，重复 keydown 被吞掉；`--wndproc-only` 断言 `hotkey_repeat=1/1/0`）。

插件宿主：`plugin| scan root=…`、`plugin| manifest …`、`plugin| loaded …`、`plugin| summary …`、`plugin| set enabled=…`、`plugin| disabled by manifest …`、`plugin| ERROR id=…`；完整列表见 `../docs/PLUGIN_API_V0.md` §8。

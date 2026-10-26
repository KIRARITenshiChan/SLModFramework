# OVERLAY_INPUT — Phase 3 Overlay 与输入处理

状态：2026-10-25 19:20。**已离线验证输入分流与 WM_CLOSE 生命周期（`--wndproc-only` PASS，含 `closing=1`）、真实游戏内默认 Insert 与可配置 F2 开关、输入吞噬、关闭生命周期；Present→ImGui 绘制仍待解锁会话实机验证**（锁屏时 Windows 不允许创建 D3D9 设备，见第 5 节）。启动偶发挂起已定位为旧 PAMU 扫描裸解引用 AV，16:28 改为私有区 + ReadProcessMemory 后连续 8 次运行正常（见 `docs/LOADING_AND_LIFECYCLE.md` §3.2）。

## 1. 目标

在游戏进程内绘制框架状态面板，用热键开关；GUI 打开时拦截游戏输入，避免"在面板上点鼠标"被 Fusion 当成游戏交互；GUI 关闭时 overlay 不再吃输入（HUD 可选）。

## 2. 实现结构（`framework/native/`）

| 文件 | 职责 |
| --- | --- |
| `slmodf.cpp` | DLL 入口（DllMain 只建线程）；worker：日志/指纹门禁 → **提前** `d3d9hook::InstallEarly()` → 等 mmfs2 → 模块快照 → PAMU 校验 → init 标记 → 5 s 心跳（每拍重扫窗口子类化） |
| `d3d9hook.cpp` | MinHook：`d3d9!Direct3DCreate9` → `IDirect3D9::CreateDevice` → `IDirect3DDevice9::Reset`(#16)/`Present`(#17)；窗口枚举/子类化；WndProc 输入分流 |
| `overlay.cpp` | ImGui 1.91.9b + win32/dx9 后端；`Init`/`RenderFrame`/`Invalidate`/`CreateObjects`/`Shutdown`；状态面板 + 可选 HUD |
| `util.cpp` | `Logf`、`LoadConfig`（`hook_d3d9`/`wait_mmfs2_ms`）、`ToggleGui`（原子计数 + `guiVisible` 翻转） |

关键顺序：**先 Enable 再等游戏调用**。MinHook 的 `MH_CreateHook`/`MH_CreateHookApi` 只创建不生效，必须 `MH_EnableHook`。P3 第一版漏了这一步，导致 `Direct3DCreate9` 断点从未触发、`presents=0`（2026-10-25 16:00 修复，见 4.2 证据）。

## 3. 窗口与输入模型（实测）

| 项 | 实测值 | 来源 |
| --- | --- | --- |
| 顶层窗口 | `Mf2MainClassTh`，title `Sister Location`，style `0x17020000`（无标题栏/边框，含 WS_MAXIMIZE） | P2a + P3 日志 |
| 渲染/输入子窗口 | `Mf2EditClassTh`，client=2560×1440，visible=1；**比顶层窗口晚约 5 s 才创建**（本机锁屏环境下 16:01:00.4 出现） | 16:00:54 运行日志 |
| 进程 DPI | Per-Monitor（DPI_AWARENESS=2），150% → 144 DPI | P2a |
| 全屏方式 | 无边框窗口（非独占全屏），因此 Present 参数里 `Windowed=1` 预期 | 推断，待解锁运行确认 |

- **注入测试消息时只发一个窗口**：真实键鼠消息只投递给有焦点的窗口（实际是 `Mf2EditClassTh` 子窗口）。若把同一条消息同时 Post 给顶层+子窗口，两个窗口都被子类化，热键会被处理两次（16:08 实测一按 Insert 出现 `visible=1 → visible=0` 两个 toggle）。launcher 已改为 `FindInputWindow()`：优先子窗口，找不到才退回顶层。
- 键盘消息只发给有焦点的窗口；Fusion 的按键处理在窗口过程里，所以子类化必须覆盖 **顶层 + 所有子窗口**。实现里 `SubclassGameWindows()` 在 Hook 后与每次心跳重扫（上限 16 个），新增子窗口会被补挂（日志 `wndproc| subclass pass #N added=…`）。
- 输入分流（`HookWndProc`）：
  1. 先喂 `ImGui_ImplWin32_WndProcHandler`（面板里的鼠标/键盘交互）；
  2. `WM_KEYDOWN` 且 `wParam == hotkey` → `ToggleGui()` 并 `return 0`（游戏收不到 Insert）；
  3. `WM_SYSKEYDOWN` 且 F4/Tab/Esc → 放行（Alt+F4 能正常关游戏）；
  4. `guiVisible` 时白名单消息放行，其余键盘/鼠标消息计数 `swallowedKeys/swallowedMouse` 并 `return 0`；
  5. 其余情况调原窗口过程。
- 白名单：`WM_CLOSE/QUIT/DESTROY/ACTIVATE/ACTIVATEAPP/KILLFOCUS/SETFOCUS/SIZE/MOVE/DPICHANGED/PAINT/NCPAINT/SETCURSOR/ERASEBKGND/SYSCOMMAND/GETMINMAXINFO/ENTERSIZEMOVE/EXITSIZEMOVE/SYSKEYUP/WINDOWPOSCHANGED` 等生命周期消息，防止 GUI 打开时窗口无法最小化/关闭/响应 DPI 变化。

## 4. 已验证 / 未验证

### 4.1 离线测试台（`framework/tests/d3d9_harness.cpp`）

自建窗口 + D3D9 设备，走同一套 Hook/ImGui 代码，200 帧里模拟 Insert×2、空格、鼠标左键、第 100 帧 `Reset`。
- 已跑通到 `CreateDevice`：**16:00 锁屏下 D3D9 建不出设备（HAL/REF 都 `0x8876086A`），harness 以退出码 2 = SKIP 结束**（日志 `harness-20261025-160044.log`）。
- 解锁后重跑应得到 `SUMMARY pass=1 ... presents≥150 toggles=2 swallowed_keys≥1 swallowed_mouse≥1 ready=1 device_hooked=1 reset_ok=1`。
- **已新增不需要 D3D9 的 `--imgui-only` 模式**（锁屏也可跑，16:20 PASS）：建普通窗口 → `overlay::InitUiOnlyForTest`（ImGui 上下文 + win32 后端 + 字体图集，无 D3D9 设备）→ 连续 3 帧构建“状态面板 + HUD”的完整 UI、再 2 帧只构建 HUD → 校验 `ImGui::GetDrawData()->TotalVtxCount>0`。实测 `gui_hud_vtx=2478`、`hud_only_vtx=200`、`SUMMARY3 pass=1`（日志 `harness-20261025-162004.log`）。它证明 Begin/End 平衡、CollapsingHeader/Checkbox/Button、HUD 前景绘制列表等 ImGui 代码在无设备路径下不会断言/崩溃；**但不等价于“画到屏幕上”**，上屏仍需 D3D9 设备。
- **已新增不需要 D3D9 的 `--wndproc-only` 模式**（锁屏也可跑，16:04/16:16 PASS）：建普通窗口 → 子类化 → Post Insert（期望 toggles=1/gui=1、原窗口过程收到 0 条）→ Post 空格+鼠标（期望全部被吞、原窗口过程仍 0 条）→ 再 Post Insert 关闭（toggles=2/gui=0）→ Post 空格（期望穿到原窗口过程）→ **Post `WM_CLOSE`（期望 `closing=1` 且 `guiVisible=0`）**。日志 `SUMMARY2 pass=1`、`SUMMARY2-ext closing=1 gui_after_close=0`，退出码 0。说明输入分流与关闭生命周期逻辑本身正确，与渲染无关。

### 4.2 真实游戏（`slmodf-20261025-160054-3448.log`，16:00:54–16:01:20）

- ✅ `minhook| enable Direct3DCreate9 hook status=MH_OK` → `d3d9| Direct3DCreate9 sdk=32 -> …`（游戏确实调用，且被我们的 detour 捕获）
- ✅ `d3d9| IDirect3D9::CreateDevice hooked vt[16]=…` → `d3d9| CreateDevice adapter=0 type=1 hr=0x8876086A`（调用被捕获；失败原因是锁屏）
- ✅ 窗口子类化：顶层 `Mf2MainClassTh`（pass #1），子窗口 `Mf2EditClassTh`（pass #2，晚到被补挂）
- ⏳ 未验证：`device vtable hooked`、`Present` 计数、ImGui 是否上屏、Reset 恢复、Alt-Tab/分辨率变化（都需要真实 D3D9 设备）
- ✅ **真实游戏内输入链路已验证**（`slmodf-20261025-160919-3416.log`，16:09:19–16:09:50，锁屏状态）：
  - 16:09:32 launcher 向子窗口 Post `WM_KEYDOWN VK_INSERT` → 框架日志 `gui| toggled by hotkey-wndproc -> visible=1 toggles=1`，**一次按键只 toggle 一次**；15 s 心跳显示 `gui=1`（GUI 保持打开 10 s）。
  - 16:09:36 `--test-input` 向子窗口 Post 空格/左键/移动 → 框架日志 `swallow key msg=0x0100/0x0101/0x0102 count=1..3`、`swallow mouse msg=0x0201/0x0202/0x0200 count=1..3`，**游戏窗口过程没有收到这些消息**（离线测试台同逻辑验证过"吞噬即不派发"）。
  - 16:09:42 再 Post Insert → `visible=0 toggles=2`；心跳 `swallow_k=3 swallow_m=3` 保持。（`presents=0` 仍是锁屏导致，见第 5 节）
- ✅ **新 DLL 实机回归**（`slmodf-20261025-161731-6604.log`，16:17:31–16:18:01，锁屏状态，构建含 WM_CLOSE 生命周期）：`--test-exit 30 --toggle-gui 12,22 --test-input 16` 全程正常 → 16:17:43 `visible=1 toggles=1`；16:17:47 `swallow_k=3 swallow_m=3`；16:17:53 `visible=0 toggles=2`；16:18:01 `lifecycle| close requested msg=0x0010 hwnd=0x0047082C; rendering stopped, state already persisted`；loader 退出码 0。证明关闭钩子只在关闭时触发一次、不影响开/关 GUI 与输入吞噬。
- ✅ **可配置热键实测**（`slmodf-20261025-163056-18820.log`，16:30:56–16:31:31，锁屏状态）：`Workspace/config/framework.ini` 写 `hotkey=f2` → 框架日志 `config| …\config\framework.ini hotkey=VK 0x71 hud=1 hook_d3d9=1 wait_mmfs2_ms=30000`；launcher 用 `--toggle-gui 12,22 --toggle-key 0x71 --test-input 16`（launcher 日志 `sched: posting toggle vk=0x71`）→ 16:31:08 `gui| toggled by hotkey-wndproc -> visible=1 toggles=1`；16:31:12 `swallow_k=3 swallow_m=3`；16:31:18 再 toggle → `visible=0 toggles=2`。默认 Insert 已由 16:09/16:17 两次运行验证 → P3 验收项"默认 Insert + 可配置"证据齐全。
- ✅ **PAMU 扫描加固回归**（16:28:56–16:36:32 两批共 8 次 `--test-exit 20/25`，其中 16:30:56 带热键与输入自动化、16:49:54 为 draw 遥测重建后的第 9 次）：全部 `pamu| scan ok regions=13–16 bytes≈0.31–0.50 MB read_fail=0 elapsed_ms=0–16` → `gate=ok` → init marker → 游戏窗口正常创建；loader rc=0，游戏退出码为原生 0xC0000005 或 0x0（均属原版退出行为，见 §6）。16:06/16:24/16:26 的启动挂起未再出现。
- ✅ **重复启退与泄漏检查**（16:53，`tools/p3_repeat_exit_check.ps1 -Runs 5 -Seconds 20`，锁屏下可跑）：5 次 `launcher32.exe --test-exit 20` 全部 `rc=0`，每次退出后 `SisterLocation.exe` / `launcher32.exe` 残留均为 0；负例 `--exe C:\Windows\System32\notepad.exe` → `rc=3` 且不创建游戏进程（指纹门禁仍有效）。结果 `logs/probe/p3-repeat-exit.txt`、每次 stdout `repeat-run{1..5}.out/.err`。附带发现：Fusion 每次启动会在 `%TEMP%` 新建一个 `mrt*.tmp` 解包目录且原版不清理（本轮结束时累计 39 个 / 83 MB，已确认无进程占用后清理；P8 需写入该原生行为及清理命令）。
- 🆕 **draw 遥测**（16:49 加入，17:13 加固）：`overlay::RenderFrame()` 每帧记录 `draw| frame=… vtx=… cmds=… gui=… hud=… presents=…`（前 3 帧 + 每 600 帧 + **vtx 由 0→>0 的上升沿**，避免 GUI 在两次周期采样之间开/关时没有 vtx>0 证据），`tools/p3_final_check.ps1` 把 `draw_vtx>0` 纳入 PASS 判据并改为在所有 `draw|` 行里取**最大 vtx**（最后一行可能采于 GUI 关闭后、vtx=0 → 原先会误判 FAIL）。用途：客观证明 ImGui 几何确实提交到 D3D9 设备（上屏仍需解锁）。
- 🆕 **热键自动重复防护**（19:20 加入，离线 `--wndproc-only` 验证）：`HookWndProc` 原先对每个 `WM_KEYDOWN` 都 toggle，真实键盘按住热键时自动重复会让 GUI 快速开关闪烁；现检查 `lParam & 0x40000000`（重复标志）→ 重复事件只吞掉、不切换，日志 `gui| ignore hotkey auto-repeat count=N`。harness 新增步骤 1b：Post `WM_KEYDOWN VK_INSERT lParam=0x40000001` 后断言 toggles 仍=1、gui 仍=1、`swallowed_keys` 不变；`SUMMARY2 pass=1 … hotkey_repeat=1/1/0`（日志 `logs/framework/harness-20261025-192019.log`）。重建后 `--imgui-only` 亦 PASS（`harness-20261025-192028.log`），产物 DLL 1,153,024 B / launcher 31,744 B / harness 1,105,408 B。
- ✅ **GUI 上屏 + Present/Reset（解锁后首次实测，20:17 与 20:37）**：会话 20:17 解锁后 watcher 自动跑完 `p3_final_check.ps1`，得到 `PHASE3 VERIFY: PASS`（`Workspace/logs/probe/p3-auto-unlock-result.txt`）：launcher exit=0、`toggles=2`、`swallow_key=3`、`swallow_mouse=5`、`device_hooked=1`、`lifecycle_close=1`、`draw_frames=1800`、`draw_vtx=1984`。框架日志 `slmodf-20261025-201756-3296.log` 关键行：20:17:58 `d3d9| device vtable hooked present=6A0B02A0 reset=6A0B06A0 origPresent=003D0F80`（说明真实 D3D9 设备创建成功、vtable Hook 生效）；20:18:09 `gui| toggled … visible=1`；20:18:17 `swallow_k=3 swallow_m=5`；20:18:19 `visible=0 toggles=2`；20:18:37 `lifecycle| close requested msg=0x0010`；心跳 `presents=2319 fps=60.0 device=04715BC0`。截图 `Workspace/logs/probe/p3-final-gui.png`（735,988 B：状态面板真实叠加在标题画面上，含 SHA-256、mmfs2 base、PAMU、D3D9 设备、Present 745、FPS 60.0、DPI 1.500）与 `p3-final-gui-closed.png`（484,485 B：关闭后画面干净、无 overlay）。**上屏、Present、Reset 恢复三项自此不再是未验证项**。
- ✅ **中文字体（CJK）与 DPI 面板（20:30）**：首版 GUI 所有中文显示为 `????`——ImGui 默认 ProggyClean 无 CJK 字形。`overlay.cpp` 新增 `LoadCjkFont()`：按候选列表 `C:\Windows\Fonts\msyh.ttc`(FontNo=0) → msyhbd.ttc → simhei.ttf → simsun.ttc → Deng.ttf → msjh.ttc 找到第一个可用字体，`AddFontFromFileTTF(..., 14.0f, ..., GetGlyphRangesChineseSimplifiedCommon())`；失败回退 `AddFontDefault()` 并记日志。实测 `overlay| CJK font loaded: C:\Windows\Fonts\msyh.ttc size=14.0`、`overlay| ImGui 1.91.9b init ok … dpi_scale=1.500 fonts=1 cjk=1`；截图中“状态面板/游戏与门禁/渲染与设备”等中文正常（`Workspace/logs/probe/p3-cjk-gui.png`，window_capture 2560x1440）。`DrawMainWindow` 同时按 DPI 缩放面板：`s = (g_dpiScale>1.01) ? g_dpiScale : 1.0` → `SetNextWindowSize(600*s, 520*s)`、`SetNextWindowPos(40*s,40*s)`，150% 缩放下不挤字。
- ✅ **HUD 独立层（GUI 关闭仍绘制，20:38 脚本 PASS + 20:40 目视确认）**：`tools/p3_hud_check.ps1` 临时写 `hud=1`、`--toggle-gui 8,16` → `RESULT PASS`（`Workspace/logs/probe/p3-hud-result.txt`），日志 `slmodf-20261025-203849-9908.log`：`draw| frame=1 vtx=200 cmds=1 gui=0 hud=1 presents=1`（HUD 从第一帧起就在画）、frame=1200/1800 `vtx=200 gui=0 hud=1`，`presents=1737`。为排除“只提交几何、没上屏”的疑问，20:40:45 另跑一局（pid 26080，`--test-exit 45 --toggle-gui 25`，config `hud=1`），在 GUI 关闭期间用 `window_capture` 抓 `Workspace/logs/probe/p3-hud-live.png`（2560x1440，nonBlack 0.2781）：底部左侧真实出现深色条带 + 蓝竖条 + 文本 `SLModFramework 0.2.0-p3 | Insert=设置 | PAMU=0x016B08C2`，中文亦正常。同局日志 frame=1800/2400 `vtx=3178 cmds=2 gui=1 hud=1`（GUI 与 HUD 同时绘制）。
- ✅ **验收脚本在真实桌面上暴露并修掉的三处问题（20:32–20:39）**：① `FindWindowW('Mf2MainClassTh')`/`FindWindowA`/`FindWindowExW` 对本游戏一律返回 0（`GetWindowTextW` 只返回 `"S"`，系统上 ANSI Fusion 窗口的窗口文本/查找行为异常），改为 C# P/Invoke `EnumWindows` + `GetWindowThreadProcessId` + `GetClassNameW` 按 pid+类名前缀 `Mf2` 查找；② 脚本原来等“最新日志出现 visible=1”，会立刻匹配到**上一次运行**的旧日志并在游戏尚未启动时截图（实测 game pid=0、hwnd=0），改为记录 `$launchTime` 后只认 `LastWriteTime -ge $launchTime` 的日志；③ `ShowWindow(SW_RESTORE)` + 置前会让 Fusion 把后台缓冲从 2560x1440 重设成 1280x720（20:37:33 `d3d9| Reset ok backbuffer=1280x720 windowed=1`），已移除 SW_RESTORE，只 `SetForegroundWindow` + 等 1200 ms，截图后恢复原前台窗口。另外两个脚本都在开头调用 `SetThreadExecutionState(0x80000003)`（用户已授权任务进行时阻止锁屏）。
- ✅ **配置鲁棒性**（16:56 发现、16:58 修复并复测，锁屏下）：故意写非法 `Workspace/config/framework.ini`（`hotkey=bogus`、`hud=7`、`hook_d3d9=0`、`wait_mmfs2_ms=999999`、`unknown_key=1`）→ 修复前日志得到 `hotkey=VK 0x00 hud=1`（bogus 被 `atoi` 归 0、hud 被 `?1:0` 放宽）；修复后 `config| warn invalid hotkey 'bogus', keeping default VK 0x2D`、`warn hud='7' is not 0/1, keeping 0`、最终 `hotkey=VK 0x2D hud=0 hook_d3d9=0 wait_mmfs2_ms=60000`（`hook_d3d9=0` 生效：`d3d9| hook disabled by config hook_d3d9=0`；`wait_mmfs2_ms` 已 clamp 120000→60000）。删除 ini 后默认路径复测：`config| not found …, using defaults: hotkey=Insert hud=0`、`d3d9| early install result=1`、HAL 设备调用仍被捕获。另有 `hotkey=0x71`（十六进制 F2）端到端实测通过：`config| … hotkey=VK 0x71` → 16:59:12 `visible=1 toggles=1` → 空格/鼠标被吞（`swallow_k=3 swallow_m=3`）→ 16:59:20 `visible=0 toggles=2`。证据：`slmodf-20261025-165625-2812.log`（修复前）、`-165801-4708.log`（修复后非法 ini）、`-165826-24776.log`（默认）、`-165859-25428.log`（hex 热键）。
- ✅ **未验证版本门禁负例**（17:09–17:10，锁屏下）：复制 EXE 并追加 1 字节（SHA-256 `93df18d4…3134341`、大小 950,556,729 B）+ `launcher32.exe --exe <副本> --no-verify --test-exit 15` → loader `FINGERPRINT MISMATCH` 后继续；框架 `loader_fp| … verified=0`、`gate| fingerprint_ok=0`、`gate| fail: exe fingerprint not the verified build`、init 标记 `status=fail detail=fingerprint_mismatch`；loader 打印 `framework initialized: FAIL status reported` 并以 **rc=2** 退出；游戏从副本仍可启动/退出（原生 0xC0000005）。附带修复：`--exe`/`--dll` 正斜杠路径曾触发 CreateProcess `err=267`（工作目录拆分只认反斜杠），已加 `NormalizeSlashes()`；`ReadSidecarFingerprint` 曾把不匹配副本误报 `verified=1`，已改为读取 sidecar `verified=`。证据 `logs/probe/fp-negative2.out`、`fp-negative-framework.log`（修复前）、`fp-negative2-framework.log`（修复后）。
- ✅ **安全模式 `hook_d3d9=0`**（17:14:46，pid 26828，`slmodf-20261025-171446-26828.log`）：`d3d9| hook disabled by config hook_d3d9=0`、`gate| version_ok build=286`、init 标记 `status=ok detail=build286+pamu hooks=0`、`lifecycle| close requested` 正常；游戏照常启退（launcher rc=0）。证明关闭渲染 Hook 后框架仍只读运行，可用作兼容性降级/对照实验。
- ✅ **mmfs2 等待窗口实测**（17:15:08，pid 9392，`wait_mmfs2_ms=1`）：`d3d9| early hook installed` 后 `mmfs2| base=67B00000 waited_ms=0` → 说明游戏在我们的 worker 到达等待点之前就已加载 mmfs2（`InstallEarly` 里 `LoadLibrary(d3d9.dll)`+MinHook 初始化本身约 80–120 ms，给了 Fusion stub 足够时间）；P2 记录的 125–235 ms 是从进程创建算起，两者不矛盾。因此无法用极小 `wait_mmfs2_ms` 制造超时负例，降级路径的强制测试留待 P8（可用环境变量或测试构建注入延迟）。该轮 `hooks=1`、init ok、正常启退。
- 结论：热键开关、输入分流、关闭生命周期、**GUI 上屏/Present/Reset 恢复、HUD 独立层、中文字体与 DPI 缩放**均已在真实游戏内验证；剩余未验证项仅“全屏/窗口化切换、Alt-Tab、运行时分辨率变化”（见 §7，需手动或脚本切换窗口后观察 Reset 日志）。

## 5. 锁屏环境诊断（重要）

- **现象**：注入后游戏进程存活、窗口存在但全黑；`presents=0`、`device=0`；`CreateDevice` 返回 `0x8876086A (D3DERR_NOTAVAILABLE)`，HAL 与 REF 都失败。
- **证据**：`OpenInputDesktop()` 返回 0（安全桌面）；`GetForegroundWindow()`=0；`LogonUI.exe` 于 15:47:50 启动；同一时刻 DSH 窗口截图正常、游戏窗口纯黑；无框架直接启动同样黑屏。
- **结论**：D3D9 设备创建被锁屏阻止，与注入/Hook 无关。解锁后必须重测 4.2 的 ⏳ 项。
- **恢复后第一轮测试命令**：

```powershell
# 一键验证：先跑两个离线 harness（锁屏也能跑），再判锁屏；解锁后才继续 D3D9 harness + 真实游戏 40 s 自动化 + 15 s 截图
pwsh -File Workspace/tools/p3_final_check.ps1
# 锁屏也可跑的重复启退/泄漏检查（5 次，每次 20 s）
pwsh -File Workspace/tools/p3_repeat_exit_check.ps1 -Runs 5 -Seconds 20
# HUD 独立层检查：临时写 hud=1，8 s 开 GUI / 16 s 关，关闭后截图并验证 hud=1 gui=0 vtx>0
pwsh -File Workspace/tools/p3_hud_check.ps1
# 退出码 0=PASS / 1=FAIL / 2=锁屏跳过；日志与截图落在
#   Workspace/logs/framework/slmodf-<ts>-<pid>.log（toggles/swallow/presents/device_hooked/lifecycle）
#   Workspace/logs/probe/p3-final.out/.err、p3-final-gui.png
```

注意（20:30 已修）：离线 harness 早期按**当前工作目录**解析 `Workspace\logs\framework`，从 `bin\` 启动会把日志写进 `bin\Workspace\...`；现在改为按 `harness32.exe` 所在位置向上三级（bin→framework→Workspace）定位，任何 cwd 都能写对目录，旧日志已迁移回 `Workspace/logs/framework/`。


## 6. 已知风险

| 风险 | 现状/对策 |
| --- | --- |
| MinHook 只创建未启用 | 已修复：`InstallEarly` 创建后立即 `MH_EnableHook`（日志 `enable … status=MH_OK`） |
| 子窗口晚于 Hook 创建 | 每次心跳 `SubclassGameWindows()` 重扫；实测补挂成功 |
| 插入/卸载后原函数指针悬空 | `Shutdown` 先 `MH_DisableHook` 再 `MH_Uninitialize`；P8 再做完整卸载测试 |
| 全屏/分辨率变化 | `Reset` Hook 重建 ImGui 设备对象；`WM_SIZE/DPICHANGED` 放行；待实测 |
| FindWindow 对本游戏失效 | 实测 `FindWindowW/A`、`FindWindowExW` 对 `Mf2MainClassTh` 返回 0（`GetWindowTextW` 只得 `"S"`）；自动化脚本改用 `EnumWindows`+pid+类名前缀查找，代码里不依赖 `FindWindow` |
| `SW_RESTORE` 触发后台缓冲重设 | 20:37 实测把无边框窗口 `ShowWindow(SW_RESTORE)` 置前后，Fusion 把 2560x1440 重设为 1280x720；自动化只做 `SetForegroundWindow`，不改变窗口状态 |
| GUI/截图 DPI 虚拟化 | 非 DPI 感知的 pwsh `CopyFromScreen` 得到 1707x960（逻辑像素），不是 2560x1440；需要原始像素证据时用 `window_capture`（DWM thumbnail，实测 2560x1440 正常） |
| 输入法/IME 窗口 | 枚举会看到 `IME`/`MSCTFIME UI`（本进程），只子类化有 `Mf2*` 渲染关系的窗口；当前按"本进程可见顶层+子窗口"过滤，后续若误挂可加类名白名单 |
| 锁屏/远程桌面 | 设备创建失败 → 框架安全降级（不崩、不画），日志留证 |
| 启动偶发挂起（根因已定位并修复） | 16:06/16:24/16:26 三次：注入成功但游戏主窗口不出现、worker 日志停在 EXE 映像假候选行（`pamu\| cand addr=006B378C …`）、loader 等不到 init 标记。根因＝旧 `ScanPAMU` 裸指针遍历全部已提交内存，游戏启动期并发释放/重保护页面时读越界 → worker 线程未处理 AV（锁屏下 WER 挂起整个进程）。16:28 改为 **只扫 MEM_PRIVATE + ReadProcessMemory 读 64 KiB 块**（不可读返回 FALSE 而非崩溃）后，连续 8 次运行 `ok`、无挂起，扫描只需约 0.3–0.43 MB / <1 ms。教训：游戏内只读探测也必须假定内存随时被改写 |
| 注入消息重复触发 | 已修：launcher 只向焦点的子窗口投递，不向整棵窗口树广播 |
| WM_CLOSE 后继续绘制/回调 | 已处理：`WM_CLOSE`/`WM_QUERYENDSESSION`/`WM_ENDSESSION` → `closing=1` 并关 GUI、`Present` 跳过绘制；不注册退出回调（原版退出路径本身 0xC0000005，P2a/P2 已记录） |

## 7. P3 验收对照（截至 2026-10-25 21:20，Phase 3 已完成；Alt-Tab 由 P4 回归补验）

| 计划 P3 验收项 | 状态 | 证据 |
| --- | --- | --- |
| GUI 可稳定开关 | ✅ 已验（离线 + 真实游戏上屏） | `--wndproc-only` PASS；真实游戏 toggles=1/2；默认 Insert 与可配置 F2/0x71 两条路径；20:17/20:37 解锁后两次上屏 |
| GUI 打开时不锁死/不误触游戏输入 | ✅ 已验（逻辑 + 真实游戏） | 离线 harness 证明被吞消息不派发；真实游戏 `swallow_k=3 swallow_m=3`/5；白名单保留 Alt+F4/Tab/Esc 与窗口生命周期消息 |
| 关闭 GUI 后 overlay 不再接收鼠标点击 | ✅ 已验 | `--wndproc-only` 第 5 步空格到达原窗口过程；真实游戏关闭后截图干净（`p3-final-gui-closed.png`） |
| 后台插件仍可按配置运行 | ✅ HUD 层已验（插件宿主 P4） | `p3_hud_check.ps1` `RESULT PASS`（`hud_vtx=200`，GUI 关闭时 `hud=1 gui=0`）+ `p3-hud-live.png` 目视确认底部 HUD 文本 |
| GUI 上屏 / Present 计数 / ImGui 几何提交 | ✅ 已验（20:17 起） | `p3_final_check.ps1` `PHASE3 VERIFY: PASS`：`device_hooked=1 presents=2319 fps=60.0 draw_vtx=1984`；`p3-final-gui.png` 面板真实叠加；CJK 面板 `p3-cjk-gui.png` |
| Alt-Tab / 焦点切换后恢复 | ✅ 已验（2026-10-25 21:14，P4 回归） | GUI 打开时用 `keybd_event` 切走 3 s 再切回：presents 237→2038、fps 31.1→58.0、device 05127FC0 不变、无 DEVICELOST/新 Reset（`logs/framework/slmodf-20261025-211338-19188.log`；截图 `logs/probe/p4-final-plugins.png`） |
| 全屏/窗口化切换、手动分辨率变化后恢复 | ⚠️ 部分：Reset Hook 已验证，切换场景未逐一验证 | 20:38 实测 `Reset ok backbuffer=1258x664 → 2560x1440`（启动期两次 Reset 后正常绘制）；游戏默认无边框全屏，手动切换场景留待后续实测 |
| 退出无悬挂窗口/无明显资源泄漏 | ✅ 已验 | `p3_repeat_exit_check.ps1` 5 次启退 rc=0、无残留；`%TEMP%/mrt*.tmp` 为原版 Fusion 行为，已记录清理 |
## 8. v1.17 字体：全字库 + 自定义字体（2026-10-27 00:1x）

**问题**：默认 `GetGlyphRangesChineseSimplifiedCommon()` 只含约 2500 个常用字，`氧`（U+6C27）不在其中，帧 31 的「氧气不多了」显示为 `?氧气不多了`。

**修复**（`Workspace/framework/native/overlay.cpp`）：

- `LoadCjkFont()` 重写为 `TryLoadCjkFont(path, baseSize, io)`：`AddFontFromFileTTF` 用 `GetGlyphRangesChineseFull()`（约 2 万字），成功时打印 `overlay| CJK font loaded: <path> size=<n> glyph_oxygen=1`；先试配置 `font_path`，再试系统候选（msyh.ttc/msyhbd.ttc/simhei.ttf/simsun.ttc/Deng.ttf/msjh.ttc），全失败回退 `AddFontDefault()`。
- `FontHasGlyph(font, cp)` 用 `FindGlyphNoFallback` 探测 `氧`（0x6C27）；**必须在 `io.Fonts->Build()` 之后探测**，否则恒为 NULL。Init 路径显式 Build 后再记 `font_atlas=2048x4096 glyph_oxygen=1`；`--imgui-only` 离线路径同样 Build 并打印 `UI-only font atlas=…`。
- 新配置：`font_path=`（空=自动候选；可指向任意 TTF/TTC，FontNo 固定 0）与 `font_size=14`（10–40）。`fonts=1` 时第一字体即 CJK 字体。

**证据**：真机 pid 20584 日志 `overlay| ImGui 1.91.9b init ok … font_atlas=2048x4096 fonts=1 cjk=1 glyph_oxygen=1`；实际 overlay 截图 `Workspace/logs/probe/p9-sc31-oxygen_d350.png` 中「氧气不多了」的「氧」字正常，同屏另有低警与红边条。离线 `--imgui-only` 日志 `atlas=2048x4096 glyph_oxygen=1`。

**注意**：全字库图集从 1024x1024 级涨到 2048x4096（RGBA 约 32 MB 显存/内存；纹理上传在 D3D9 初始化时一次完成），实测 60 FPS 无可见影响；若显存紧张可 `font_path` 指到子集字体或改回常用字库代码。

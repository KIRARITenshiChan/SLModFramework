# Sister Location Mod Framework 开发计划

> 目标：为 Steam PC 版《Five Nights at Freddy's: Sister Location》制作一个类似 REFramework 的本地 Mod 框架。玩家可以在游戏运行时通过快捷键呼出 GUI，并以插件形式添加 QoL 功能。
>
> 首要使用场景：正常游玩原版游戏，不重制游戏；优先实现可视化攻击预警、音效柔化/替换，以及通用调试工具。
>
> 当前阶段：**Phase 5 / 6 / 8-QoL 完成；Phase 7 转为示例插件（v0.5.1，默认关闭）；【v1.21】Phase 8 收尾——P8.1–P8.5 全部完成、v1.0.0 冻结、发行 ZIP 与干净安装/卸载演练通过，仅剩 P8.6 GitHub 发布。** ①m12195 拍板已全部落地：安装布局 B（`<game>\SLModFramework\`，launcher/DLL 从自身路径推导框架根与游戏根，config/plugins/logs/assets 全相对框架根）、项目 MIT + 公开源码、GitHub 首发 v1.0.0、文档中文为主+英文摘要、首发包只带 HelloPlugin、GitHub 身份 KIRARITenshiChan；②字节级可复现构建（DLL `fd83f195…` / launcher `22daac7f…` / harness `248ab25b…`），离线四模式 PASS，objdump 依赖审计通过；③发行包 `release/SLModFramework-1.0.0-win-x86.zip` 805,520 B / SHA-256 `f8932e25…`（18 文件），干净安装→GUI（滚动/诊断包/配置重置）→卸载→原版对照演练通过（`docs/P8_ACCEPTANCE_DRILL.md`）；④SoftJumpscares/ThreatTelegraphs 冻结为示例插件，其完善与 Phase 9 字幕延后到发布后（插件优先，m12162）；⑤下一步仅 P8.6：GitHub 首发 v1.0.0 + tag + Release 附件，随后按插件优先主线推进。**所有运行时细节必须先实测，不能假定渲染 API、地址、对象布局或函数签名。**
>
> **文档版本 v1.21 ｜ 最近更新 2026-10-27 ｜ 历史版本备份：Workspace/plan-backups/ ｜ 变更记录：见文末。**

## 0. 项目原则

1. **先观测、后修改。** 第一版只读游戏状态、画 UI；在状态读取与版本检测可靠之前，不修改敌人 AI、计时器、存档或游戏流程。
2. **不要硬编码未经验证的地址。** 所有地址/指针/签名都要记录适用的 EXE 指纹、验证方法和失败行为。版本不匹配时应禁用相关功能，而不是继续读写内存。
3. **框架和插件分层。** 框架处理注入、日志、GUI、配置、游戏状态适配、插件生命周期；插件只使用公开 API，不直接操作底层内存或渲染 Hook。
4. **按小里程碑推进。** 每阶段都要有可运行产物、验收标准和测试记录。不要一次性生成一个大型、未经测试的 Hook 工程。
5. **保留原版与可回滚性。** 不覆盖 Steam 原始文件。需要改资源时，使用独立 Mod 目录、补丁清单和原文件备份/校验机制。
6. **资源和代码分离。** 不在公开仓库分发游戏原版的贴图、音频或其他受版权保护资源。分析输出留在本地工作目录。
7. **避免过度设计。** MVP 先支持一个游戏版本、一个 GUI、少量插件接口；暂不追求多版本、多游戏或复杂联网功能。
8. **插件优先（【v1.20】新增，用户 m12162）。** 新功能默认以插件形式实现（`plugins/<name>/plugin.ini + main.lua`，受 API v1 约束）；只有插件 API 能力不足、需要新 native Hook 或涉及安全边界时，才更新框架本体。框架更新与插件更新分开记账，避免为单个玩法改动核心二进制。

## 1. 目标与非目标

### 1.1 MVP 必须具备

- 识别并启动/附加到正版 Steam 版 `SisterLocation.exe`。
- 记录游戏 EXE 的路径、架构、文件版本、时间戳、大小和 SHA-256 等指纹。
- 可安全初始化/关闭框架；有日志、错误报告与基础配置。
- 通过可配置热键（默认建议 `Insert`）呼出/关闭游戏内 GUI。
- GUI 能显示框架状态、插件列表、插件启停与配置页面。
- 第一版游戏状态适配器只读；能可靠识别当前帧/场景或明确报告“未知”。
- 支持至少一个小型插件，以及插件异常时的隔离和错误日志。
- 有明确的禁用/卸载方式，游戏崩溃时不破坏原始安装。

### 1.2 后续目标

- `DebugInspector`：查看场景、已识别对象/变量、状态变化和最近事件。
- `SoftJumpscares`：降低或替换 jumpscare 音效，配置音量、音色/替换资源和启停状态。
- `ThreatTelegraphs`：在已验证的场景中显示攻击预警，例如 Night 2 Breaker Room、Night 5 Private Room。
- 可扩展的脚本插件 API（首选先试验 Lua）；允许插件注册 HUD 标记、文本、开关和配置项。
- 版本兼容检查、崩溃保护、配置迁移和使用文档。

### 1.3 当前明确不做

- 不把 SL 移植到 Unity。
- 不复刻或替换整个 Clickteam Fusion Runtime。
- 不在第一阶段修改游戏难度、敌人速度、计时器、关卡结果或存档。
- 不承诺每个夜晚、每个版本都能自动识别状态。
- 不开发联网、反作弊绕过或分发原版资产功能。
- 【v1.6】不把调试用场景跳转（warp）暴露进插件 API 或常规用户界面；它只在开发期 `debug=1` 下可用。
- 【v1.20】首发发行包（v1.0.0）不包含 SoftJumpscares / ThreatTelegraphs 两个示例插件（用户 m12195 同意裁剪），只带 HelloPlugin；两个插件后续作为独立示例插件更新发布。

## 2. 技术路线（需由预研结果最终确认）

### 2.1 建议架构

- **语言：** Windows 原生 C++，优先 C++20；使用 CMake 管理构建。
  - 【v1.1】构建目标必须是 **32 位 x86**（游戏为 PE32/i386，x64 DLL 无法注入）。【v1.2】工具链已安装并验证：winlibs i686-mingw-w64 **gcc 16.2.0**（D:/devtools/winlibs-i686/mingw32），可编译/链接 PE32 DLL（P2a 探针即用它构建）；MSVC 不需要。构建命令与版本见 docs/PREFLIGHT.md。
- **GUI：** Dear ImGui。渲染后端必须根据实际游戏运行环境选定，不能预先认定为 D3D9。【v1.1】已获二进制证据：运行时 DLL 为内嵌的 mmfs2.dll（Fusion 2.5 Runtime）+ mmf2d3d9.dll（仅导入 Direct3DCreate9，非 Ex）+ mmf2d3d8.dll 回退 → 高概率 D3D9；【v1.2】P2a 已实机确认 mmf2d3d9 已加载、mmf2d3d8 未加载，D3D9 后端成立。
- **Hook：** 若确有需要，可评估 MinHook 等成熟 Windows API hooking 库；Hook 类型与目标函数须基于探测结果决定。
- **脚本：** 可评估嵌入式 Lua，让插件作者写脚本而不是每个功能都编译 DLL。先实现最小 API，不要一开始暴露任意内存操作。
- **资源改动：** ~~优先探索可回滚的外置资源替换/打补丁方案；若不适合，再研究游戏资源加载或音频播放路径的运行时 Hook。~~【v1.1】路线已改：本作 950 MB 单 EXE、资源全部内嵌（pack/chunk），无外置可替换文件；改包需 EXE→MFA + 正版 Fusion 2.5 Developer，且 Steam 校验会覆盖 → **P6 采用运行时音频 Hook（DirectSound 包装）**，关闭插件即回滚。详见 Phase 6。【v1.2】P2a 实测：运行时每次启动把 8 个内嵌 DLL/扩展解包到 %TEMP%/mrt*.tmp 再加载（旧目录不清理），游戏目录不产生文件 → 安装期资源替换无可行路径，确认 P6 只能运行时 Hook。
- **游戏逻辑逆向：** 使用 `ctfak-cpp` 等工具导出 Fusion 事件、对象、帧和资源作为静态参考【v1.1：ctfak-cpp 为 AGPL-3.0，仅作只读工具，不复制代码、不随框架分发；本机已验证可解析，另有自研 Python 解码器作备选】；运行时状态读取仍须单独验证。

### 2.2 运行时 Hook 的选择顺序

不要在预研之前决定用哪种 Hook。按以下顺序调查：

1. 读取 PE 信息，确认目标 EXE 的架构、模块布局和版本指纹。
2. 确认游戏窗口、渲染模式/API，以及窗口化/全屏下是否变化；同时确认运行时 mmfs2.dll 的加载方式与基址。【v1.1】【v1.2】已实测：mmfs2/mmf2d3d9 等从 %TEMP%/mrt*.tmp 加载、模块枚举可见；基址在两次运行间会变（mmf2d3d9 0x038E0000→0x030B0000、DDRAW 0x799C0000→0x7A400000），禁止硬编码绝对地址。
3. 检查是否存在可用的外部叠加层路线；评估透明独立窗口的焦点、置顶、全屏兼容性。【v1.1：降级为 HUD-only 备选；PCGW 称本作 borderless fullscreen，但未实测；若 P2a 确认无法稳定叠加，则仅提供外部 HUD】
4. 如果需要进程内绘制，再选择与实际 API 匹配的 Present/EndScene 等 Hook 位置。【v1.1 主选：MinHook hook IDirect3DDevice9::Present（vtable #17）/ Reset（#16）；备用：外部 HUD 窗口】【v1.2 理由：mmf2d3d9.dll 只静态导入 d3d9!Direct3DCreate9，设备对象由 COM 创建，IAT patch 拿不到设备指针；备选 IAT patch mmf2d3d9!Direct3DCreate9 拦截 IDirect3D9，再在 CreateDevice 后取设备。d3d8 路径本机未加载，不投入。】

【v1.3】设备级 Hook 的安装时机：在 `mmf2d3d9.dll` 加载且 `IDirect3D9::CreateDevice` 成功后再安装 `IDirect3DDevice9` vtable #17 Present / #16 Reset；禁止在 DllMain 或 DLL 加载时直接 Hook；渲染器模块未出现时保持纯原版。
5. 确认 Hook 可恢复、可卸载，并有设备重置、窗口切换、退出流程的处理。

渲染 Hook 失败时，不要无限增加猜测性 Hook。先产出调查记录，再选择替代方案。

### 2.3 推荐模块边界

```text
SLModFramework/
├─ launcher/              # 可选：检测游戏版本、启动/附加和显示错误
├─ native/
│  ├─ bootstrap/          # 初始化、退出、线程协调
│  ├─ platform/win32/     # 进程、窗口、输入、PE 指纹
│  ├─ render/             # 仅包含经实测确认的渲染后端
│  ├─ gui/                # ImGui 主界面、HUD 层、输入焦点管理
│  ├─ logging/            # 日志、崩溃上下文和诊断信息
│  ├─ config/             # 配置读写和迁移
│  ├─ game_adapter/       # 只读游戏状态适配器
│  ├─ plugin_host/        # 插件发现、启停、生命周期、错误隔离
│  └─ audio/              # 【v1.1】运行时音频播放路径适配（DirectSound 包装）
├─ scripts/               # 框架自带的 Lua 插件（若选用 Lua）
├─ third_party/           # 【v1.1】第三方依赖与许可证信息；禁止放入 AGPL 派生代码或原版资源
├─ tools/                 # 【v1.1】自研采集/解析脚本（可提交的只读工具）
├─ plugins/
│  ├─ DebugInspector/
│  ├─ SoftJumpscares/
│  └─ ThreatTelegraphs/
├─ config/
├─ logs/
├─ docs/
└─ tests/
```

实际目录可以调整，但不要把 GUI、游戏状态逆向、具体 QoL 功能全部写进同一个 DLL 文件。

【v1.5】当前实际落盘：`Workspace/framework/{launcher,native,bin,tests}`（native 目前是扁平实现：util/overlay/d3d9hook/plugin_host/slmodf）、`Workspace/plugins/HelloPlugin`、`Workspace/config/framework.ini.example`、`Workspace/third_party/{imgui,minhook,lua-5.4.7}`。等模块继续变大再按上面的子树拆分，避免过早拆目录（§0 原则 7）。

## 3. 分阶段实施

## Phase 0 — 建立目标版本档案与可复现环境

### 任务

- [x] 【v1.1】定位 Steam 安装目录，确认启动文件为 `SisterLocation.exe`（实际文件名以本机为准）。已实测：游戏目录内仅此一个文件，无外置资源/配置。
- [x] 【v1.1】记录 EXE 的 PE 架构（x86/x64）、SHA-256、文件版本、时间戳、节区布局和文件大小。已实测：PE32 x86；950,556,728 B；SHA-256 c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73；节区 .text/.rdata/.data/.rsrc/.reloc；Fusion productVersion=3 / productBuild=286；Steam buildid=1497593。
- [x] 【v1.1】记录游戏启动参数、窗口标题、窗口类、窗口化/全屏行为和分辨率变化表现。【v1.2】已实测（P2a，见 docs/RUNTIME_PROBE.md）：无启动参数；窗口标题 Sister Location；窗口类 Mf2MainClassTh（子窗口 Mf2EditClassTh）；无边框最大化全屏（style 0x17020000，无 CAPTION/THICKFRAME）；client=屏幕 2560×1440、DPI 144（150%，Per-Monitor）；标题页显示 v1.121。分辨率变化/Alt+Tab 行为待 P3。
- [x] 【v1.1】建立独立开发目录和备份策略；确认 Steam 验证文件可以恢复干净原版。开发目录=Workspace/，备份策略见 Workspace/README.md；Steam 校验恢复待实机执行。
- [x] 【v1.1】建立一份场景观察清单，至少覆盖标题页、Night 2 Breaker Room、Night 5 Private Room、一个 jumpscare、游戏退出。（帧号已定位：Night 2=帧 9、Night 5=帧 27；标题页已实机截图；其余场景时序与攻击窗口由 P5/P7 实机采集）【v1.2】清单与证据见 analysis/ 与 docs/RUNTIME_PROBE.md。
- [x] 【v1.1】记录当前 Steam 构建的版本号；之后每次更新先重新验证指纹。Steam appid 506610 / buildid 1497593（appmanifest_506610.acf 已读）。

### 交付物

- `docs/PREFLIGHT.md`
- `docs/BUILD_FINGERPRINT.md`
- `tools/collect_game_info.py`（只读采集信息，不修改游戏）。【v1.1】实现可由 recon/scripts/pe_probe*.py 整理而来；PowerShell 版可选。
- 【v1.1】docs/BUILD_FINGERPRINT.md 必需字段：EXE SHA-256、文件大小、PE 架构、Fusion build、Steam buildid、pack/chunk 布局摘要、内嵌运行时 DLL（mmfs2/mmf2d3d9）指纹、采集脚本与采集时间。

### 验收标准

- 能重复采集相同的 EXE 指纹。
- 工具能明确报告目标路径错误或版本无法识别，而不是继续执行后续操作。
- 原版启动与退出流程保持正常。
- 【v1.1】静态可采集项已完成；窗口/全屏/启退项随 Phase 2a 验收。
- 【v1.2】P0 全部验收项完成：运行时窗口/渲染/音频/退出证据见 docs/RUNTIME_PROBE.md；EXE 与 Fusion 指纹可用 tools/verify_fingerprint.py 重复采集。

## Phase 1 — 静态逆向与场景行为调查

### 任务

- [x] 【v1.1】获取并构建/运行 `ctfak-cpp`，已确认可解析当前 PC 版 SL 的 EXE：13–14 s 解析完成，36 帧 / 5260 图 / 392 声；构建需 mingw16 并加 -DCMAKE_CXX_FLAGS=-include cstdint。ctfak-cpp 为 AGPL-3.0，仅作工具使用，不复制其代码、不链接、不随框架分发；另有自研 Python 解码器（pack + chunk + 解密）独立解析成功，双路线互证。
- [x] 将解析输出放在本地 analysis/，不要提交游戏资产或完整提取结果到公开仓库。【v1.2】已完成：转储在 recon/dumps/ 与 analysis/data/，均仅本地。
- [x] 导出并检查帧、对象、事件程序、图像/音频资源清单。【v1.2】已完成：36 帧 / 788 对象 / 392 声音 / 5260 图像。
- [x] 【v1.1】找到 Night 2 Breaker Room、Night 5 Private Room 与 jumpscare 相关帧/对象/事件。已确认：Night 2 Breaker Room=帧 9；Night 5 Private Room=帧 27「Final Encounter」（游戏中不存在名为 Private Room 的帧；Custom Night 复用帧 31「custom level」）；jumpscare 音频索引 86/112/120/148/257/265/303，共 20 处播放点（帧 9→#120；帧 27→#303；帧 8/11/19→#86；帧 13/16/31→#112；帧 10→#148；帧 26→#257/#265），映射见 analysis/jumpscare-audio.md。
- [x] 将每个场景记录成“进入条件 → 状态变化 → 威胁判定 → 玩家应对 → 失败条件”的状态表。【v1.2】两目标场景已完成：Breaker Room（G28–G34/G58–G61/G63/G64/G73–G84）与 Final Encounter（G35/G108–G110/G153–G175/G196–G202/G206/G213/G234–G236），见 analysis/night2-breaker-room.md、analysis/night5-private-room.md；其余场景转 P5/P7。
- [x] 明确“即将攻击”的语义：是否存在明确的预攻击状态/计时器，还是只能根据一组条件推断。【v1.2】静态结论：两场景均有可推断的预攻击/死亡前置状态（帧 9：Freddy 计数 + got you；帧 27：final stage/hour/got you/side），但非引擎显式“预攻击”标志；实机值域与时序由 P5/P7 验证。
- [x] 用实际游戏游玩记录校验静态事件分析；标出静态工具尚未正确解析的内容。（【v1.1】静态部分已完成；【v1.2】实机校验由 P5/P7 完成）

### 交付物

- `analysis/frame-index.md`
- `analysis/night2-breaker-room.md`
- `analysis/night5-private-room.md`
- `analysis/jumpscare-audio.md`
- 必要的本地 JSON/事件转储（默认不进版本库）
- 【v1.1】analysis/event-rules.md：事件组级规则表（条件/动作/来源文件行号/证据等级；可并入 night2/night5 文件）

### 验收标准

- 至少能清楚描述一个预警场景的触发条件与结束条件。
- 每个判断都标记证据来源：事件转储、运行时观察或尚待验证假设。
- 若 `ctfak-cpp` 无法完整解析当前 EXE，记录失败位置并评估备选工具，不要将不完整输出当成真相。
- 【v1.1】关键结论必须标注证据等级：实测 / 推断 / 待实机。
- 【v1.1】jumpscare 音频名必须与 banks.json 的 sound index 对齐（dump 中声音名不含 index，易错配）。
- 【v1.1】至少一条完整死亡链证据可复现（如帧 9：Freddy≥6 → got you=1 → 创建 jumpscare + 播放 #120 → 跳帧 15 game over）。
- 【v1.2】六份交付文档已产出：docs/PREFLIGHT.md、docs/BUILD_FINGERPRINT.md、analysis/frame-index.md、analysis/night2-breaker-room.md、analysis/night5-private-room.md、analysis/jumpscare-audio.md。

## Phase 2 — 注入/启动机制与基础设施验证（【v1.3】已完成 2026-10-25）

### Phase 2a — 只读注入探针（【v1.1】新增，先于最小 DLL）【v1.2】已完成（2026-10-25）

> 【v1.2】执行结果（完整记录见 docs/RUNTIME_PROBE.md，日志 logs/probe/）：
> - 注入：CreateProcessW(CREATE_SUSPENDED) + 远程 LoadLibraryW，两次运行均成功（HMODULE 0x67E10000）；游戏进标题页，无需 Steam。
> - 模块：mmfs2.dll base 0x67D90000 / size 0x78000；mmf2d3d9.dll 已加载（两次基址不同 0x038E0000→0x030B0000）；mmf2d3d8.dll 未加载；d3d9/DSOUND/WINMM/DDRAW/nvd3dum 已加载；无 Steam/overlay 模块；加载 apphelp/AcLayers 兼容层。
> - 窗口：标题 Sister Location、类 Mf2MainClassTh、子窗口 Mf2EditClassTh、style 0x17020000（无边框最大化）、client=屏幕 2560×1440、DPI 144/Per-Monitor。
> - 渲染：D3D9 确认；mmf2d3d9 仅导入 d3d9!Direct3DCreate9；Present/Reset 的 D3DPRESENT_PARAMETERS 未记录（转 P3）。
> - 音频：DSOUND 已加载，mmfs2 按序号 1 导入 DirectSoundCreate；同时导入 WINMM mciSendCommandA/W → 实际播放路径未证实（转 P6 前置探针）。
> - 资源：启动时把 pack 内 8 个文件解压到 %TEMP%/mrt*.tmp 再加载，哈希稳定，旧目录不清理；游戏目录不产生文件。
> - 内存：PAMU 头可读（runtimeVersion=0x302、productBuild=286），地址随机（0x016257D8 / 0x00E76008）且布局与文件头不同；CRunApp/计数器未定位（转 P5）。
> - 退出：WM_CLOSE → 进程 0xC0000005；无注入原版对照同样 → 游戏原生缺陷。
> - 未取得项（【v1.3】更新）：Present 参数 → P3 进行中（Hook 链已在离线 harness 验证，真实设备参数待解锁后记录）；DirectSound 调用链 → P6 前置探针；CRunApp/计数器 → P5 前置探针。
> - 【v1.3】加载时序实测：mmf2d3d9.dll 在 mmfs2 之后 250–437 ms 加载，d3d9.dll/dsound.dll 与其同时出现（实测基址 0x69FD0000 / 0x69260000）；mmf2d3d8.dll 全程未加载。

**目的：** 在实现任何 Hook/内存读取前，先取得运行时证据基线。探针只读：不 Hook、不写游戏内存、不修改文件。

- 32 位外部启动器：CreateProcess(SUSPENDED) → 远程 LoadLibraryW 注入探针 DLL → Resume。
- 采集项：①模块清单与基址（mmfs2.dll / mmf2d3d9.dll / 系统 DLL，是否落盘 temp）；②窗口标题/窗口类/客户区尺寸/风格；③D3D9 设备 Present 参数（backbuffer 尺寸/窗口模式/交换效果）；④DirectSound 设备与 buffer 属性；⑤PAMU/CRunApp 候选指针与帧句柄读取尝试（只读）。
- 交付：docs/RUNTIME_PROBE.md + 原始日志（logs/）。
- 验收：R286 下每项要么有数据、要么记录不可得原因与降级方案（如 mmfs2 纯内存映射 → 模块内 AOB+版本指纹）；探针不得导致游戏崩溃。

### 任务

- [x] 根据 PE 架构确认框架 DLL 和注入/加载机制的架构匹配。【v1.1】游戏为 PE32 x86 → 框架 DLL 必须 32 位。【v1.2】已完成：P2a 用 winlibs i686 构建 32 位 launcher + 探针 DLL，注入成功。
- [x] 比较两种方案：外部启动器负责加载，或单独的测试注入器负责加载；先选维护成本较低的一种。【v1.2】已定：P2a 验证的外部启动器（CreateProcess(SUSPENDED)+远程 LoadLibraryW）定为 MVP 加载方式；P2 直接把 tools/p2a_probe/launcher 升级为框架 loader（日志/指纹/DLL 生命周期），d3d9 代理仅列备用、不实现。
- [x] 建立最小 DLL：进程附加后只初始化日志、获取版本指纹，不执行游戏状态读写。【v1.2】P2a 已证明主线程恢复前注入可行；框架 DLL 需自行等待 mmfs2.dll 加载完成（轮询 GetModuleHandle，超时则安全退出），模块基址/函数地址一律用「模块内偏移/AOB + 版本指纹」定位，禁止硬编码绝对地址。【v1.3】已完成：framework/native/slmodf.cpp（DllMain 仅 CreateThread；日志→指纹门禁→等待 mmfs2→PAMU 校验→init 标记→心跳）；loader 见 framework/launcher/main.c（SHA-256 门禁 + 远程 LoadLibraryW + init 轮询 + --test-exit 回归）。
- [x] 明确线程模型：不要在 `DllMain` 中做复杂初始化、加载 Lua、创建 GUI 或等待其他线程；通过安全的初始化路径完成工作。【v1.3】已完成：DllMain 只 CreateThread；全部初始化在 worker 线程；GUI/ImGui 上下文在 Present 钩子内懒初始化。
- [x] 加入安全关闭机制：停止工作线程、移除已安装 Hook、释放资源，或在无法安全卸载时明确要求随游戏进程结束。【v1.2】实测：WM_CLOSE 退出时游戏可能以 0xC0000005 结束（原版无注入同样），DllMain/DLL_PROCESS_DETACH 不可依赖；框架状态/配置须运行期持续落盘。【v1.3】部分满足：当前无退出回调，心跳停止即结束；P3 起在检测到 WM_CLOSE/设备丢失时主动保存并停止绘制。
- [x] 记录加载失败、版本不匹配和初始化异常。【v1.3】已完成：框架日志 + init-<pid>.done（status/detail/version/gate/pamu/modules）+ launcher 退出码 0=成功/1=通用失败/2=注入失败/3=指纹不匹配/4=初始化超时。

### 交付物

- 可构建的最小 DLL / 启动器（按选择的方案）
- `docs/LOADING_AND_LIFECYCLE.md`
- 日志文件示例
- 【v1.3】已完成：`framework/`（native/、launcher/、bin/、tests/、build.sh）、docs/LOADING_AND_LIFECYCLE.md（10 节，含实测表）、logs/framework/ 样例日志、recon/out/p2-framework-title.png
- 【v1.1】P2a 探针产物：docs/RUNTIME_PROBE.md、探针 DLL 与启动器源码（tools/ 下）、原始日志【v1.2】已完成：tools/p2a_probe/、logs/probe/、recon/out/p2a-window-title.png。

### 验收标准

- 在已记录版本上以原版对照判定崩溃（WM_CLOSE 退出码 0xC0000005 为原生行为，不算框架缺陷）；连续 3 次「启动→标题页→退出」无新增崩溃。
- 在错误版本或错误架构上拒绝初始化相关功能。
- 【v1.2】loader 在 EXE 指纹不匹配（SHA-256 / Fusion build / Steam buildid）时拒绝注入并给出明确错误码。
- 框架未开启时，游戏运行与原版一致。
- 【v1.1】P2a 探针完成对 mmfs2 加载方式/基址、窗口模式、D3D9 参数、DirectSound 设备、帧读取可行性的记录；未取得项有降级方案。【v1.2】已完成：上述均已记录；Present 参数、DirectSound 调用链、CRunApp 指针链未取得，已分别转入 P3/P6/P5 前置探针。

### 【v1.3】Phase 2 完成记录（2026-10-25，实测）

> 交付物：`framework/`（native/、launcher/、bin/、tests/、build.sh）、`docs/LOADING_AND_LIFECYCLE.md`、`logs/framework/` 样例日志、`recon/out/p2-framework-title.png`。验收对照：
> - ✅ 连续 6 次「启动→标题页→WM_CLOSE 退出」无新增崩溃；6 次中 1 次（pid 20244）PAMU 首轮未命中被误判 degraded，加 300 ms×15 s 重试后 6/6 gate=ok。
> - ✅ 错误 EXE（notepad.exe）→ loader 退出码 3，不创建游戏进程（指纹门禁负例）。
> - ✅ 框架未开启时游戏运行与原版一致；退出码与原版对照同为 0xC0000005。
> - ✅ loader 指纹门禁：全文件 SHA-256（950,556,728 B，约 4–5 s）；DLL 只读、DllMain 仅 CreateThread。
> - ✅ 注入：远程 LoadLibraryW（HMODULE 约 0x67EB0000），mmfs2 加载可见延迟 125–235 ms；PAMU 运行时 build 286 门禁（带重试）。
> - ✅ init 标记 / 5 s 心跳 / 日志约定；退出码：0 成功、1 通用失败、2 注入失败、3 指纹不匹配、4 初始化超时。
> - ⚠ 无退出回调：原生 WM_CLOSE → 0xC0000005，框架状态/配置在运行期持续落盘，不依赖 DLL_PROCESS_DETACH。

## Phase 3 — GUI Overlay 原型（【v1.4】已完成 2026-10-25）

### 任务

- [x] 完成渲染 API 探测并记录证据，选择一个经过验证的后端。【v1.2】已确认 D3D9/mmf2d3d9；Present/Reset 参数由 P3 hook 时记录。
- [x] 集成 Dear ImGui，先只显示一块包含框架名称、版本和游戏指纹的窗口。【v1.4】已完成：真实 D3D9 设备上懒初始化 ImGui Win32+DX9 后端；状态面板叠加在标题画面（logs/probe/p3-final-gui.png），Present=2319、FPS 60.0、draw_vtx=1984；离线 `--imgui-only` 可无设备回归 UI 代码。
- [x] 添加可配置快捷键（默认建议 `Insert`）打开/关闭 GUI。【v1.4】默认 Insert 与 config `hotkey=f2`/`0x71` 两条路径均实测；按住热键的自动重复（WM_KEYDOWN lParam bit30）只吞不切换。
- [x] 处理鼠标与键盘焦点：GUI 打开时不应意外触发游戏内门、按钮或交互。【v1.4】已确认 Mf2MainClassTh 为顶层、Mf2EditClassTh 为渲染/输入面；GUI 打开时键鼠全部被吞（swallow_k=3/swallow_m=5），关闭后消息穿回原窗口过程（`--wndproc-only` 第 5 步 + 真实游戏）。
- [ ] 支持窗口化、全屏、Alt-Tab、分辨率变化和游戏结束。【v1.4】部分完成：游戏为 2560×1440 无边框全屏、D3DPRESENT_PARAMETERS windowed=1；Reset Hook 实测 `Reset ok backbuffer=1258x664 → 2560x1440` 并恢复绘制；游戏结束/关闭生命周期已验证。**Alt+Tab/最小化/手动分辨率变化未逐一验证**，转 P4 顺带回归。
- [x] 增加独立 HUD 绘制层，允许 GUI 关闭时仍显示插件绘制的图标/警告。【v1.4】已完成并目视确认：`tools/p3_hud_check.ps1` PASS（GUI 关闭时 `draw| gui=0 hud=1 vtx=200`），`logs/probe/p3-hud-live.png` 底部可见 `SLModFramework 0.2.0-p3 | Insert=设置 | PAMU=…`。
- [x] 如果设备重置或窗口重建导致渲染后端失效，应能恢复或安全关闭 overlay。【v1.4】Reset Hook：Invalidate → 原 Reset → CreateDeviceObjects，实测恢复；Present 返回 D3DERR_DEVICELOST 时走同一 Invalidate 路径。

【v1.1】实现要点（来自静态证据与联网调研）：
- 主路线：MinHook hook IDirect3DDevice9::Present（vtable #17）与 Reset（#16），ImGui 使用 D3D9 后端。
- 输入屏蔽：WndProc 子类化 + ImGui WantCaptureMouse/Keyboard；实测 mmfs2 无 DirectInput/RawInput/GetAsyncKeyState 导入，输入全走窗口消息。
- DPI：【v1.2】实测进程已 Per-Monitor DPI Aware（144 DPI），不要修改进程 DPI 感知；用 GetDpiForWindow 缩放字体与布局。
- 设备丢失：TestCooperativeLevel 失败时释放并重建 ImGui D3D9 设备对象。
- 【v1.2】退出缺陷：游戏原生 WM_CLOSE 可能 0xC0000005；overlay 卸载不能只靠 DllMain，必须在检测到 WM_CLOSE/设备丢失时主动保存并停止绘制；测试以原版对照为准。
- 降级：【v1.2】已确认非独占全屏（无边框最大化），外部 HUD 仅作安全模式保留；若 hook 失败先报告再选替代，不无限猜。


【v1.3】P3 实测与阻塞记录（2026-10-25）：
- Hook 链：d3d9!Direct3DCreate9 → IDirect3D9::CreateDevice → IDirect3DDevice9 vtable #17 Present / #16 Reset，全部经 MinHook；`MH_CreateHookApi` 只创建不启用，必须 `MH_EnableHook`（首版漏掉导致 presents=0，已修复并在真实游戏中确认断点命中）。
- 窗口：顶层 Mf2MainClassTh + 渲染/输入子窗口 Mf2EditClassTh（晚约 5 s 创建，需心跳重扫补挂）；键鼠消息应发给子窗口，两个窗口都要子类化。
- 离线测试台：framework/tests/d3d9_harness.cpp（自建 D3D9 设备走同一 Hook/ImGui 路径；退出码 0 PASS / 1 FAIL / 2 SKIP）与 `--wndproc-only` 输入分流模式（16:04 PASS）。
- 已验证（锁屏下）：真实游戏内 Insert 热键开关、GUI 打开时键鼠吞噬（swallow_k=3/swallow_m=3）、launcher 单窗口投递（向顶层+子窗口广播会双 toggle）。
- 生命周期：WM_CLOSE/WM_QUERYENDSESSION/WM_ENDSESSION → closing=1、立即关闭 GUI、Present 停止绘制（原版退出路径本身 0xC0000005，不注册退出回调）；离线 16:16 与真实游戏 16:17 各验证一次。
- 离线补充：`--wndproc-only` 覆盖 WM_CLOSE（SUMMARY2-ext closing=1，16:16 PASS）；`--imgui-only` 无 D3D9 设备构建完整 UI（GUI+HUD 2478 顶点 / HUD-only 200 顶点，16:20 PASS），证明 UI 代码路径无断言/崩溃。
- 阻塞：Windows 会话锁屏时 HAL/REF 均 CreateDevice=0x8876086A，游戏同样黑屏；Present/ImGui 上屏、Reset 恢复、Alt-Tab/分辨率变化待解锁后重测（一键脚本 tools/p3_final_check.ps1）。
- 可配置热键（16:30）：config 写 hotkey=f2 → 框架日志 hotkey=VK 0x71；launcher --toggle-key 0x71 自动化下 visible=1 / swallow_k=3+swallow_m=3 / visible=0；默认 Insert 由 16:09、16:17 run 覆盖。
- PAMU 扫描加固（16:28）：改为 MEM_PRIVATE + ReadProcessMemory，修复裸解引用 AV 导致的三次启动挂起；16:28:56–16:30:56 连续 4 次 gate=ok、窗口正常。
- 门禁负例（17:09–17:10）：追加 1 字节的 EXE 副本经 --no-verify 注入 → sidecar verified=0、gate=fail fingerprint_mismatch、loader rc=2，游戏仍可启动；修复 ReadSidecarFingerprint 误报 verified=1 与 --exe 正斜杠 CreateProcess err=267。
- 配置鲁棒性（16:56–16:59）：hotkey=bogus 回退默认 VK 0x2D、hud/hook_d3d9 仅接受 0/1、wait_mmfs2_ms clamp、支持 hotkey=0x71；证据 slmodf-20261025-165625-2812 / -165801-4708 / -165826-24776 / -165859-25428.log。
- 安全模式与等待窗口（17:14–17:15）：hook_d3d9=0 → init status=ok hooks=0、游戏正常启退；wait_mmfs2_ms=1 仍 waited_ms=0（mmfs2 在 worker 等待前已加载）。
- draw 遥测加固（17:13）：vtx 0→>0 上升沿日志 + p3_final_check 取所有 draw 行最大 vtx；离线 wndproc/imgui 回归 PASS。HUD 独立层检查脚本 tools/p3_hud_check.ps1（锁屏 exit 2，解锁后验证 GUI 关闭后 hud=1 vtx>0 + 截图）。
- 验收对照：docs/OVERLAY_INPUT.md §7（截至 17:11）逐项列出 P3 验收状态；未完成项仅剩需解锁的 GUI 上屏/Present/Reset/Alt-Tab 与 HUD 视觉。
- 热键自动重复防护（19:20）：HookWndProc 检查 WM_KEYDOWN 的 lParam bit30，重复事件只吞不 toggle（日志 gui| ignore hotkey auto-repeat count=N）；--wndproc-only 新增步骤 1b 断言 hotkey_repeat=1/1/0、SUMMARY2 pass=1（harness-20261025-192019.log）。修复真实键盘“按住热键 GUI 闪烁”隐患。

【v1.4】P3 完成记录（2026-10-25 20:17–20:41，解锁后真实设备实测；本节取代上面 v1.3 的“阻塞”状态）：
- ✅ GUI 上屏：`tools/p3_final_check.ps1` 输出 `PHASE3 VERIFY: PASS`；框架日志 `d3d9| device vtable hooked present=6A0B02A0 reset=6A0B06A0 origPresent=003D0F80`、心跳 `presents=2319 fps=60.0 device=04715BC0`；截图 p3-final-gui.png（面板叠加画面）/p3-final-gui-closed.png（关闭后干净）。
- ✅ 中文字体与 DPI：首版中文全为 `????`（ImGui 默认字体无 CJK）；新增 `LoadCjkFont()` 优先加载 `C:\Windows\Fonts\msyh.ttc`（14 px，简体常用字形集，失败回退默认字体并记日志），面板按 `g_dpiScale` 放大为 600×520 逻辑、150% 下 1.5 倍；截图 p3-cjk-gui.png 中文正常。
- ✅ HUD 独立层：`p3_hud_check.ps1` RESULT PASS（`hud_vtx=200`、`presents=1737`），GUI 关闭期间 window_capture 目视确认 HUD 条带与文本上屏（p3-hud-live.png）。
- ✅ 验收脚本三处修复：FindWindow 对 Mf2 窗口返回 0 → 改 EnumWindows+pid+类名前缀；stale-log 竞态 → 只认本次 start 之后的日志；`ShowWindow(SW_RESTORE)` 会把后台缓冲重设为 1280×720 → 只 SetForegroundWindow。两个脚本均加 keep-awake（`SetThreadExecutionState(0x80000003)`，用户已授权任务期间阻止锁屏）。
- ⚠ 剩余：Alt+Tab/最小化/手动分辨率变化未逐一验证（Reset 路径已实测），并入 P4 回归；其余 P3 验收项见 docs/OVERLAY_INPUT.md §7（已全部标为通过）。
### 交付物

- 最小可用 GUI
- `docs/OVERLAY_INPUT.md`
- 录屏或截图验收记录（只用于项目内部）

### 验收标准

- GUI 可稳定开关；不遮挡/锁死游戏输入。
- 关闭 GUI 后 overlay 不再接收鼠标点击，但后台插件仍可按配置运行。
- 全屏/窗口化切换和退出游戏不会留下悬挂窗口或明显资源泄漏。

【v1.4】验收结论（2026-10-25 20:41）：前两条已实测通过（GUI 上屏/稳定开关/输入屏蔽；关闭 GUI 后画面干净、消息穿回原窗口过程）；第三条由 `p3_repeat_exit_check.ps1` 5 次启退 PASS + 无残留进程覆盖；全屏/窗口化切换仅验证 Reset 恢复，Alt+Tab/手动分辨率变化转 P4。

## Phase 4 — 配置、插件宿主与 API v0（【v1.5】已完成 2026-10-25）

### 任务

- [x] 配置文件使用明确的版本字段，并支持默认值和损坏配置恢复。【v1.5】已完成：`config_version=1`；缺文件自动写默认；缺失版本按 v1 迁移；非法/未知版本/二进制/无效行过多 → 备份 `framework.ini.bad-<ts>` 后重建（日志 `config| RECOVERED`）。示例 `config/framework.ini.example`。
- [x] 设计插件清单：ID、名称、版本、API 版本、启用状态、入口文件。【v1.5】已完成：`plugin.ini` 字段 id/name/version/api_version/enabled/entry；缺 id 或 api_version → `kInvalid`；根目录 `<game>/Workspace/plugins`，上限 32。
- [x] 插件生命周期最小化为 `load`、`update`、`draw_ui`、`shutdown` 等必要事件。【v1.5】已完成：`plugin_info()` → `on_load(ctx)` → `on_update(ctx)`（每帧）→ `on_draw_ui(ui)`（GUI 打开时）→ `on_shutdown()`，全部在渲染线程、经 `lua_pcall` 调用。
- [x] 若采用 Lua：嵌入 Lua（5.4 系列），限制插件只通过框架暴露的 API 访问游戏；每个回调使用受保护调用并捕获脚本错误。【v1.5】已完成：Lua 5.4.7 静态链接；单 state + 每插件独立只读 `_ENV`；仅 base/table/string/math/coroutine/utf8；io/os/package/debug 与 dofile/loadfile/load/require 移除；错误按插件计数并限流（前 3 次逐条、之后每 600 次一条）。
- [x] 初期不要开放任意内存读写、任意 Hook 注册、任意文件系统写入等高风险接口。【v1.5】已完成：v0 插件只有 `sl.log/sl.warn`、`ctx.game` 只读占位字段与 5 个 UI 原语；无文件/内存/Hook 接口。
- [x] GUI 显示插件启用状态、版本/API 不兼容原因和错误日志。【v1.5】已完成：面板显示 `插件 API v1 | total/loaded/disabled/api_mismatch/error/invalid`，逐插件复选框 + 颜色状态 + 版本/api/upd/err + 错误文本；“重新扫描插件”重载。
- [x] 先写一个只输出日志并显示“Hello Plugin”的测试插件。【v1.5】已完成：`plugins/HelloPlugin/`（中文自绘 UI、update/draw 计数、示例复选框），实机 pid 3588/19188 验证。

### API v0 建议（【v1.5】已实现：以下是原草案，实际接口与差异见 docs/PLUGIN_API_V0.md）

```lua
-- 示例接口草案，不是已经存在的实现

function plugin_info()
    return {
        id = "example.hello",
        name = "Hello Plugin",
        version = "0.1.0",
        api_version = 1
    }
end

function on_update(ctx)
    -- ctx.game 在初期可能只包含 frame_id / scene_id / supported 状态
end

function on_draw_ui(ui)
    ui.text("Hello from plugin")
end
```

### 交付物

- [x] `docs/PLUGIN_API_V0.md`（已产出：目录/清单、沙箱、生命周期、ctx/ui、启停持久化、错误与限流、日志行、示例、验证记录、限制）。
- [x] `plugins/HelloPlugin/`（已产出：`plugin.ini` + `main.lua`，中文 UI、自绘复选框、update/draw 计数）。
- [x] 配置 schema 示例：`config/framework.ini.example`（`config_version=1` + hotkey/hud/hook_d3d9/wait_mmfs2_ms 注释；首次运行自动生成 `config/framework.ini`）。

### 验收标准

- 插件能被发现、加载、启用/禁用；错误插件不会让整个框架直接崩溃。
- API 版本不匹配时给出清楚提示。
- 重启游戏后插件与配置状态仍符合预期。

【v1.5】Phase 4 验收结果（2026-10-25 21:14）：
- 离线夹具 6 个（good / mismatch(api=99) / syntax / disabled / runtime 每帧抛错 / sandbox 禁库检查）：`SUMMARY4 pass=1 total=6 vtx=2586 good=1/3/0 mismatch=1 syntax=1 disabled=1 runtime_errors=3 sandbox=1/0`（`logs/framework/harness-20261025-205954.log`，命令 `harness32.exe --plugins`）。
- 真实游戏：HelloPlugin 加载并自绘中文 UI（pid 3588，`logs/probe/p4-plugins-gui.png`）；GUI 勾选框禁用 → `plugin| set enabled=0` 写回清单 + `disabled by manifest`（pid 23104，`p4-plugin-disabled.png`）；重启仍禁用（pid 20904）；再启用恢复 loaded（21:12:28 `imgui click at 86.0,809.0`）；最终构建复测 pid 19188（`p4-final-plugin-disabled.png` / `p4-final-plugins.png`）。
- API 不匹配/语法错/运行时错/沙箱越权均有独立夹具断言，坏插件不影响框架与其它插件；插件状态与清单持久化通过重启验证。
- 详细接口、沙箱边界、ctx/ui 字段、错误处理与复现命令见 `docs/PLUGIN_API_V0.md`。

### Phase 5.0 — 调试场景直达（debug warp，P5 前置）【v1.6】

- **背景**：P5 需要 Breaker Room（帧 9）与 Private Room（帧 27）多局实机采集，但游戏仅有一个初始存档（`%APPDATA%\MMFApplications\sl`，内容 `[sl]/intro=1/current=1`），且存档只记录“是否看过开场”与“当前夜”，**没有夜内场景字段**；改存档最多跳到某夜开头。
- **目标**：开发期在 ≤30 秒内进入任意目标帧（至少帧 9、帧 27），支撑 P5/P6/P7 的多局采集与触发时机调试。
- **边界**：warp 是开发期工具，**不进入插件 API、不进入常规用户界面**；仅在 `debug=1` 配置下可用并写日志；不属于发布功能，不影响原版安装。
- **P5.0a（低成本，先行）**
  1. 存档辅助：`tools/save_night.py --night N` / `--restore`；改动前把 `sl` 备份到 `Workspace/backups/save/` 并记录 SHA-256，支持一键恢复；只写 `current` 字段。
  2. Extras 入口实测：标题页 → Extras → 点击 `little Baby` → 静态已知会 `Jump to frame 27`（见 §7.1）；确认解锁条件、进入后对象/计时器/摄像头是否正常，记录失败行为。
- **P5.0b（主力，必要时做）**
  3. Ghidra 反汇编 `mmfs2.dll`（`Workspace/recon/out/runtime-temp/mmfs2.dll`，470,528 B，**纯序数导出**）：从事件动作 `Game act #2` 的执行路径或一次自然跳帧的调用栈定位帧跳转入口（形如 `(CRunApp*, frameIndex)`）。
  4. 框架 debug 命令 `warp <frame>`：校验目标帧范围、记录前后帧号、失败安全返回；从标题页与目标帧内各测 5 次，检查目标帧渲染/对象创建/事件计时器是否启动。
  5. 最小前置状态清单：记录 `go to` / `night 2` / `Script Event` 等全局 Counter 在 warp 前是否需要设置，以及不满足时的崩溃/黑屏表现。
- **止损点**：若累计投入 >6 小时仍无稳定入口，停止逆向，退回 P5.0a + 自然游玩采集，并在文档写明限制。
- **交付物**：`tools/save_night.py`、`tools/debug_warp/`（或框架 debug 命令）、`docs/DEBUG_WARP.md`、`analysis/warp-test-log.md`（每次 warp 的帧号/结果/截图/日志路径）。
- **验收**：① 帧 27 至少一条路线（Extras 或 warp）稳定进入且可交互（cam/门/计时器）；② 帧 9 经 warp 进入后 60 秒内目标计数器（Freddy/danger level 4/got you）变化可解释；③ 5 次内崩溃/卡死 ≤1 次且原因可定位、存档可恢复。

【v1.7】Phase 5.0 完成记录（2026-10-26 00:46）：
- P5.0a（存档辅助）已完成：`tools/save_night.py`（--show / --backup / --night N / --set KEY=VALUE / --restore，原子写 + SHA-256 校验 + Workspace/backups/save/ 备份）。实测 `--set beat1=1 --set beat3=1` 解锁标题 EXTRAS 与 CUSTOM NIGHT（帧 1 G47/G51 的 beat1 门、G48/G52 的 beat3 门）；`--night N` 只改 current。
- P5.0b（场景直达）已完成，**未使用 Ghidra**：改用「自然跳帧字段追踪 + 内存差分」直接破译运行时协议。
  · 场景识别已上机验证（框架 `scene` 模块，36 帧表 + anchor/pair 扫描）：实测识别 title screen(1) / Extras(18) / 8-bit Baby Game(26) / Girl Voice(25) / Elevator(2) / Breaker Room(9) / Final Encounter(27)。
  · CRunApp / CRunFrame / RunHeader 偏移、frame handle→index 映射（32 项非恒等）、跳帧协议（`rhQuitParam=handle` + `rhQuit=3`）全部实测；详见 `docs/RUNTIME_SCENE.md`。
  · 调试跳帧工具 `tools/p5_jump.py`（dev-only，只写 RunHeader 两个字段）：实测 Elevator → handle 8 → Breaker Room；Breaker Room → handle 28 → Final Encounter。
  · 截图证据：`logs/probe/p5-warp-breaker-room.png`（Breaker Room，含 DANGER LV HUD 与 SPACE 提示）、`logs/probe/p5-warp-final-encounter.png`（Final Encounter，USAGE / POWER 98% / 12 AM / 三监视器）。
  · 结论：§7.2 第 8 项已解；warp 后帧内对象/计时器初始化实测完整，但仍按双轨证据规则（warp 取证 + 自然游玩复核）区分证据强度。

## Phase 5 — GameAdapter 与 DebugInspector

### 任务

- 【v1.7】采集前置已就绪：Phase 5.0 完成（见上）；帧 9 / 帧 27 可用 `tools/p5_jump.py` 直达（handle 8 / 28）；Extras→little Baby 仍是自然入口之一，但受 handle 映射影响实际进入 index 26 8-bit Baby Game（见 §7.1）。
- [x] 【v1.8】建立统一的只读状态接口；插件不得直接使用底层地址。→ 框架内 `native/state.h` 已提供 scene/对象/计数器只读查询 API（`state::Status/SceneIndex/ObjectCount/CounterCount/CounterName/CounterValue/CounterValid/FindCounter/FormatSummary`）；**插件侧**读取入口【v1.9】已接入：`ctx.game.scene_id/scene_name/scene_width/scene_height/counters_ready/counters`（只读副本，無原始地址）；HelloPlugin 实机读到 `scene=9:Breaker Room counters=[Freddy=0 got you=0 danger level 4=1]`。插件仍不得直接读内存。
- [x] 【v1.8】先支持 `supported / unknown` 等明确状态，未知状态时不生成任何肯定性预警。→ `state::Status()` 五态（0 未就绪 / 1 ok / 2 部分 / 3 无场景 / 4 门禁未通过）；单计数器越界或对象校验失败时 `CounterValid=0`、GUI 显示 `?`，不解读为安全/危险。
- [x] 【v1.8】研究 Fusion 运行时数据结构、相关对象以及状态变量的可靠定位方式。→ 已完成：PAMU→CRunApp→CRunFrame→RunHeader→OI 表→对象头→计数器值；偏移与验证方法见 §7.1 与 docs/RUNTIME_STATE.md。
- [x] 【v1.8】采用经过验证的签名/结构识别或版本专用适配，不使用单次搜索得到的地址当作永久地址。→ 每次会话重新扫描 CRunApp（字段校验：maxIndex/maxHandle/映射表排列）；帧对象地址复用通过 rh 变化检测重建；所有地址均当次解析、不落盘复用。
- [x] 【v1.2】先写只读扫描探针：定位 PAMU → CRunApp → 帧/对象指针链，并在跨场景/跨启动下验证稳定性（P5 前置）。→ 已完成：`tools/p5_objects.py`、`tools/p5_inst.py`、`tools/p5_runapp.py` 在多 pid / 多次启动下验证（见 docs/RUNTIME_SCENE.md、docs/RUNTIME_STATE.md）。
- [x] 【v1.8】对所有读到的指针和数据做边界、可读性、合理范围校验；访问失败时返回 `unknown`。→ 对象校验 hoSize(0x40..0x8000)/hoOi(句柄查表)/hoType(与 OI 一致)/hoOiList(等于 oiList+idx*146)；计数器值限制 ±1e6，超界置无效。
- [x] 【v1.8】用 DebugInspector 显示当前识别到的帧/场景、目标变量、上一次状态变化时间和数据来源。→ GUI「场景状态 (DebugInspector)」显示场景/帧索引/对象数/计数器数/rh + 场景相关关键计数器（帧 9：Freddy/danger level 4/got you/beat/interface；帧 27：hour/power left/final stage/got you/side 等）；计数器初值与每次变化写日志（`state| counter <name> <old> -> <new>`），日志行有时间戳；“数据来源/置信度”标签与“上次变化时间”字段待补。
- [x] 建立状态录制功能：按需写入带时间戳的状态快照，帮助比较实际攻击时间与预警触发时间。【v1.9】已完成（2026-10-26 01:58，`native/recorder.cpp`）：JSONL 逐行快照（ts_ms/scene/scene_name/presents/scene_changed/counters），GUI 「状态录制 (JSONL)」开关与 `record=1`/`record_interval_ms=500` 配置，默认关闭；进入方式（warp/自然）标记仍待接入跳帧工具旁路信号。

【v1.1】候选读取路径（静态已知对象名，R286 布局待实测）：
- 读取链候选：【v1.2】进程内扫描 PAMU 魔数并校验 runtimeVersion/productBuild → 再找 CRunApp → 帧句柄/对象列表 → 按对象名/句柄读数；备选：从 mmfs2.dll 导出/字符串/全局引用定位 CRunApp。
- 帧 9（Breaker Room）：Freddy（Counter）、danger level 4（Counter）、got you（Counter）、danger level 2/3、beat、jumpscare。
- 帧 27（Final Encounter）：final stage（Counter）、got you（Counter）、power left、power count down、Ennard 与位置对象、left/right/vent door、left/top/right is closed、cam 01–07。
- 降级方案：若对象/计数器读取不可靠，退化为「帧 ID + 音频播放事件 hook + 事件解释器 hook」组合判断；仍不可靠则该场景只提供调试信息，不提供预警。
- 【v1.2】约束：PAMU 在私有堆可读但地址每次启动随机（0x016257D8 / 0x00E76008），内存结构比文件头少 runtimeSubversion 字段 → 不能按文件字节布局直读，也不能硬编码。
- 【v1.2】P5 必须先在探针里验证指针链跨场景/跨启动稳定，再写适配器常量；任何一步校验失败即返回 unknown。

【v1.8】Phase 5 进展记录（2026-10-26 01:28）：state 模块与 DebugInspector 首版完成并实机验证：
- 实现：`native/state.h` / `native/state.cpp`（只读），由 `slmodf.cpp` worker 每 250 ms 调 `state::Poll()`；`overlay.cpp` 新增「场景状态 (DebugInspector)」面板；构建脚本 native 列表加入 `state`。
- 读取链：scene::Current() → CRunFrame+0x7C(rh) → RunHeader+0x18(app)/+0x54(numberOi)/+0x58(oiList) → OI 表（stride 146，名字 UTF-16LE@0x5A）→ 全内存搜 dword==rh 得对象（hoAdRunHeader）→ 校验对象头 → 计数器值 = `~int16(obj+0x20A)`。
- 实机证据（pid 6756，日志 `logs/framework/slmodf-20261026-012353-6756.log`）：Breaker Room（warp handle 8）map `oi=80 objs=105 counters=30`，`state| scene=9 ... Freddy=0 danger level 4=1 got you=0 beat=0 interface=0`；Final Encounter（warp handle 28）map `oi=78 objs=73 counters=19`，`state| hour=12 power left=99 final stage=0 side=2 viewing cam=1 count seconds=1`；GUI 截图 `logs/probe/p5state4-gui-fe.png`、`p5state4-gui-breaker2.png`、`p5state4-gui-breaker3.png`（120 s 后 Script Event 82→83 证明实时刷新）。
- 语义闭环：标题页按 Down，`state| option=1 -> option=3`，对象 +0x208 dword 0xFFFE0000→0xFFFD0000→0xFFFC0000（= int16 -2/-3/-4 取反 1/2/3），与公式完全一致；截图 `p5state5-option2.png` / `p5state5-option3.png`。
- 限制：①每次映射重建全表扫描 670–855 regions / 350–780 MB / 230–360 ms（仅在场景或 rh 变化时触发，5 s 心跳）；②有效对象数少于 application.json 实例数（105/109、73/77，原因未定，可能为未创建/已销毁/校验过严）；③场景识别延迟（rh=0 的帧走 table/pair 兜底，最长 14–20 s）在 state 上仍存在；④计数器数值语义（正常值域、与行为对应关系）只在标题/两目标帧局部验证，需自然游玩复核。
- 下一步：scene 快速路径（缓存 CRunApp，只读 app+0x1D8）→ 状态录制器 → 插件 API 暴露；然后 P5 收尾进入 P6。

【v1.9】Phase 5 收尾记录（2026-10-26 02:05）：scene 快速路径、状态录制器、插件 ctx.game 均已完成并实机验证：
- scene 快速路径（`native/scene.cpp`）：缓存 CRunApp（`scene| fast path ready app=… via runheader/scan`），之后每 poll 只读 `app+0x1D8` 得 CRunFrame；§保留 anchor/pair 扫描作兜底，失败 >5 s 才清缓存。实测跳帧识别延迟 Extras 1.68 s / Breaker Room 2.06 s / Final Encounter 3.22 s（§帧加载本身耗时，无 14–20 s 扫描度）；日志 `logs/framework/slmodf-20261026-014052-24152.log`、`logs/probe/p5fast-run.log`。
- 状态录制器（`native/recorder.cpp`）：JSONL 逐行快照，场景切换行等 state 映射就绪（最长 3 s）后写；同名计数器写 `compute#2…compute#16`；实测 95 行/4 个场景（Warning 3、title 10、Breaker 30、Final 19），日志 `recorder| scene … counters=N line=N`。
- 插件 ctx.game（`plugin_host.cpp::PushGameContext`）：新增 scene_id/scene_name/scene_width/scene_height/counters_ready/counters（名→整数，最多 64）；HelloPlugin 实机读到帧 9/27 计数器（详见 docs/PLUGIN_API_V0.md §4）。
- 离线回归（重建后）：`--wndproc-only` / `--imgui-only` / `--plugins` 均 PASS（`logs/framework/harness-20261026-015751.log`）。
- 剩余：自然游玩复核计数器语义（双轨证据）；可选的数据来源/置信度标签；无敌人 AI/计时/胜负写入（仅读）。

【v1.12】Phase 5 自然游玩复核完成（2026-10-26 05:20，用户真人操作三局；双轨证据的自然轨道）：
- Night 2 Ballora Gallery（帧 8，pid 24092）：`jumpscare` 0→1 @04:41:46.548（distance 582→601）→ death @04:41:48.046（约 1.5 s，`random=5`）→ game over @04:41:53.347；确认该计数器是死亡前约 1.5 s 的唯一自然信号。
- Night 2 Breaker Room（帧 9，pid 24092）：Freddy 1→2 @04:47:47.229、4 @04:47:49.234、6+got you=1 @04:47:51.238、interface=100 @04:47:52.740、got you=2 @04:47:54.243、death @04:47:56.062（`random=3`）；与静态事件组 58–61 完全吻合，Freddy≥6 到死亡约 4.8 s。
- Night 5 Final Encounter（帧 27，pid 28120）：入场 hour=12/power left=99/final stage=0/side=2；hour 每 90 s 一档（12→1 +89.24 s，随后 +90.26/+89.74/+90.25/+89.75）；power left 99→7 约 470 s（~5.1 s/点）；final stage 0→1 +3.51 s、1→2 +29.58 s。
- Night 5 死亡链：got you 0→2 @05:15:58.285 → death @05:15:59.971（+1.686 s），死亡时 power left=7、三扇门均未关（AFK）；death 行 `random=1` → 约 5.2 s 后进入帧 26 8-bit Baby Game，再由帧 15 game over。
- 入口修正：Night 5 自然路径为帧 10 Funtime Auditorium 经 keycard 闸门进入帧 27（存档 keycard=1 播 "granted"；keycard=0 播样例 #50 "denied" 且约 2 s 后 got you=1 死亡）；keycard 由帧 26 8-bit Baby Game 完成时 `DO "Ini" act #87 keycard = 1` 写回，属小游戏奖励而非可跳过内容。此前的「Private Room 来自帧 14 death/帧 18 Extras」表述已废弃。
- 计数器语义修正：帧 27 的 `near` 全程为 0 且事件无引用，不得用于预警；`side`（1/2/3）是已核对计数器中唯一在死亡前持续变化的方向信号；`viewing cam` 取 1/2/5；`got you` 到死亡仅约 1.7 s，不能作为预警触发；Ennard `alterable[3]==99` 表示到达门口（静态事件），需 P5 扩展读取 Active alterable 后才能用。
- 证据：`docs/P5_NATURAL_VERIFICATION.md`（17,713 B / 173 行）、`analysis/data/night5-private-room-natural-20261026.csv`（938 行）、`analysis/data/night2-breaker-room-natural-20261026.csv`（102 行）、`logs/state/state-20261026-{043055-24092,044907-26740,050107-28120}.jsonl`。
- 剩余：可选的数据来源/置信度标签；有效对象数少于 application.json 实例数（105/109、73/77）原因仍未定；跨启动长期稳定性样本在 P7 自然数据中继续累积。

### 建议的只读状态模型

```text
GameSnapshot
├─ version_status: supported | unsupported | unknown
├─ scene: known scene ID | unknown
├─ frame_id: optional
├─ elapsed_time: optional
├─ entities: only reliably identified entities
└─ evidence: adapter/version profile that produced the data
```

### 交付物

- `native/game_adapter/`
- `plugins/DebugInspector/`
- `docs/STATE_ADAPTER.md`
- 至少一份真实游玩状态日志
- 【v1.1】docs/RUNTIME_LAYOUT.md：每个偏移/AOB 签名的验证方法、适用 build、失败行为；R286 未验证前不得写入代码常量

### 验收标准

- 无法识别数据时显示 `Unknown`，绝不将无效值解释成安全或危险状态。
- 游戏跨场景切换时适配器能重置场景缓存，不继续使用上一场景的指针。
- 重复游玩同一场景时，采集到的关键状态与实际行为基本一致。【v1.12】已由用户真人自然游玩三局复核（Ballora Gallery / Breaker Room / Final Encounter；见 docs/P5_NATURAL_VERIFICATION.md）。
- 【v1.1】若读取路径不可靠，适配器必须按降级方案返回 unknown/调试信息，而不是猜测预警。

## Phase 6 — SoftJumpscares 插件

### 任务

> 【v1.1】原假设（已废弃）：先研究「可回滚的资源替换/打补丁」。实测证明本作 950 MB 单 EXE、资源全部内嵌（pack/chunk），无外置文件可替换；改包需 EXE→MFA + 正版 Fusion 2.5 Developer，且 Steam 校验会覆盖。

- [x] 列出需要处理的 jumpscare 音频资源：索引 86/112/120/148/257/265/303，共 20 处播放点（详见 analysis/jumpscare-audio.md）。

> 【v1.2】P2a 证据：DSOUND.dll 已加载；mmfs2.dll 按序号 1 导入 DirectSoundCreate，同时导入 WINMM mciSendCommandA/W → 播放路径可能不止 DirectSound。P6 必须先做前置探针（≤5 分钟）记录 jumpscare 场景实际调用（CreateSoundBuffer/Play/SetVolume vs mciSendCommand），再决定 hook 点。
- [x] 【v1.10】运行时 DirectSound Hook 已实装并验证（`framework/native/audio_hook.cpp`）：hook `dsound!DirectSoundCreate` → `IDirectSound::CreateSoundBuffer`（vtable[3]）→ 每个 buffer 的 `Play`(vt[12])/`SetVolume`(vt[15])；primary 与 secondary buffer 的 Play 实现地址不同（实测 0x69293890 vs 0x69293990），MinHook 单个 detour 无法区分 trampoline，因此按目标地址安装薄 detour 变体（Play 8 个 / SetVolume 6 个）。模块基址仍只用 GetModuleHandle 解析，禁止硬编码。
- [x] 【v1.10】识别路径最终实现（原计划整段样本哈希不可行）：Fusion 以固定 1.2 s chunk 流式播放（chunk_bytes = rate*ch*(bits/8)*6/5；44100/mono/16=105840 B、48000=115200、44100/stereo=211680），probe dump 确认 chunk #0 与样本 PCM data 前缀逐字节相同 → `tools/gen_sound_table.py` 为 178 个样本生成 prefixFp/chunkFp（FNV-1a 64），scream 7 条：86/112/120/148/257/265/303。实测帧 9 死亡链：`audio| play #12 … name="scream op5-7" how=3 chunk=0 scream=1`（pid 19712，02:42）。
- [x] 【v1.10】配置已实装：`audio_probe`(0/1)、`audio_soften`(0/1)、`audio_gain_percent`(1..100，默认 25≈-12 dB)，默认全关=原版音效。柔化只作用于匹配到的 scream buffer：off_cb = 2000*log10(gain)（25% → -1204 厘贝），调用真实 SetVolume 后用 GetVolume 回读验证。整体衰减倍率/替换 WAV/高频衰减仍列为后续可选（资源全内嵌，替换需拦截样本加载，见 §2.1）。
- [x] 【v1.10】异常旁路：匹配失败、或该 vtable 变体无 original SetVolume 时只记日志（`audio| soften skipped …`）不改音；`audio_soften=0` 时识别仍工作但整局 0 条柔化（pid 15276 回滚验证）→ 关闭即恢复原版行为。
- [x] 【v1.10】缺失/不支持：ctfak 导出为裸 MP3 的 214 个样本未纳入指纹表，运行时只显示 match=-1、不柔化、不崩溃；指纹表与构建指纹绑定（见 §8 风险登记）。
- [x] 【v1.11】帧 27（Night 5 Final Encounter）`scream op5-6`（handle 303）识别 + 柔化实测：pid 23104 `forced` 写 `got you=1` → `audio| play … name="scream op5-6" how=3 chunk=0 scream=1`，同毫秒 `soften … readback=-1204`（`tools/p6c_final_test.ps1`，报告 `logs/probe/p6c-final-report.txt`）。
- [x] 【v1.11】插件化：`plugin_host` 新增 `ctx.audio` 与 `ui.slider_int/ui.button`；`plugins/SoftJumpscares/` 通过 `ctx.audio.set_soften` 在 config 关闭时独立开启柔化（pid 20980，`tools/p6d_plugin_test.ps1`）。
- [x] 【v1.11】GUI 取证：`tools/p6e_launch_gui.ps1` + `p6e_post_key.ps1`（EnumWindows + PostMessage，不抢焦点），截图 `p6e-plugin-ui.png` 显示插件自绘区与 `探针: true | 柔化: true | 目标: 25%`（pid 28380）。

### 交付物

- [x] 【v1.11】`plugins/SoftJumpscares/`（已完成：`plugin.ini` + `main.lua`；通过框架 `ctx.audio` 读取 match/统计并设置柔化、白名单；自带开关/增益滑条/Night5 白名单/统计 UI。实测 pid 20980 在 config `audio_soften=0` 时插件自行开启柔化并命中帧 27 scream）
- [x] 【v1.10】`docs/AUDIO_HOOK.md`（12,392 B / 158 行：chunk 事实、Hook 链、指纹算法、实机证据、限制与复现）；原计划的 `docs/AUDIO_MODDING.md` 内容已并入该文档。
- 用于测试的自制/合法替换音效，不把原版音频放入公开仓库
- [x] 【v1.10】`analysis/jumpscare-audio.md`（7 个 scream 样本 + 20 处播放点 + 帧白名单）已完成；A/B 误伤验证见 `docs/AUDIO_HOOK.md` §5（普通音效无 soften）。

### 验收标准

- [x] 【v1.10/v1.11】能在目标 jumpscare 场景触发柔化效果（帧 9 实测：识别 + SetVolume -1204 厘贝 + readback；帧 27 Night 5 实测 `scream op5-6` handle 303 + readback -1204，pid 23104）；替换音效未做（资源全内嵌，见 §2.1）。
- [x] 【v1.10】普通对话/环境音/音乐不误伤：同局 pop1、Static2、Breaker Room 环境音均无 soften 行（pid 24028）。
- [x] 【v1.10】回滚：不改任何游戏文件，`audio_soften=0` 时整局 0 条柔化（pid 15276），退出即恢复原版；原版文件从未被修改，自然可恢复。
- [x] 【v1.10/v1.11】A/B 验证：见 `docs/AUDIO_HOOK.md` §5.2/§5.3；帧 27 `scream op5-6`（handle 303）已补测（§5.5），插件化用例见 §5.6。

### 【v1.10】Phase 6 进展记录（2026-10-26 02:52）

- 范围：先做只读识别（P6a），再做只改音量的柔化（P6b），最后验证关闭即回滚；不触碰游戏文件与存档。
- 实测证据：①识别 pid 19712 `audio| play #12 … match=89 handle=120 name="scream op5-7" how=3 chunk=0 scream=1`；②柔化 pid 24028 `audio| soften … gain=25% off_cb=-1204 volume=0 -> -1204 (readback=-1204)`，同局 pop1/Static2 无 soften；③回滚 pid 15276 识别仍命中、0 条 soften；④离线回归 --wndproc-only/--imgui-only/--plugins 全 PASS（logs/framework/harness-20261026-024926.log）。
- 实现要点：多 vtable 薄 detour（Play 8/SetVolume 6 变体）；每次 Play 重置匹配状态后重算（流式 buffer 会重填）；匹配算法按长度升序单遍增量 FNV + checkpoint 快照，只 Lock 一次；scream chunk 命中优先（how=3），同名同数据样本标 ambiguous（how=5）；只对 scream 生效。
- 限制与后续：①只覆盖有 PCM 指纹的 178/392 样本（214 个裸 MP3 不在表内）；②强制死亡链只播 chunk #0（1.2 s 内切场景），chunk #1+ 机制已实装但暂无长样本实机证据；③音量语义受游戏混音/系统音量影响；④帧 27 scream op5-6（handle 303）待实测；⑤插件化与「只读事件 + 增益」接口待做；⑥指纹表与构建绑定，构建更新需重跑 tools/gen_sound_table.py。
### 【v1.11】Phase 6 收尾记录（2026-10-26 04:03）

- 范围：补齐帧 27 音频链、把音频能力插件化、GUI 取证；chunk #1+ 长样本证据评估后列为已知限制（不阻塞验收）。
- 帧 27（P6c，pid 23104，日志 `slmodf-20261026-034828-23104.log`）：forced 写 `got you=1` → `name="scream op5-6"` handle 303 how=3 chunk=0 scream=1，同毫秒 `soften … gain=25% off_cb=-1204 … (readback=-1204)`；1 s 内游戏走分支 B 切帧 14 death，因此只记录 chunk #0；dump `dump-23104-007-105840-44100-1-16.bin`。
- 插件化（P6d，pid 20980，日志 `slmodf-20261026-035632-20980.log`）：config 只开 `audio_probe=1`、`audio_soften=0`；SoftJumpscares 加载后 `audio| plugin set_soften=1 gain=25%`，帧 27 scream 被柔化（readback -1204）→ 插件可独立控制，不依赖 config 的全局开关。
- 插件 API：新增 `ctx.audio`（supported/soften_enabled/gain_percent/last{has,name,handle,chunk,scream,helper,how,plays,seq}/stats{creates,plays,matched,screams} + set_soften/reset_soften/set_names/clear_names）与 `ui.slider_int/ui.button`（v0 共 7 个 UI 原语）；文档 `docs/PLUGIN_API_V0.md` §4.1/§5/§9/§10/§11。
- GUI 取证（P6e，pid 28380）：`p6e_launch_gui.ps1` 启动等标题页，`p6e_post_key.ps1` PostMessage Insert/滚轮（不抢焦点）；截图 `logs/probe/p6e-plugin-ui.png` 显示 SoftJumpscares 自绘开关、滑条 25、Night5 白名单与统计 `探针: true | 柔化: true | 目标: 25%`。
- 离线回归：harness 重新链接 `audio_hook.o`（`tests/build_harness.sh` 的 natives 列表补该模块），三模式 PASS；`SUMMARY4 pass=1 total=6 vtx=2630`（`logs/framework/harness-20261026-035518.log`）。
- 文档同步：`docs/AUDIO_HOOK.md`（§5.5/§5.6、文件清单、后续）、`framework/README.md`、`docs/PLUGIN_API_V0.md`、`logs/probe/README.md`、`Workspace/README.md`。
- chunk #1+：Fusion 续流不重新 `Play`，Play 钩子只能看到 chunk #0；同一 buffer 的 SetVolume 对该 buffer 后续写入同样生效（音量是 buffer 级属性），因此实际听感覆盖整段 scream；若引擎未来新建 buffer 再 Play，chunkFp 仍可识别。此项保留为已知限制/后续可选，不再作为 P6 验收阻塞项。

## Phase 7 — ThreatTelegraphs 插件

### 设计原则

预警首先是一种只读、提示型辅助；MVP 不修改敌人 AI、不延迟攻击、不阻止死亡。预警应由经过证实的攻击前置状态触发，而不是简单根据“敌人出现在附近”猜测。

### 第一目标：Night 2 — Breaker Room

> 【v1.1】静态已证实（帧 9 事件组 58–61，待实机）：Freddy（Counter）≥ 6 且 got you == 0 → got you = 1 → 创建 jumpscare 对象 + 播放 sound #120「scream op5-7」+ #20「pop1」→ got you = 2 → Jump to frame 15「game over」。danger level 4 表示已按过 restart。预警候选信号：Freddy 值、danger level 4 状态、got you 段位（0/1/2）；必须先实机验证计数器读取时机与误报。

> 【v1.12】自然实测（pid 24092，2026-10-26 04:47）：Freddy 1→2 @04:47:47.229、4 @04:47:49.234、6 @04:47:51.238（got you=1）、interface=100 @04:47:52.740、got you=2 @04:47:54.243、death @04:47:56.062。Freddy 到 6 后约 4.8 s 死亡；建议 P7 分级为 Freddy≥3 预告、≥5 高警、≥6 或 got you==1 闪烁，具体提前量按多局样本校准。

> 【v1.14】帧 9 实测分级（2026-10-26 06:29–06:39，pid 19272/13216/1408）：Freddy 自然序列为 1→2→4→6（会跳过奇数），因此阈值取 ≥2 低警（约提前 4 s）、≥4 高警（约提前 2 s）、≥6 或 got you≥1 攻击（窗口 0.75–1.0 s）；`beat>=1` 只让房间通关后显示静默，**不是护盾**（死亡链 G58 不含 beat 条件）。

- [x] 【v1.13】危险状态与时机已确认：帧 27 Ennard[3] 状态链 0→1(靠近左)→0→5(通风口)→6(到通风口)→99(攻击)，99→death 实测仅 0.77 s；帧 9 Freddy≥6→death 约 4.8 s（v1.12 自然数据）。提示结束条件=状态离开/门关/切帧；失败条件=got you≥1 或切 death 帧。
- [x] 【v1.13】存在明确原生预攻击状态：帧 27 `Ennard.alterable[3]` 的 4/6/10 分别表示已到左门/通风口/右门（对应门未关则下一 tick 判定攻击），99=攻击已判定。低警(1–3/5/7–9)为推断的“靠近”，UI 在 debug 行标注为 inferred；状态未就绪时宁漏勿误，不显示确定方向。
- [x] 【v1.13】先用 state/recorder 验证读数与变化时刻，再上 overlay；实机 pid 24336 日志把每次 alterable 变化、alert 级别与截图对齐（见 docs/THREAT_RULES.md §6）。
- [x] 【v1.13】已提供：插件 CONFIG.enabled 总开关（默认 false）、debug 调试行、show_approach、scale 大小滑条、GUI 内“预览预警 (3 秒)”按钮；显示时长由状态驱动而非固定秒数。透明度暂未暴露，留后续。
- [x] 【v1.14】帧 9 分级已实装并实机验证：阈值 Freddy≥2 低警（黄色“Freddy 接近 n/6”）、≥4 高警（红色“Freddy 即将出现 n/6”+顶部红条）、≥6 或 got you≥1 攻击（红色“Freddy 已抓住你 / 本局结束”）；自然序列 1,2,4,6，故不用计划原设的 3/5。pid 19272 高/低警截图 `p7n2-high-freddy4-19272.png` / `p7n2-low-freddy2-19272.png`。
- [x] 【v1.14】帧 9 攻击态已抓拍：`tools/p7_attack_capture.py` 多点截图（d150/d350）得到 jumpscare + 攻击框有效证据（pid 13216，`logs/probe/p7n2-attack-13216_d150.png`/`_d350.png`）；攻击窗口 0.75–1.0 s，d550 已切 death 无效。
- [x] 【v1.14】`beat>=1` 静默边界已实测：pid 1408 写 beat=1 + Freddy=6 时插件无告警，但游戏仍约 0.5 s 后 got you=2 并死亡——G58 不含 beat 条件，beat 只是房间已完成后的显示静默，不是游戏内护盾。
- [x] 【v1.14】默认关闭回归：pid 3584 加载日志 `enabled=false debug=false`，帧 9 写 Freddy=4 无 overlay/日志；截图 `p7-default-off-3584.png`。开发临时 `enabled/debug=true` 已恢复为 false。
- [x] 【v1.15】全游戏矩阵已实装：v0.3.0 覆盖帧 8/9/10/11/13/19/27/31，帧 16/26 不做（理由见 docs/THREAT_RULES.md §3）；离线 50 条断言 + `luac -p` 通过；帧 8/10/11/13/19/31 待自然采样。

### 第二目标：Night 5 — Private Room

> 【v1.1】帧号更正：游戏内不存在名为「Private Room」的帧；Night 5 Private Room 的实现载体是 **帧 27「Final Encounter」**（1900×800，77 实例/251 事件组；Custom Night 复用帧 31「custom level」）。该帧对象包括：left/right/vent door、left is closed/top is closed/right is closed、Ennard 与 left side/right side/middle/near/going here/was here、cam 01–07、viewing cam、power left、power count down、final stage、got you、scare 1/2；死亡音效 #303「scream op5-6」。预警候选：Ennard 位置对象、门状态、final stage 计数；先实机验证再画图标。

> 【v1.12】自然实测（pid 28120，2026-10-26 05:08–05:16）：入场 hour=12/power=99/final stage=0/side=2；`side` 取 1/2/3 且是已核对计数器中唯一在死亡前持续变化的方向信号；`viewing cam` 取 1/2/5；`got you` 0→2 后约 1.7 s death；`near` 全程 0 且事件无引用，禁止用于预警；Ennard `alterable[3]==99` 表示到达门口（需先扩展 Active alterable 读取）。

- [x] 【v1.13】已确认：alterable[3] 1–3=左路靠近、4=到左门；5=通风口靠近、6=到通风口；7–9=右路靠近、10=到右门；99=攻击判定。对应门计数器 `left/right/top is closed` 用于高警抑制。
- [x] 【v1.13】已实现：`ui.viewport()` 每帧取客户区，锚点按比例计算（左 0.105w/0.55h、右 0.895w/0.55h、通风口 0.5w/0.145h、攻击中心 0.5w/0.40h），字号/框体按 `h/1440` 缩放；实测 2560×1440 与 1280×720 均正常。
- [x] 【v1.13】已实现：要求 `ctx.game.objects_ready` 且 alterables 中存在 Ennard；未就绪/场景不符/状态未知时不画方向，只允许 debug 显示缺失原因。
- [ ] 多威胁并发（帧 9 Freddy + 其他提示、同帧多方向）未测；帧 27 单 Ennard 路径的低/高/攻击天然互斥，首版未做重叠仲裁。

### 通用功能

- [x] 【v1.13】已提供：`CONFIG.debug=true` 时顶部显示 scene/Ennard[3]/LRT 门状态/got you/hour/power，并解释当前 alert 来源；`on_draw_ui` 有启用/调试/靠近提示/大小滑条。
- [x] 【v1.13】不配置固定提前量：规则完全由 `Ennard[3]`/门状态驱动；文档明确高警→99 实测约 10–12 s 且随 hour/timer 变化，99→death 仅 0.77 s，不能承诺固定 X 秒。
- [x] 【v1.13】帧 27 规则来源：帧 27 事件组 125–137（方向/到门判定）、196–202（got you/死亡链）、215–217（side 非方向）；实机证据见 docs/THREAT_RULES.md §6（pid 24336 日志与截图）。插件默认关闭。
- [x] 【v1.13】仅读 `alterables`/counters，不写游戏内存、不注入输入、不改计时/AI/胜负；overlay 只画前景，GUI 关闭时仍不拦截键鼠。

### 交付物

- [x] `plugins/ThreatTelegraphs/`（v1.13：plugin.ini + main.lua + GUI 配置/预览；默认关闭）
- [x] `docs/THREAT_RULES.md`（v1.13：状态表、规则、证据、限制、复现）
- [x] Night 5 状态记录与测试结果（v1.13，pid 24336 全链路）
- [x] Night 2 状态记录与测试结果（v1.14，Breaker Room 帧 9 分级 low/high/attack + beat 静默边界；pid 19272/13216/1408/3584；截图 `logs/probe/p7n2-*` 与 `p7-default-off-3584.png`；多局误报率仍待采样）
- [x] 【v1.15】全游戏规则矩阵与 SoftJumpscares 全局扩展：`plugins/ThreatTelegraphs/` v0.3.0（8 场景）、`plugins/SoftJumpscares/` 空白名单（7/7 scream，帧 8/10/19 实测）；`docs/THREAT_RULES.md` 270 行、`docs/AUDIO_HOOK.md` §5.7。

### 验收标准

- [x] 每条预警规则都有可复现的触发条件和退出条件（docs/THREAT_RULES.md §3/§4，含低/高/攻击的进入与退出）。
- [ ] 反复测试与误报/漏报统计：帧 27/帧 9 各完成单局全链路（pid 24336/19272/13216），多局/多分辨率仍待采样；【v1.15】全游戏 8 场景规则已实装，帧 8/10/11/13/19/31 待自然采样，限制已写入 docs/THREAT_RULES.md。
- [x] 【v1.13】游戏状态未知、版本不支持或读值校验失败时，不显示确定性的攻击预警。

### 【v1.13】Phase 7 首版进展记录（2026-10-26 06:00–06:20）

- 范围：只读威胁预警 API 与帧 27（Night 5 Final Encounter）首版插件；帧 9 Breaker Room 分级、多局误报率留待后续。
- 绘制 API：`plugin_host.h` 新增 `DrawOverlay()`；插件回调新增 `on_draw_overlay(ui)`；`overlay.cpp` 在 `DrawHud()` 之后、`ImGui::Render()` 之前调用（GUI 关闭也绘制）。新增前景原语 `ui.viewport/text_size/text_at/rect_filled/rect/line`（ImU32 颜色，物理客户区像素），见 docs/PLUGIN_API_V0.md §5.1。
- Active alterable：`state` 新增 `ObjectInfoCount/ObjectInfoName/ObjectInfoType/FindObjectInfo/ObjectInfoAlterable/FindObjectAlterable`；读取路径 rVal→rvpValues→CValue[26]（type0 long/type2 double），0-based；仅监视 `Ennard`、`flip it`，GUI 与 `ctx.game.alterables` 暴露 0..3。实测 pid 21076/24336 读到 Ennard[3] 变化。
- 插件：`plugins/ThreatTelegraphs/`（v0.1.0，API v1）。帧 27 规则：4/6/10 且对应门未关=high（边缘红条+“左门/通风口/右门 + 关门”）；1–3/5/7–9=low（黄色“靠近”）；99 或 got you≥1=attack（红框闪烁）；门已关时不提示。锚点按比例、字号按 h/1440 缩放。默认 `enabled=false`、`debug=false`，日志仅在启用/调试时写。
- 实机证据（pid 24336，日志 `logs/framework/slmodf-20261026-060601-24336.log`）：06:08:17.329 Ennard[3] 0→1 → low/left；06:08:23.345 1→0 → none；06:09:53.347 0→5 → low/vent；06:10:11.394 5→6 → high/vent；06:10:23.428 =99/gotYou=2 → attack/center；06:10:24.196 切 death 帧 14（**99→death 0.77 s**）。截图 `p7-gui-scroll-24336.png`（低警）、`p7-threat-high-vent-24336.png`（高警）；`p7-threat-attack-24336.png` 抓拍晚于切帧，**无效**。
- 默认关闭验证（pid 19872，日志 `slmodf-20261026-061207-19872.log`）：`ThreatTelegraphs loaded enabled=false debug=false approach=true`，`callbacks … overlay=1`；日志门控与绘制 `CONFIG.enabled` 判断已加。
- 文档：新建 `docs/THREAT_RULES.md`；更新 `docs/PLUGIN_API_V0.md`（on_draw_overlay/alterables/新原语/记录）、`framework/README.md`（Phase 7 行为）、`logs/probe/README.md`（产物与证据有效性）。
- 已知限制：仅帧 27；99 后仅 0.77 s；高警→99 的 10–12 s 随 hour/timer 变化；门状态为 500 ms 级快照；state 未就绪时宁漏勿误；无音效提示；overlay 依赖 D3D9 设备。

### 【v1.14】Phase 7 帧 9 分级与攻击态进展记录（2026-10-26 06:29–06:40）

- 范围：帧 9 Breaker Room 的 Freddy 分级（低/高/攻击）+ `beat` 静默边界 + 默认关闭回归；只读 counters，不改 AI/计时/胜负。
- 状态量：`Freddy` 0..6 单调危险值；`danger level 4` 重启阶段标志（0/1）；`got you` 0→1→2 死亡链；`beat` 通关标志（interface≥800 时 G63 置 1 并清零 got you）；`interface` 蓄力进度。
- 阈值与理由：pid 24092 自然序列 1→2→4→6（跳过奇数），故用 ≥2 低警（约提前 4 s）、≥4 高警（约提前 2 s）、≥6 或 got you≥1 攻击；若按计划原 3/5 会错过 4 这一档。
- 实机证据：pid 19272 high（`p7n2-high-freddy4-19272.png`，Freddy=4）与 low（`p7n2-low-freddy2-19272.png`，Freddy=2）；pid 13216 attack（06:37:23.642 Freddy 0→6 / got you=2，23.644 alert=attack，24.645 切 scene=14；d150/d350 有效）；pid 1408 beat=1 无告警但 18.773 切 death；pid 3584 默认关闭无输出 + 截图。
- 边界修正：G58（Freddy≥6 且 got you==0 → got you=1）**无 beat 条件**；G63 才是 interface≥800 且 beat==0 → beat=1、got you=0；G64 beat==1 → Freddy=5。故 beat 静默只是“房间已完成”的显示优化，不能阻止攻击。
- 限制：帧 9 只有顶部方向；攻击窗口 0.75–1.0 s；计数器为 500 ms 级快照可能跳级；多局误报率、多威胁并发、其他场景未测；插件默认关闭。
- 文档：`docs/THREAT_RULES.md` 全量重写（183 行）；`framework/README.md`、`logs/probe/README.md` 已同步帧 9 证据。

### 【v1.15】Phase 7 全游戏扩展 + P8-QoL（2026-10-26 16:20–16:45）

- 范围：①把 SoftJumpscares 从 Night 5 两个样本扩展到全部 7 个 scream；②把 ThreatTelegraphs 从帧 9/27 扩展到全游戏 8 个可读场景；③用户新增 QoL 三项（失焦解锁鼠标、密码盘小键盘、Private Room 与 Custom Night 同款快捷键）。
- 全局音频（SoftJumpscares v0.2.0）：`audio_hook.cpp` 的空白名单语义=`WhitelistAllowsLocked` 放行所有样本，`SoftenLocked` 只对 `kSoundTable[].scream` 衰减；7 个 scream 样本 86 `scream op5-8`、112 `scream op5-9`、120 `scream op5-7`、148 `scream op5-2`、257 `jump2`、265 `scream op3-2`、303 `scream op5-6` 全覆盖。实机 pid 29632：帧 8 `name="scream op5-8" handle=86` + soften readback -1204；帧 10 `scream op5-2` handle=148 + soften（同局 Static2 不软化）；帧 19 死亡 `scream op5-8` + soften（pid 30284 日志）。插件 `night5_only=false`、空白名单，UI 改“全游戏 scream”。
- 全局威胁矩阵（ThreatTelegraphs v0.3.0，948 行/38,655 B）：覆盖帧 8（distance/progress/jumpscare/backwards）、9（Freddy/beat/interface/got you）、10（distance/recharge/planned death）、11（timer/ready/win）、13（Counter/steps，默认禁用）、19（密码 death timer/chain + 回合 timer）、27（Ennard[3]+门+side）、31（Foxy/Bidy/Freddy/氧气/Minireena/Ennard）；帧 16 因唯一连续量 `windpad[0]` 不在原生 alterable 白名单、帧 26 因碰撞瞬时判定而明确不做，只保留通用锁存（got you/jumpscare/died/planned death）。同一帧只显示最高优先级一条（attack>high>low）。帧 27 `side` 修正为 1=左/2=中/3=右（G215–G217 碰撞 + pan -50/0/+50），作为 `st==0` 时的方向低警。默认 `CONFIG.enabled=false`、`scene13_enabled=false`。
- QoL 实现（native）：`state.h/cpp` 新增 `ReadCounterLive/WriteCounter/WriteAlterable`（写 counter 为 `obj+0x20A` int16 取反；写 alterable 走 rVal→rvpValues→CValue[type0]）；`d3d9hook.cpp` 新增 QoL 块——失焦/GUI 打开时 hook `user32!ClipCursor/SetCursorPos/ShowCursor` 释放鼠标（`QolShouldRelease = qolReleaseMouse && (guiVisible || !foreground)`），密码盘 `VK_NUMPAD1..9`（守卫 scene19 + Script Event==607 + prompt 1..9 + answer==0 + chain<10 + got you==0 → 写 answer），Private Room A/D/W/S（scene27 + got you==0 + hour!=6 → 写 door/button alterable + `* is closed` + cooldown，S 翻 `flip it` 0↔2）。新增配置 `qol_release_mouse/qol_keypad_numpad/qol_private_hotkeys`（默认 1）。
- QoL 实机证据：鼠标释放 pid 29632（Insert 开 GUI → `qol| mouse released gui=1 foreground=1`，关闭 → `qol| mouse captured again`；Alt-Tab 后 gui=0 foreground=0）；小键盘密码盘 pid 19316（写 Script Event=607/prompt=5 后发 WM_KEYDOWN VK_NUMPAD5 → `chain 0→1`、prompt 被消费；日志 `qol| keypad numpad digit=5 prompt=5 chain=0 vk=0x65`）；Private Room pid 29536（`flip it 0→2` 画面切到 CAM01-07 监控面板、`left/vent/right door -> closed` 后三处门指示灯红→绿；日志 `qol| private left door -> closed` 等）。合成鼠标点击（SendInput/PostMessage）实测不被游戏识别，故 Private Room 走状态注入而非点击。
- 发布清理：删除 dev 用 F10 光标冻结（`g_qolFreezeCursor` 与 WndProc 分支）；`SLMODF_VERSION` 0.4.0-p5 → **0.5.0-p8**；overlay 副标题 `(Phase 8 QoL / plugins / threat overlay)`。构建 `build.sh` 与 `tests/build_harness.sh` 通过；离线 `--wndproc-only/--imgui-only/--plugins` 三模式 PASS（`logs/framework/harness-20261026-164347/164351/164356.log`）。
- 文档：新建 `docs/QOL.md`（三功能/守卫/限制/复现）；`docs/THREAT_RULES.md` 重写为 v0.3.0 全局矩阵（270 行）；`docs/AUDIO_HOOK.md` 补 §5.7 三场景全局验证；`docs/PLUGIN_API_V0.md` 补 `ctx.game.alterables`/新 UI 原语/记录（Phase 7 时）。
- 限制：帧 8/10/11/13/19/31 的阈值来自静态分析，未实机标定（首轮自然游玩需开 debug 采样）；QoL 私室注入跳过门动画（指示灯会同步）；密码盘依赖 NumLock/帧 19/607；鼠标释放 hook 未装成时静默失效（日志 3/3 可查）。

### 【v1.16】QoL 门动画/Doorslam + 监控面板原生链路 + 预警 hour 修复（2026-10-26 22:28–23:14）

- 背景：用户 m09602 反馈「快捷键关门只有指示灯变绿，门没关、无音效」，m09926 追加「监控显示器仍是瞬间开启且无动画和打开音效」；m09965 反馈首轮通风管攻击预警未生效。root cause：QoL 只写 alterable/counter，跳过了原版事件里的 `act #17 ACT_EXTFORCEANIM`、Speaker 播放与 flip trigger 链。
- 门动画：`state::WriteAnimation(name, anim)` 写 `RunObject.rAni.raAnimForced`（对象基址 **+0x1CE**，由 `hoOffsetValue=0x242` 反推 pack(2) 布局交叉验证；写 `anim+1`，游戏 `animIn` 每 tick 消费）；关门写 12、开门写 14。实机 pid 22180：日志 `qol| anim force name=left door anim=12 ...`，内存采样 `forced=13, on=12, frames=10`，frame 1→9 播完 0.295 s 后 `forced=14, alt0=2`；开门同样 `anim=14` 播完回 `alt0=0`。截图 `logs/probe/p8-fr27-door-lamp-compare.png`（开=红/关=绿）。
- Doorslam 音效：`qol_door_sfx=1`（默认 1）；从本机解包素材 seed `Workspace/assets/sfx/Doorslam.wav`（99,858 B，RIFF PCM mono 22050 Hz 16-bit），QoL 用 `PlaySoundA(..., SND_FILENAME|SND_ASYNC|SND_NODEFAULT)` 播放；日志 `qol| door sfx path=... found=1` + `winmm| PlaySoundA ... flags=0x00020003`。发布包不含素材，缺失时静音但门功能正常。
- 监控面板（`flip it`）：原版 Group63/64 由隐藏 Active `trigger cursor` 与 `flip trigger` 矩形碰撞触发，会 Create `Active 8`、强制翻起/放下动画并播 Speaker #299 `flipup2` / #300 `putdown`。QoL S 键改为：`state::ReadObjectRect("flip trigger")` 取实时矩形 → `WriteObjectPosition("trigger cursor", center)` + `WriteCounter("trigger ready", 1)`；`QolFlipTick()` 在 Present 里重试 8 帧直到 `flip it[0]` 到达目标。实测 pid 9908：23:11:57.402 `qol| private flip request state=0 target=2` 同毫秒 `audio| play ... handle=299 name="flipup2"`，截图 `logs/probe/p8-flip-native-9908.png` 监控面板已升起；23:12:29.398 反向 request 同毫秒 `handle=300 name="putdown"`，`flip it[0]` 回 0。面板不再瞬开，动画与音效均来自原版事件链。
- 合成输入路线确认不可用：游戏每帧把光标重置到客户区中心（虚拟客户区 1707×960 = 物理 2560×1440 @150% DPI，光标恒 853,480），`SendInput`/`PostMessage` 均无法命中 `flip trigger`；故触发器改用移动游戏内部对象。
- 预警 hour 修复：帧 27 的 `hour` 从 12→1→…→6，`hour>=6` 会在 12 AM 就静默；改为 `hour==6`。离线 harness `tools/dev-scratch/tmp_tt_hour_check.lua` 用真实 `main.lua` + fake ctx 验证四用例：hour=12/st=5→low、hour=12/st=6→high、hour=6/st=5→无、hour=6/st=99→无。
- 构建/回归（23:14）：`build.sh` 成功（`bin/SLModFramework32.dll` 1,607,168 B）；`build_harness.sh` 成功；离线 `--wndproc-only/--imgui-only/--plugins` 三模式 PASS（`logs/framework/harness-20261026-231423/231424.log`）。新增 native API：`state::ReadObjectRect/WriteObjectPosition/ReadAlterableLive/WriteAnimation`。
- 文档：`docs/QOL.md` 重写 §3（门动画/Doorslam/面板原生链证据、`qol_door_sfx`、限制）、`docs/THREAT_RULES.md` `hour>=6`→`hour==6`、`framework/README.md` 与 `Workspace/README.md` 同步。
- 限制：`raAnimForced` 偏移依赖 pack(2) 对象布局（指纹门禁未覆盖对象内布局，游戏更新后需重测）；Doorslam 素材需部署时 seed、发布包不含；摄像头切换与屏幕上下选择仍需鼠标；Custom Level（帧 31）尚未由用户实测。

### 【v1.17】ThreatTelegraphs v0.4.0（Ballora 位置/倒计时、多通道、安全提示）+ Overlay 字体全字库（2026-10-27 00:2x）

- 背景：用户 m10402 三项改进——①Ballora 音乐音量平滑变化，旧规则只在「危险值」报警，损失了预警窗口；希望实时显示位置、进危险区转红并给防守倒计时；②预警在关门后立刻消失，失去态势感知；希望威胁解除后短暂显示安全提示，并允许多方向提示同屏；③自定义夜「Minireena 偷氧气」时「氧」显示为 `?`（字体缺字）。
- Ballora 帧 31 模型：`Ennard[3]` 为路径状态（0 idle；1–4 左路；5–6 通风口；7–10 右路；99 attack）；`Ballora count` 每 200 ms -1、到 0 时推进一段并重置为 50 → **每段约 10 s**，左路 1→2→4（约 20 s 到门）、通风口 5→6（约 10 s）、右路 7→8→10（约 20 s）；`current volume` 每 100 ms ±1 逼近 `set to volume`（Ennard 与 4 个重叠区决定 0/5/15/50）→ 可作「音乐接近」音量条。ETA 公式：danger 段 ≈ `Ballora count/5` 秒，到门后门开即 99→got you。插件 v0.4.0 显示「Ballora 在左路 · 音乐 5/50 · 约 10s 到门」，danger/door 转红色高警/攻击（「左门…未关！立即按 A 关门」）；帧 8 显示「Ballora 距离 d/600 · 正在回退/还在靠近 · 约 Xs 到死亡线」（distance 由 W 键 +1/100 ms、松开 -1/30 ms，>600 且 dancer[0]==0 触发 jumpscare）。
- 多通道/安全提示：`compute_alerts` 返回按方向去重的列表（attack 全屏、其它方向按锚点并列）；`update_safe` 在之前 high/attack 的方向消失后保留 `safe_notice_seconds=2.5` 帧显示绿字「左门安全/右门安全/通风口安全」，与其它方向威胁同屏。
- 字体：`overlay.cpp` 的 `TryLoadCjkFont()` 改用 `GetGlyphRangesChineseFull()`（约 2 万字），新增 `font_path`（空=自动候选 msyh.ttc 等）与 `font_size=14`（10–40）；`io.Fonts->Build()` 后用 `FindGlyphNoFallback` 探测 U+6C27，日志 `font_atlas=2048x4096 glyph_oxygen=1`。
- 实机证据（pid 20584，log `slmodf-20261027-001644-20584.log`）：帧 31 自然推进 `00:20:58 Ennard[3]=1 count=49 vol=1 alert=low/left` → `00:21:07 [3]=2 vol=5 alert=high/left` → `00:21:17 [3]=4 alert=attack/left` → `00:21:26 [3]=99 gotYou=2`；另一路径 `00:22:30 [3]=7 low/right → 00:22:39 [3]=8 high/right → 00:22:48 [3]=10 attack/right`。截图 `logs/probe/p9-sc31-low-002058.png`、`p9-sc31-high-002107.png`、`p9-sc31-attack-002117.png`（全屏攻击+中心 Minireena 黄字同屏）、`p9-sc31-safe-002759.png`（写 `left is closed=1`+`Ennard[3]=0` 后绿字「左门安全」）、`p9-sc31-oxygen_d350.png`（「氧气不多了」的「氧」正常）、`p9-sc8-low_d150.png`/`p9-sc8-high_d150.png`（距离 400/600 与 557/600）。
- 构建/离线：`build.sh` 成功（`bin/SLModFramework32.dll` 1,609,216 B）、`build_harness.sh` 成功；离线三模式 PASS；插件离线断言 `tools/tt_offline_check.lua` pass=10（危险 ETA、安全提示、多通道、帧 8 位置行、GUI 安全滑条 0 禁用）。
- 文档：`docs/THREAT_RULES.md` §13（v0.4.0 显示层/证据/配置/限制）、`docs/OVERLAY_INPUT.md` §8（字体全字库/自定义字体/显存）、`framework/README.md`、`Workspace/README.md`、`logs/probe/README.md`。
- 限制：帧 31 的 A/D/W 原生键处理仍只在帧 27（场景 31 需内存写门状态，QoL 未扩展）；warp 场景 31 会唤醒 AI，约 40–90 s 后死亡转 Custom menu；Ballora ETA 由 `Ballora count` 推算，计时器暂停/切场景时可能跳变；全字库图集 2048×4096（约 32 MB），低显存环境可用 `font_path` 子集字体。
### 【v1.18】ThreatTelegraphs v0.4.1 — Ballora 时序模型修正（2026-10-27 01:0x）

- 背景：task-4 静态研究 `Workspace/research/p7-global/ballora-timing.md`（575 行，实机日志复核）推翻 v0.4.0 的两条假设：①到门 4/10 门开并不立即 attack，而是要等下一脉冲才 99——v0.4.0 把最有价值的约 8–9 s 关门窗口误报成「已判定」；②ETA 不是 `Ballora count/5`（多算了 `Ballora Diff`），正确周期为 `(50 − Ballora Diff)×0.2 s`、剩余为 `clamp(count−Diff,0,50−Diff)×0.2 s`。
- 修正后的分级：4/10 + 门开 → **high + 实时倒计时**（remaining ≤ 2 s 升级「马上关门」）；4/10 + 门关 → 该通道静默（1.25–5.01 s 内撤退）；3/9 → high；2/8、1/7 → low；attack 只保留 `got you ≥ 1`（99 与 got you=2 同帧，基本观测不到）。通风口 5/6 在帧 31 无事件组置位（死代码），不再映射。
- 入场伪值门控：刚 warp/加载时 `Ballora Diff` 可能是垃圾值（实测 12464，持续约 47 s），插件要求连续 0.5 s 读到稳定 1..20 才启用 Ballora 规则，否则静默并只保留 got you 兜底。
- 文件：`plugins/ThreatTelegraphs/main.lua` → v0.4.1（`PLUGIN_VERSION`、`BALLORA_PATHS` far/mid/near/door、`update_ballora_gate`、`ballora_info/alert` 重写、`compute_telemetry` 位置行、`compute_scene31` 移除死代码分支、GUI 两个新滑条、CONFIG `scene31_ballora_urgent=2`）；`plugin.ini` version=0.4.1；`docs/THREAT_RULES.md` §14（模型/改动/证据/限制）；`tools/tt_offline_check.lua` 重写。
- 离线验证：`main.lua` 语法 OK；`tt_offline_check.lua` 30 项断言 **pass=30 fail=0**（door 倒计时 8 s/1 s、3/9 high、5/6 静默、垃圾 Diff 静默后重武装、Diff=13 周期、多通道、hour6 静默、got you attack、safe 开关）。无 native 代码改动，无需重建 DLL/harness。
- 限制：帧 31 仅实测 Angry Ballet 预设（Diff=3），其它自定义预设未实测；新 Lua 需重启游戏或重载插件才生效；`current volume` 滞后约 3.5 s，只作 UI 提示。

### 【v1.19】ThreatTelegraphs v0.5.x：帧 31 Freddy 精确门/语音字幕 + Bonnet 行走倒计时 + 动态对象读取（2026-10-27 02:4x–03:1x）

- 背景：用户 m11423 要求先用自动化仿真成果完善预警插件：①Funtime Freddy 攻击前有明确语音提示，最终由 Bon-Bon（#303 scream op5-6）触发 jumpscare，因此需要字幕系统；②Bonnet 在放下监控时刷出、从右往左走，走完 jumpscare，处理方式是点它的鼻子；并要求之后回到框架主线（P8），两个插件转为示例插件延后完善。
- 帧 31 Freddy 状态机（frame_31_custom level_events.txt 实读）：Freddy.alterable[0]=侧别(1 左/2 右)、[1]=语音序号 1..4、[2]=在场计数(250)、[3]=攻击语音变体 1..3、[6]=阶段(0 idle/1·2·4 GoGetHim 播放中/3=到同侧门/5=跨到对侧门)；Group319/324 入场播 #350 Freddy_Intro，Group326-329 播 #351-#354、Group333-336 播 #355-#357 并置 alt6=1/2/4，Group341-343 播完置 alt6=3/5，Group344-347 按 alt6+alt0+门开创建 Bonnie scare x105 + #303 + got you=3，Group348-351 关门播放 #358 并 alt6=0。
- 插件 v0.5.0：Freddy 规则读 alterable（精确侧别/阶段），phase 3/5 + 门开→high「Freddy 在左/右门！立刻关」、门关→low「被挡住」、phase 1/2/4→low「要出手了」、alt2>0→low「Freddy 在说话」；旧 Freddy count 阶梯保留为读不到 alterable 的回落。Bonnet 规则：Active 19 实时位置 + (x−(-230))/(180×N) ETA（N=1+(Diff≥5)+(Diff≥10)+(Diff≥15)），flip==2 低警「在监控画面里·放下后点鼻子」、flip 0/3→high「约 Ns」/「马上点鼻子」、alt1==1/alt2>0→「已被打中」。
- 状态字幕（试点）：不依赖音频指纹（392 样本中 214 个 dump 是 MP3 帧，现有 178 条指纹表无法识别 Freddy 语音），改用 alterable 索引→固定文本（alt[1] 1..4、alt[3] 1..3、alt[6]），底部居中黑底白字，subtitle_seconds=2.5 保持；GUI 可开关。字幕系统本身作为框架级 Phase 9 提案写入本计划（见下）。
- 动态对象读取（native）：ReadObjectRect/ObjectRval/WriteObjectPosition 在 published 快照查不到对象时回退遍历 RunHeader+0x1278 rhObjectList（镜像结构体 + #pragma pack(2) 校准：rhApp 0x18/rhFrame 0x1C/rhQuit 0x30/rhQuitParam 0x38/rhNumberOi 0x54/rhOiList 0x58 全部吻合 → rhObjectList=0x1278、rhNObjects=0x3C；每项 8 字节，hoOi 是句柄）；新增插件 API ctx.game.object_rect(name) / ctx.game.alterable(name,index)（只读）；回退日志 state| live rect fallback 限 8 行预算。构建 bin/SLModFramework32.dll 1,611,776 B、harness 1,509,376 B（03:01/03:02），离线三模式 PASS。
- 实机证据（pid 23520，log slmodf-20261027-024951-23520.log）：帧 31 注入 Freddy alt0=1/alt1=2/alt2=250 → 02:50:47 alert=low dir=center reason=Freddy alt1=2 alt2=236 + 截图 p9-v050-freddy-subtitle2.png（底部字幕「Freddy: Are you ready or not?」）；注入 alt6=3/alt0=1/left closed=0 → 02:52:09.098 alert=high dir=left reason=Freddy alt6=3 alt0=1 closed=0（用户要求的 Freddy 精确门），同毫秒 got you=3 → attack。pid 29848：Bonnet v0.5.1 实机 03:07:32.852 bonnet_dbg flipPub=2 flipLive=0 → 03:07:32.853 alert=high dir=center reason=Bonnet x=1201 eta=2.0 speed=720（budget 内 8 行 state| live rect fallback name=Active 19 obj=1D3E7838）。
- v0.5.1 修正：Bonnet 的 flip it 改从 ctx.game.alterable 实时读取——published alterable 快照滞后（实测 flipPub 仍 2 而 flipLive 已 0），否则会一直停在「监控画面里」的低警，永远不报 high。
- 离线验证：tools/tt_offline_check.lua 扩展 S/T 组断言（Freddy phase3 左门 high、关门 hold low、phase5 跨右 high、语音字幕、字幕过期、无 alterable 回落；Bonnet ETA/紧急/监控/打中/对象缺失静默），pass=64 fail=0；lua 语法检查通过。
- 限制：Bonnet 生成是 random(30) < Bonnet Diff 概率事件（实测 diff=30 spawn 1.9–2.2 s；diff=3/12 在 20 s 内未生成），本次仅有日志级 high 证据、尚无 Bonnet 行走高警截图；Freddy alt6 语义只在帧 31 验证；字幕为固定文本映射，音频驱动字幕仍受 MP3 指纹缺失限制；插件默认关闭、测试构建以 enabled=true 运行。

## Phase 8 — 稳定性、发行打包与首次公开发布（【v1.21】P8.1–P8.5 完成，P8.6 待发布）

> 【v1.20】本节由用户草案 `Workspace/Phase8.md`（211 行）整合进主计划，并把用户 m12195 的拍板固化为可执行条款：安装布局 B、MIT + 公开源码、GitHub 首发 v1.0.0、文档中文为主+英文摘要、首发只带 HelloPlugin。审阅阶段追加的 6 处补充也已并入：§0 原则 8「插件优先」、P8.0 插件 API v1 冻结、P8.1 构建硬证据（objdump 导入表 + 离线三模式）、P8.2 日志轮转/诊断包/配置重置规格、P8.3 素材审计清单、P8.4 游戏更新后的用户可见行为。
>
> Phase 8 不新增一般玩法功能，不重写已经过实机验证的模块；只修发行阻塞缺陷、补文档/许可/安装包。示例插件功能完善不阻塞首发。

### 【v1.21】Phase 8 收尾记录（2026-10-27 06:00–07:00）

- **P8.1 可复现构建**：链接加 `-Wl,--no-insert-timestamp`、移除 DLL 启动日志的 `__DATE__/__TIME__` 后，DLL/launcher/harness 连续两次独立构建字节级一致（哈希见 §7.1）；`objdump -p` 确认仅依赖系统 DLL + `api-ms-win-crt-*`；离线四模式（`--wndproc-only` / `--imgui-only` / `--plugins` / `--p8-only`）全 PASS。
- **P8.2 稳定性**：日志轮转（默认 4 MiB×5，环境变量 `SLMODF_LOG_ROTATE_BYTES` 可覆盖）、`ExportDiagnostics()` 诊断包（summary + 配置 + 日志 + sidecar → `diagnostics-<ts>.zip`）、`ResetConfigAndReload()` 配置重置（`.bak-<ts>` + 默认模板重载）、插件连续 30 次运行时错误自动禁用并写回 `enabled=0`。离线 `--p8-only` + 实机（轮转 env=4096；AutoDisableProbe 30 帧自动禁用）取证，详见 `docs/P8_STABILITY.md`。
- **P8.3 许可/审计**：`LICENSE`（MIT，Copyright (c) 2026 KIRARITenshiChan）、`THIRD_PARTY_NOTICES.md`（Dear ImGui MIT / MinHook BSD-2 / Lua 5.4.7 MIT / MinGW GCC 运行库 GPLv3+RLE）、`docs/P8_ASSET_AUDIT.md`；发行包不含游戏派生素材、recon/dump/日志。
- **P8.4 文档**：`docs/release/README.md`、`INSTALL.md`、`UNINSTALL.md`、`TROUBLESHOOTING.md`、`CHANGELOG.md`、`PLUGIN_API.md`（中文为主 + English summary）；`tools/make_release.sh <version>` 白名单组包 → `release/BUILD_MANIFEST.sha256`（18 文件）+ 确定性 ZIP + `SHA256SUMS`（固定 mtime、ZIP 内路径前缀 `SLModFramework/`）。
- **P8.5 演练**：发行 ZIP 干净安装 → 首跑自动生成 `config/framework.ini` 与 `logs/framework/*` → HelloPlugin loaded=1 → GUI 可滚动（修复高 DPI 下内容超高：窗口限高 + 滚动）→ 点击「导出诊断包」「恢复默认配置」均实测生效 → 删除 `SLModFramework` 后原版直跑无框架模块/无日志 → 最终 ZIP 复装复验。证据 `docs/P8_ACCEPTANCE_DRILL.md`、截图 `logs/probe/p85-final-gui-top.png` / `p85-final-gui-bottom.png` / `p85-original-uninstalled.png`。
- **产物（v1.0.0 冻结）**：DLL 1,617,920 B / `fd83f195…`；launcher 31,744 B / `22daac7f…`；harness 1,517,056 B / `248ab25b…`；发行包 `release/SLModFramework-1.0.0-win-x86.zip` 805,520 B / SHA-256 `f8932e25…`（18 文件）。版本标识四处一致（DLL 日志 / `init-<pid>.done` / GUI 标题 / launcher 日志）。
- **发布门槛**：11 条中 10 条达成，仅剩「GitHub Release v1.0.0 已发布」；本轮未新增玩法功能，示例插件与 Phase 9 按 m12162/m11423 延后。

### 8.0 首发范围冻结（P8.0 输入）

**首发交付内容（v1.0.0）：**

- 框架启动器 `launcher32.exe` 与 32 位框架 DLL `SLModFramework32.dll`。
- 已实现的 GUI、插件宿主、Lua API v1、配置、日志、状态读取与场景识别。
- 内置 QoL：失焦/开 GUI 解锁鼠标、密码盘小键盘、Private Room A/D/W/S 快捷键、门动画与 Doorslam 音效、监控面板原生翻起/放下。
- HelloPlugin 基础示例（随包；两个功能示例插件不随包）。
- 第三方许可证文本、用户文档、版本信息（MIT 项目许可）。

**暂缓内容（不阻塞 P8）：**

- SoftJumpscares / ThreatTelegraphs 的进一步功能完善；作为独立示例插件后续发布。
- 独立字幕系统（Phase 9）、通用外部音频替换、其他新增玩法功能。
- 多游戏支持、未验证游戏版本支持、大规模框架重构。
- 非注入（HUD-only）绘制模式：需要另一套绘制架构，列为已知限制，不在 1.0 实现。

不得为凑齐发行功能而重构现有内置 QoL。

### P8.0 — 范围冻结与现状盘点（【v1.21】完成：`docs/P8_STATUS.md` / `docs/P8_KNOWN_ISSUES.md`）

#### 任务

1. 盘点源代码、构建产物、配置、插件、文档与依赖目录现状。
2. 为每项功能标记：已实现 / 已实机验证 / 仅离线验证 / 尚未验证 / 已延期。
3. 检查启动器、DLL、GUI、README 的框架版本标识是否一致；区分框架版本（`SLMODF_VERSION`）与插件 API 版本（v1）。
4. 记录内置 QoL 的配置开关、默认状态、适用场景、已知限制与失效行为。
5. 冻结插件 API v1：P8 期间不新增破坏性接口；确需扩展时只做向后兼容追加，并同步 `docs/PLUGIN_API_V0.md`。
6. 把 SoftJumpscares / ThreatTelegraphs 的功能完善单独登记到后续任务，与发布验收隔离。

#### 交付物

- 发布范围清单（并入本节与 `docs/P8_STATUS.md`）。
- 当前实现与验证状态表（功能 × 证据 × 限制）。
- Phase 8 已知问题清单（`docs/P8_KNOWN_ISSUES.md`）。

#### 验收标准

- 首发包含/不包含什么已经明确，不把计划功能误报为已实现。
- 后续任务不会自动扩大首发范围。
- 版本标识三处一致：DLL 日志、`init-<pid>.done`、GUI 标题。

### P8.1 — 可复现构建与发行包（【v1.21】完成：字节级可复现构建 + 最终发行 ZIP）

#### 任务

1. 从干净源代码目录执行正式构建；不直接把开发目录中未确认的二进制当作发行产物。
2. 确认构建目标为当前支持的 32 位 x86 游戏环境（winlibs i686-mingw-w64，`bash build.sh`）。
3. 运行离线测试三模式：`--wndproc-only` / `--imgui-only` / `--plugins`。
4. 核对启动器与 DLL 的版本号、API 版本、配置路径、插件路径、日志路径。
5. 按代码实际路径规则确定发行包目录结构（禁止猜测）；【v1.20】固定为布局 B：

```
<game>\\
  SisterLocation.exe            (原版，不变)
  SLModFramework\\               <- 框架根 = bin 的父目录
    bin\\launcher32.exe
    bin\\SLModFramework32.dll
    config\\framework.ini(.example)
    plugins\\HelloPlugin\\
    logs\\framework\\ logs\\state\\
    assets\\sfx\\                 (可选，用户自备素材)
    docs\\  LICENSE  THIRD_PARTY_NOTICES
```

- 框架根 = `launcher` 所在 `bin` 的父目录；游戏根 = 框架根父目录，启动器据此拼 `SisterLocation.exe` 并校验存在。DLL 通过 `GetModuleFileNameA(self)` 剥两级得到框架根；config/plugins/logs/sfx 全部相对框架根，运行时不在游戏根写入文件。
- 开发目录 `Workspace/` 保留为构建/记录/取证目录；`framework/deploy.sh`（`--with-docs` / `--with-sfx`）负责把 bin + 配置模板 + HelloPlugin + 文档复制到安装目录。`Workspace/logs/runtime` 为指向 `<game>\\SLModFramework\\logs` 的目录 junction，仅开发期便于查看运行日志。
6. 制作独立发行 ZIP：只含 `bin/`、`config/framework.ini.example`、`plugins/HelloPlugin/`、`docs/`、`LICENSE`、`THIRD_PARTY_NOTICES`、`README.md`；不含开发日志、状态 JSONL、内存/资源 dump、recon 工具产物、`assets/sfx/Doorslam.wav` 等游戏派生素材。
7. 记录构建命令、工具链版本、产物清单、SHA-256 与离线测试日志。
8. 【v1.20】构建硬证据：`objdump -p` 确认依赖仅系统 DLL + `api-ms-win-crt-*`（无 libstdc++/libgcc/winpthread 运行库）；DLL/launcher 字节数与哈希写入 `docs/P8_BUILD.md`；harness 重建后再跑三模式。

#### 验收标准

- 干净源目录可重新生成发行包；包内无开发文件/游戏原版资源。
- 包内目录结构与程序实际读取路径完全一致（P8.5 用发行 ZIP 实测）。
- 包内二进制、版本标识与测试报告相互对应。
- 【v1.21】最终输入：2026-10-27 06:55 复现构建 DLL 1,617,920 B / `fd83f195…`、launcher 31,744 B / `22daac7f…`、harness 1,517,056 B / `248ab25b…`；离线四模式（wndproc/imgui/plugins/p8-only）PASS；发行 ZIP `release/SLModFramework-1.0.0-win-x86.zip` 805,520 B / SHA-256 `f8932e25…`（18 文件），P8.5 干净安装→卸载→原版对照演练通过（`docs/P8_ACCEPTANCE_DRILL.md`）。

### P8.2 — 稳定性与失败处理（【v1.21】完成：日志轮转/诊断包/配置重置/插件自动禁用）

#### 任务

1. 验证正确版本可启动框架；错误版本或被替换 EXE 被明确拒绝（指纹门禁 exit 3，不启动游戏，错误信息含期望/实际 SHA-256）。
2. 版本门禁失败时不得使用未经验证的地址/对象布局；GUI 显示 unsupported，依赖内部状态的插件禁用。
3. 验证插件语法错误、运行时异常、禁用状态、API 版本不兼容时不会拖垮框架或刷屏（每插件错误归属到 plugin id + 回调）。
4. 验证配置缺失、损坏、非法值的恢复：损坏文件备份为 `framework.ini.bad-<ts>` 后写默认模板；GUI 提供配置重置入口。
5. 关键初始化错误、Hook 失败、状态读取失败、插件异常都有可定位日志。
6. 关键状态/配置落盘不依赖进程退出回调（native WM_CLOSE 原生缺陷 0xC0000005 已知，测试以原版对照判定，不算框架回归）。
7. 回归：设备 Reset、场景切换、GUI 开关、Alt+Tab、最小化、全屏/窗口化、分辨率变化、反复启动退出。
8. 无法可靠支持的情形写限制，不用未经验证的替代 Hook。

#### 诊断与日志（【v1.20】规格）

- 日志轮转：单文件上限 4 MB，最多保留 5 个历史文件（`slmodf-*.log` 按时间轮转；启动时清理超限旧文件）。
- 诊断包导出：GUI 按钮生成 `diagnostics-<ts>.zip`（版本清单 + 配置 + 最近日志 + 插件摘要）；默认不含内存 dump/游戏资源，压缩前记录文件清单，文档提示分享前检查本地路径等个人信息。
- 配置重置：GUI 一键把 `config/framework.ini` 备份为 `.bak-<ts>` 并写回默认模板；等价手工步骤写入 TROUBLESHOOTING。
- 插件错误可定位到 plugin id / 回调 / 帧号；同一插件连续错误自动禁用（日志记 `plugin| auto-disabled`）。

#### 验收标准

- 错误版本安全拒绝；插件错误可定位且不影响其他插件；配置损坏可恢复；日志不会无限增长。
- 未完成的兼容性测试全部进入已知问题清单。

### P8.3 — 第三方许可证与资源审计（【v1.21】完成：MIT + 第三方声明 + 素材审计）

#### 任务

1. 盘点正式产物实际包含/链接的全部第三方依赖与版本：Dear ImGui 1.91.9b（MIT）、MinHook（BSD-2）、Lua 5.4.7（MIT）、mingw-w64 运行库（静态链接，按其许可保留声明）。
2. 发行包包含 `LICENSE`（项目 MIT）、`THIRD_PARTY_NOTICES`、各依赖许可证全文与鸣谢。
3. 确认仅作研究工具的 AGPL/GPL 项目（ctfak-cpp、CTFAK、社区 GPL D3D9 overlay）未被复制、链接或随框架分发。
4. 素材审计清单（逐项确认不进公开发行包）：`assets/sfx/Doorslam.wav`（游戏派生，用户自备；缺失时 QoL 静默不播）、`sound_table.h`（由本地 dump 生成的指纹表，仅哈希/前缀）、`analysis/data/*`、`recon/**` dump、`logs/**`、`docs/` 中引用的游戏文本/字幕表（`Workspace/data/subtitles.json` 仅本地）。
5. 项目许可证定为 MIT 并公开源码仓库（用户 m12195）；仓库含源码、构建脚本、第三方许可证，不含游戏资源与个人数据。
6. 明确可选 Doorslam.wav 缺失时的行为：不播放、不报错、日志记一次 `qol| door sfx missing`。

#### 交付物

- `LICENSE`、`THIRD_PARTY_NOTICES`、第三方版本清单（并入 `docs/P8_BUILD.md`）。
- 发行资源检查记录。

#### 验收标准

- 每项随包/链接依赖都有可核对许可记录；无原版游戏资源混入；公开源码授权范围明确（MIT）。

### P8.4 — 用户文档（中文为主 + 英文摘要）（【v1.21】完成：`docs/release/` 六份）

#### 必需文档

1. `README.md`：用途、能力、支持范围、已知限制（中文为主，附英文摘要）。
2. `docs/INSTALL.md`：安装步骤（解压到 `<game>\\SLModFramework`）、启动方式、首次运行检查。
3. `docs/UNINSTALL.md`：停止游戏、删除框架目录、恢复配置、Steam 文件验证后的行为。
4. `docs/TROUBLESHOOTING.md`：版本不匹配、GUI 不出、插件未加载、配置损坏、日志/诊断包位置、配置重置。
5. `docs/CHANGELOG.md`：首发内容与已知问题。
6. 插件 API 文档（沿用 `docs/PLUGIN_API_V0.md`，补 HelloPlugin 入门）与生命周期说明。
7. 第三方依赖与许可证说明（`THIRD_PARTY_NOTICES` 指引）。

#### 内容要求

- 以第一次使用的普通玩家为主要读者；解释操作与现象，不要求理解内存偏移/Hook/Fusion 结构。
- 声明支持的游戏构建：EXE SHA-256 `c60e1c79...2b2c73`、Fusion build 286、PC v1.121、Steam buildid 1497593。
- 区分框架内置功能与插件；说明 Steam 文件验证、游戏更新、框架关闭与彻底卸载的影响。
- 【v1.20】游戏更新后的用户可见行为：启动器哈希不符 → 显示期望/实际指纹并以退出码 3 拒绝启动（不注入、不写游戏状态）；文档给出手动核验与新版本适配流程，不承诺静默降级。
- 未验证功能明确标记；不使用"完全兼容"等绝对化措辞。
- 安全软件：说明注入式框架可能被误报；1.0 只记录本机 Windows Defender 实测结论，其他引擎未测试须明示未测。

### P8.5 — 干净环境安装、使用与卸载演练（【v1.21】完成：发行 ZIP 安装→GUI→卸载→原版对照）

#### 测试准备

建立独立测试目录，确认无开发工作区文件、缓存、日志、预生成配置或 junction。

#### 验证流程

1. 只从最终发行 ZIP 解压安装到 `<game>\\SLModFramework`。
2. 启动器识别目标游戏版本并注入。
3. 启动游戏，确认 init 成功、版本/路径日志正确（framework= / game_dir= / exe=）。
4. 呼出/关闭 GUI，验证输入焦点与鼠标释放正常。
5. 启用/禁用 HelloPlugin，重启后状态保持。
6. 验证内置 QoL 开关、实际效果与素材缺失时行为。
7. 模拟错误插件与损坏配置，确认自动禁用/恢复。
8. 检查日志足以说明初始化结果与失败原因。
9. 按正式文档执行卸载（删除 `SLModFramework`；确认游戏根无框架写入文件；Steam 文件验证不改变原版）。
10. 无框架重启游戏作为原版对照。

#### 验收标准

- 安装/卸载无需开发环境或手工修补；关键功能通过发行包运行而非开发目录。
- 卸载后恢复原版状态；测试结果、日志、包哈希与未解决问题均有记录。

### P8.6 — 发布候选版与最终验收（【v1.21】进行中：P8.1–P8.5 输入齐备，待 GitHub Release v1.0.0）

#### 任务

1. 冻结候选版本，验收期间不加新功能。
2. 按 P8.0–P8.5 结果更新文档与已知问题。
3. 生成最终 ZIP、SHA-256、版本号、变更记录；`SLMODF_VERSION` 冻结为 `1.0.0`（去掉 `-p8` 阶段后缀）。
4. 源码仓库与二进制发行包的版本、依赖、构建记录一致；打 tag `v1.0.0` 并发布 GitHub Release（ZIP + SHA-256 + 源码；推送身份 KIRARITenshiChan）。
5. 对注入方式的杀软告警做风险评估：仅本机 Defender 实测，其余引擎未测须写明；不把未测项写成兼容。
6. 未能在当前环境完成的兼容性测试标记为未验证/不支持。
7. 所有阻塞问题解决后才标记正式公开发行。

#### 最终发布门槛（【v1.20】合并草案 11 项与历史验收项）

- [x] 干净构建成功，离线三模式 PASS，objdump 依赖证据齐全。
- [x] 正确版本可启动框架；错误版本/被替换 EXE 被明确拒绝且不启动游戏（exit 3 负例）。
- [x] 框架正常启动、GUI 可呼出，退出行为与原版对照（0xC0000005 属原版缺陷，不算回归）。
- [x] 插件可独立启停；损坏插件被禁用并有日志；插件错误不使其他插件失效。
- [x] 配置持久化与损坏恢复、配置重置、日志轮转、诊断包导出可用。
- [x] 内置 QoL 主要路径已验证（含素材缺失降级），开销可接受。
- [x] 安装/更新/卸载/回滚文档完整；干净目录安装→卸载演练通过。
- [x] 第三方许可证与素材审计通过；包内无原版游戏资源；项目 MIT 生效。
- [x] 已知问题与未验证兼容性公开记录（含无 HUD-only 模式、游戏更新指纹拒绝行为）。
- [ ] 发行包、SHA-256、版本号、变更记录一致（`release/SLModFramework-1.0.0-win-x86.zip` 805,520 B / SHA-256 `f8932e25…`、`BUILD_MANIFEST.sha256` 18 文件、版本 1.0.0 四处一致）；**GitHub Release v1.0.0 待发布（P8.6 唯一剩余项）**。
- [x] AGPL/GPL 合规检查通过（无 AGPL 代码进入框架产物）。

### 明确延期事项

以下不作为 Phase 8 阻塞项：SoftJumpscares 进一步完善；ThreatTelegraphs 额外场景/多局误报率与漏报率；独立字幕插件（Phase 9）；通用外部音频替换；未验证的游戏版本与其他游戏支持；需要另一套绘制架构的非注入 HUD 模式。

若发现会导致框架无法启动、无法恢复原版、错误版本误写游戏状态或插件错误破坏运行环境的问题，重新纳入 P8 阻塞项。

**完成定义：** Phase 8 结束时交付一个版本明确、可复现构建、可安装、可使用、可排错、可卸载的框架发行包（v1.0.0），不要求示例插件已具备最终功能集。

## Phase 9 — 字幕系统（设计提案，【v1.19】新增）

> 用户 m11423 提出「字幕系统可能会需要被加入到计划中」。本节只定义范围与接口方向，不改变 Phase 8 优先级；实现排在框架发布主线之后。

### 目标与边界

- 在 overlay 上显示游戏语音/环境音的字幕（Funtime Freddy 语音、Bon-Bon jumpscare、关键警报），支持开关、字号、行数、保持时间与位置；不修改游戏资源、不改变配音播放。
- 首选状态驱动字幕：由插件把游戏状态（alterable/counter 索引）映射为固定文本。理由：392 个音频样本中 214 个是 MP3 帧，现有 DirectSound 指纹表（178 条）无法可靠识别；MP3 解码后的 PCM 与 Fusion mp3flt 是否逐字节一致未验证。
- 音频驱动字幕作为后续可选路径：需要把 MP3 样本解码为 PCM 并纳入指纹表（tools/gen_sound_table.py 扩展 ffmpeg 解码），或 hook mp3flt 解码输出。

### 框架级接口（草案）

- 插件 API：ctx.ui.subtitle(text[, seconds]) 入队/替换单行字幕；ctx.ui.subtitle_clear()；框架负责渲染、去重（同文本不重复刷新）与超时。
- 内置字幕渲染：底部居中、黑底白字、可配 subtitle_seconds（默认 2.5）、字体沿用 overlay 全字库；多行上限 2–3 行；GUI 总开关 subtitles=1。
- 数据侧：ThreatTelegraphs 已用状态字幕试点（Freddy alt1/alt3/alt6 → 文本）；Phase 9 把同样模式交给独立 plugins/Subtitles，并预留样本名→文本表（Workspace/data/subtitles.json，仅用户本地可用，发布包不含游戏语音文本）。
- 验收：至少 3 段 Freddy 语音 + Bon-Bon jumpscare 在自然/注入两路可复现；字幕与语音时序偏差 < 0.5 s；关闭后零开销；先不引入新 native hook。

### 状态

- 设计提案已登记；实现在 P8 发布主线完成后启动；ThreatTelegraphs 的状态字幕作为临时示例继续可用。

## 4. 错误处理与安全边界

- **游戏版本不匹配：** 禁止启用依赖内部数据的状态插件；GUI、日志等独立功能可按实际兼容情况保留。
- **状态读取异常：** 返回 `unknown`，记录诊断信息，不继续沿用上一个场景的陈旧数据。
- **脚本异常：** 捕获异常，禁用出错插件或隔离当前回调，不让错误持续刷屏。
- **Hook 安装失败：** 明确报告失败原因；禁止不经验证地尝试其他函数地址。
- **GUI 初始化失败：** 保留安全退出路径，不允许后台线程永久卡住游戏。
- **补丁失败或校验不一致：** 不覆盖现存文件；提供恢复说明。
- **日志隐私：** 默认不收集与问题诊断无关的个人路径/账户信息；分享日志前提示可能包含本地路径。
- 【v1.1】**注入/探针可恢复：** 注入器与探针必须支持一键卸载/恢复原状，失败时不残留线程或 Hook 残留；探针不得写游戏内存。
- 【v1.1】**Hook 目标校验：** 安装 Hook 前校验目标模块归属与函数序言/AOB，版本不符立即放弃并报告，不尝试其他地址兜底。
- 【v1.1】**音频 Hook 旁路：** 音频 Hook 异常时必须旁路恢复原播放路径，不得静音、不得卡死游戏。
- 【v1.2】**原生退出崩溃：** 检测到窗口关闭/设备丢失时先保存状态并停止绘制；不得依赖 DLL_PROCESS_DETACH 完成关键落盘；测试用原版对照判定。
- 【v1.2】**运行时地址随机：** 任何内存/Hook 目标地址必须来自模块基址 + vtable/导出解析；启动时校验模块存在与版本；解析失败一律降级为 unknown 并禁用对应功能。
- 【v1.6】**调试 warp 边界：** 帧跳转入口仅由框架 debug 通道触发（`debug=1` + 日志），不暴露给插件 API/常规界面；warp 前必须记录当前帧号与目标帧号。
- 【v1.6】**warp 状态非自然：** 通过 warp/Extras 进入的场景，其计数器初值可能与自然游玩不同；采集记录必须标注进入方式（natural/extras/warp），最终结论须至少一次自然游玩复核。
- 【v1.6】**存档改动可恢复：** 任何改动 `%APPDATA%\MMFApplications\sl` 的操作必须先备份原文件并记录哈希，提供一键恢复；失败时不得留下半写状态。

## 5. 每次交给 Coding Agent 的工作规范

每个子任务都按以下流程推进：

1. 先读本计划和当前 `docs/`，检查仓库现状，避免重复实现。
2. 先调查证据和不确定点，再改代码；凡是涉及地址、偏移、Hook、数据布局的结论都要留下验证依据。
3. 一次只推进一个里程碑或一项明确任务，不顺手重构无关模块。
4. 修改前列出准备改动的文件、预期行为与验收方式；完成后说明实际改动、构建命令、测试结果和未解决问题。
5. 如果缺少本机运行环境，明确标记为“静态完成，未实机验证”，不得声称运行成功。
6. 不要生成大量猜测性代码来掩盖未知信息。遇到关键未知时，先编写探测工具、最小 PoC 或调查报告。
7. 每个阶段更新对应文档，并保持 README 的当前状态与实际实现一致。
8. 任何失败都保留最小复现步骤、日志和相关指纹，便于下一轮 agent 接手。
9. 【v1.1】每次修改计划或文档前，先把旧版备份到 Workspace/plan-backups/（时间戳+版本号，SHA-256 记录），并在 plan-changes/CHANGELOG.md 追加记录。
10. 【v1.1】所有工作痕迹（脚本、日志、笔记、dump、报告）留在 Workspace/ 内，不散落到系统盘；目录约定见 Workspace/README.md。
11. 【v1.1】AGPL 工具只读使用：不复制代码进框架、不链接、不随产物分发。

### 首个 Agent 任务（直接执行）【v1.1：已完成，保留追溯】

> 请先执行 Phase 0 和 Phase 1 的预研工作，不要马上实现注入器、渲染 Hook 或内存读取。检查本机 Steam 版 Sister Location，采集 EXE 指纹和 PE 架构；尝试使用 `ctfak-cpp` 静态解析游戏文件；产出 `docs/PREFLIGHT.md`、`docs/BUILD_FINGERPRINT.md`、`analysis/frame-index.md` 及两个目标场景的事件调查报告。请记录每个结论的证据和置信程度。若工具无法解析或目标场景无法定位，报告具体阻塞点和下一步验证方案。不得把游戏原版资源或完整提取结果提交到仓库。

#### 下一轮 Agent 任务（v1.1）

> 【v1.1】Phase 0/1 的静态部分已完成。下一轮请：①把已有静态结果整理成 docs/PREFLIGHT.md、docs/BUILD_FINGERPRINT.md、analysis/frame-index.md、analysis/night2-breaker-room.md、analysis/night5-private-room.md、analysis/jumpscare-audio.md 六份文档（从 recon/notes/2026-10-25-static-findings.md 与 recon/ 产物提炼，不复制 dump）；②在用户批准下执行 Phase 2a 只读注入探针（含安装 32 位工具链）；③不要实现 Hook、内存写入或渲染修改；④结论标证据等级，失败记录阻塞点；⑤不得提交 dump、原版资源或完整提取结果。

## 6. 参考资料

以下资料是预研入口，不代表每项工具都已在当前 Steam 构建上验证成功。

- [ctfak-cpp — ClickTeam Fusion 2.5 等格式的 C++ 解析/资源提取实现](https://github.com/Sergey004/ctfak-cpp)
- [CTFAK — 原版 ClickTeam Fusion Army Knife 项目](https://github.com/CTFAK/CTFAK)（仓库声明已停止开发；EXE 到 MFA 也并非完全支持）
- [Dear ImGui 官方仓库](https://github.com/ocornut/imgui)
- [Dear ImGui：Getting Started / 后端与输入焦点](https://github.com/ocornut/imgui/wiki/Getting-Started)
- [MinHook — Windows API hooking 库](https://github.com/TsudaKageyu/minhook)
- [Lua 5.4 官方参考手册](https://www.lua.org/manual/5.4/manual.html)
- [Clickteam Fusion 2.5+ 功能与运行时说明](https://www.clickteam.com/clickteam-fusion-2-5-plus-feature-overview)
- [SteamDB：Sister Location 配置与启动文件信息](https://steamdb.info/app/506610/config/)

## 7. 已知事实与未解问题（【v1.1】新增，证据登记）

### 7.1 已确认（实测/高置信推断）

- 目标 EXE：SisterLocation.exe，950,556,728 B，SHA-256 c60e1c79…2b2c73，PE32 x86，Fusion build 286，Steam buildid 1497593。
- 文件结构：外层 MinGW 启动器 + 内嵌 Fusion pack（mmfs2.dll 运行时、mmf2d3d9.dll / mmf2d3d8.dll 渲染器、Perspective/kcini/kcclock 扩展、mp3flt/waveflt 滤镜）+ 标准 Fusion chunk 流（63 顶层 chunk）。
- 资源：游戏目录仅一个 EXE，全部资源内嵌；无 steam_api 导入（DRM-free 推断）。
- 36 帧清单与对象/事件索引已解析（ctfak-cpp 13–14 s；自研 Python 解码器互证）；392 个声音资源。
- jumpscare 音频索引 86/112/120/148/257/265/303，共 20 处播放点。
- 帧 9 Breaker Room 死亡链（事件组 58–61）；帧 27 Final Encounter 对象清单；帧 31 Custom Night 同构。
- 渲染 D3D9 证据（仅 Direct3DCreate9）；音频 DirectSound 证据（仅 DirectSoundCreate）；无 DirectInput/RawInput，输入走窗口消息。
- 32 位 C++ 工具链已安装并验证（winlibs i686 gcc 16.2.0，D:/devtools/winlibs-i686）。
- 【v1.2】注入可行且可复现：CreateProcess(SUSPENDED)+远程 LoadLibraryW，HMODULE 0x67E10000。
- 【v1.2】运行时模块：mmfs2.dll base 0x67D90000/0x78000；mmf2d3d9 已加载（基址会变）；mmf2d3d8 未加载；d3d9/DSOUND/WINMM/DDRAW/nvd3dum 已加载；无 Steam/overlay；apphelp/AcLayers 生效。
- 【v1.2】窗口：title Sister Location、class Mf2MainClassTh、子窗口 Mf2EditClassTh、style 0x17020000、2560×1440、DPI 144 Per-Monitor。
- 【v1.2】D3D9 确认（mmf2d3d9 仅导入 Direct3DCreate9）；DSOUND 确认（DirectSoundCreate 序号 1），但 WINMM/MCI 同时导入。
- 【v1.2】每次启动解包 8 文件到 %TEMP%/mrt*.tmp（哈希稳定、旧目录不清理）。
- 【v1.2】PAMU 头在私有堆可读（build 286），地址每次启动随机、布局与文件头不同。
- 【v1.2】游戏可直接启动，无需 Steam；进程 32 位 WOW64。
- 【v1.2】退出：WM_CLOSE/关窗 → 0xC0000005；无注入原版对照相同，属游戏原生缺陷。
- 【v1.3】加载时序：mmf2d3d9.dll 在 mmfs2 之后 250–437 ms 加载，d3d9.dll/dsound.dll 与其同时出现（实测基址 0x69FD0000 / 0x69260000）；早期快照仅约 39 个模块；mmf2d3d8.dll 全程未加载。
- 【v1.3】PAMU 扫描竞态：首轮可能未命中，需 300 ms 重试至 15 s（实测第 2 次扫描 312 ms 才成功）；缺失时 gate=degraded 而非崩溃。
- 【v1.3】PAMU 假候选：EXE 映像 0x006B378C 处存在 PAMU 字节但字段非法（rt=0x1112850F / pb=900），必须校验 runtimeVersion==0x302 且 productBuild==286。
- 【v1.3】loader 指纹门禁成本：全文件 SHA-256 950,556,728 B 约 4–5 s（约 190 MB/s）；负例 notepad.exe → exit 3 且不创建进程。
- 【v1.3】MinHook 的 MH_CreateHookApi 只入队不启用，必须再调 MH_EnableHook，否则 detour 永不触发（P3 首版 presents=0 根因）。
- 【v1.3】游戏窗口 = 顶层 Mf2MainClassTh + 子窗口 Mf2EditClassTh（晚约 5 s 创建，承渲染/输入）；键鼠消息应发给子窗口，WndProc 需同时子类化两者（广播会给同一按键两处 toggle）。
- 【v1.3】锁屏/安全桌面下 D3D9 CreateDevice 对 HAL/REF 均返回 0x8876086A（D3DERR_NOTAVAILABLE），OpenInputDesktop()=0 可作判据；该环境不影响 P2a 只读探针，但会阻塞所有渲染/输入验证。
- 【v1.3】ImGui UI 可在无 D3D9 设备下构建（overlay::InitUiOnlyForTest）：16:20 harness --imgui-only PASS（gui_hud_vtx=2478、hud_only_vtx=200），说明 UI 代码路径可离线回归；上屏仍需真实设备。
- 【v1.3】WM_CLOSE 生命周期钩子：WM_CLOSE/WM_QUERYENDSESSION/WM_ENDSESSION 置 closing 标志、关 GUI、Present 停画；离线 16:16 与真实游戏 16:17 各触发一次，不影响开/关 GUI 与输入吞噬。
- 【v1.3】PAMU 扫描加固（16:28）：只扫 MEM_PRIVATE 区，用 ReadProcessMemory(GetCurrentProcess()) 分块读取（不裸解引用）；实测 14–15 个私有区 / 0.31–0.43 MB / <1 ms 即命中；旧实现扫映像/映射区会命中假候选并在游戏并发释放页面时越界读。
- 【v1.3】启动挂起根因（16:24–16:30 定位并修复）：16:06/16:24/16:26 三次注入成功但游戏主窗口不出现、worker 日志停在 `pamu| cand addr=006B378C …`、loader 等不到 init 标记。根因＝旧 ScanPAMU 裸指针遍历全部已提交内存，游戏启动期页面被释放/重保护 → worker 线程未处理 AV，锁屏下 WER 挂起整个进程。修复后 16:28:56/16:29:28/16:30:00/16:30:56 连续 4 次运行 gate=ok、窗口正常。
- 【v1.3】可配置热键实测（16:30）：Workspace/config/framework.ini 写 hotkey=f2 → 启动日志 `hotkey=VK 0x71`；launcher `--toggle-gui 12,22 --toggle-key 0x71 --test-input 16` → visible=1 toggles=1 → swallow_k=3/swallow_m=3 → visible=0 toggles=2。P3 验收「默认 Insert + 可配置」两路径均有证据。
- 【v1.3】sidecar `verified=` 语义：ReadSidecarFingerprint 必须读 loader 写入的 verified 字段，不能以 sha256 长度推断（曾把不匹配副本误报 verified=1）。
- 【v1.3】未验证版本负例（17:09–17:10）：追加 1 字节副本（SHA-256 93df18d4…、950,556,729 B）经 --no-verify 注入 → verified=0、gate=fail fingerprint_mismatch、loader rc=2；游戏仍可启动。
- 【v1.3】P3 安全模式：hook_d3d9=0 时不装任何 D3D Hook，init status=ok hooks=0，游戏正常启退；可用于兼容性降级/对照实验。
- 【v1.3】mmfs2 等待窗口：P3 起 worker 先 InstallEarly（LoadLibrary d3d9 + MinHook 约 80–120 ms），到达等待点时通常 waited_ms=0；P2 的 125–235 ms 自进程创建起算，两者不矛盾。
- 【v1.3】配置非法值回退：hotkey=bogus → 保持默认 Insert（VK 0x2D）；hud/hook_d3d9 非 0/1 保持旧值；wait_mmfs2_ms clamp 0–120000；支持 hotkey=0xNN。
- 【v1.3】热键自动重复：WM_KEYDOWN 的 lParam bit30 置位表示自动重复，必须只吞掉不 toggle；否则按住热键会以键盘重复率反复开/关 GUI。
- 【v1.4】真实 D3D9 设备 Hook 生效：`device vtable hooked present=6A0B02A0 reset=6A0B06A0 origPresent=003D0F80`；实例心跳 `presents=2319 fps=60.0 device=04715BC0`（device 指针每次启动不同，禁止硬编码）。
- 【v1.4】状态面板已上屏且可关闭：p3-final-gui.png / p3-final-gui-closed.png；关闭后截图无 overlay 残留。
- 【v1.4】HUD 独立层：GUI 关闭时仍绘制（`draw| frame=N vtx=200 cmds=1 gui=0 hud=1`），window_capture 可见底部文本；HUD 用 `ImGui::GetForegroundDrawList()`，与主窗口独立。
- 【v1.4】ImGui 默认 ProggyClean 无 CJK 字形（中文显示 ????）；需在构建图集前 `AddFontFromFileTTF(<系统字体>, 14, ..., GetGlyphRangesChineseSimplifiedCommon())`，候选 msyh.ttc/msyhbd/simhei/simsun/Deng/msjh，失败回退默认字体。
- 【v1.4】`FindWindowW/A`、`FindWindowExW` 对 `Mf2MainClassTh` 返回 0（`GetWindowTextW` 只返回 `'S'`）；自动查找窗口必须用 `EnumWindows`+pid+`GetClassNameW` 前缀。
- 【v1.4】`ShowWindow(SW_RESTORE)` + 置前会让 Fusion 把后台缓冲 2560×1440 重设为 1280×720（`Reset ok backbuffer=1280x720`）；自动化只能 `SetForegroundWindow`。
- 【v1.4】非 DPI 感知进程的 `CopyFromScreen` 得到逻辑分辨率（实测 1707×960，物理屏 2560×1440）；原始像素证据用 `window_capture`（DWM thumbnail）或 draw 遥测。
- 【v1.4】任务期间可用 `SetThreadExecutionState(0x80000003)`（ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED）阻止系统锁屏；用户已授权在任务进行时使用，任务结束须复位（`0x80000000`）。
- 【v1.5】配置版本化与恢复：`config_version=1`；缺文件自动写默认；版本字段缺失按 v1 迁移且不重写文件；非法/未知版本、二进制内容、≥3 无效行 → 备份为 `framework.ini.bad-<timestamp>` 并写回默认（`config| RECOVERED`）。
- 【v1.5】插件宿主：Lua 5.4.7 静态链接，单 `lua_State`、每插件独立 `_ENV`（`__index` 指向真实全局，全局只读）；可用 base/table/string/math/coroutine/utf8；io/os/package/debug 与 dofile/loadfile/load/require 全部置 nil；所有回调在渲染线程。
- 【v1.5】Lua chunk 的 `_ENV` 绑定顺序：必须 `lua_setupvalue(L,-2,1)` 且把 env 副本最后压栈；顺序错会得到 `attempt to index a nil value (global 'sl')`（甚至错误串到别的插件），此坑已在 plugin_host.cpp 修复并留注释。
- 【v1.5】插件状态 5 值：0=disabled（清单 enabled=0）、1=loaded、2=api mismatch（清单或 plugin_info 的 api_version≠1）、3=error（语法/入口/plugin_info/on_load 异常）、4=invalid（清单缺失/缺 id 或 api_version）；MAX_PLUGINS=32；on_load 抛错→该插件 kError 不加载。
- 【v1.5】GUI 启停：`SetEnabled` 写 `plugin.ini.tmp` 再 `MoveFileEx` 原子替换 → `RequestReloadPlugins`；禁用前先调 `on_shutdown`；重启从清单恢复（实测 pid 20904 保持 disabled=1，再勾选回 loaded=1）。
- 【v1.5】插件自绘复选框实测屏幕中心 (86,809)（150% DPI、面板 640×600 逻辑）；真实鼠标注入必须 `SetProcessDpiAwarenessContext(-4)` + `SetCursorPos`/`mouse_event`，PostMessage 伪造鼠标会被 ImGui Win32 后端的 `WM_MOUSELEAVE` 清空 `MousePos`。
- 【v1.5】插件离线夹具 6 个覆盖 good/mismatch/syntax/disabled/runtime/sandbox；`SUMMARY4 pass=1 total=6 vtx=2586`；沙箱夹具验证 io/os/package/debug/dofile/loadfile/load/require 全部不可见。
- 【v1.5】`ctx.game` 占位值【v1.9 已过时】：当初只有 `supported=false`、`scene_id=-1`；现在 `ctx.game` 已由 GameAdapter 注入只读现场（scene_id/scene_name/counters_ready/counters），但仍不得据此做危险/安全判断（`counters_ready=false` 表示未就绪）；插件 API 版本=1，清单与 `plugin_info()` 两处都必须匹配。
- 【v1.6】存档格式（实测）：`%APPDATA%\MMFApplications\sl` 26 B 文本 INI，仅 `[sl]/intro/current`，无夜内场景字段、无注册表项；首次启动创建。
- 【v1.6】帧路由未闭合（静态）：帧 4 `load` 按全局 Counter `go to` 跳帧（6→帧 9、12→帧 6…），但 99 个 Jump to frame 中仅 3 处指向帧 4（均来自帧 15 game over 的检查点恢复）；其余帧设置 `go to` 后如何回帧 4 在导出中缺失，需动态验证。
- 【v1.6】Extras 隐藏入口（静态）：帧 18 点击 `little Baby`（`started==0`）→ `came from extras=1` → Jump to frame 27；帧 9 无菜单入口。
- 【v1.6】`mmfs2.dll` 为纯序数导出（EAT≈1095 项，Name Pointer/Ordinal Table=0，Ordinal Base=2），无法按符号名定位帧跳转函数。
- 【v1.7】CRunApp 定位法（实测）：RW 私有区找 dword==36（m_frameMaxIndex）+4==36（m_frameMaxHandle）+8→36 个 WORD 为 0..35 排列；命中地址即 &m_frameMaxIndex，**base=命中-0xC4**；app+0x1D8=m_Frame、+0x1E4=m_appRunningState（3=运行/1=加载）、+0x1EC=m_nextFrame(index)、+0x1F0=m_nCurrentFrame。
- 【v1.7】CRunFrame 实测偏移：+0x00/0x04 w/h（FrameHeader）、+0x10 m_name（**UTF-16LE**）、+0x2C 层数、+0x44 实例数、+0x4C 实例 handle→index 表、+0x50 实例数组、+0x74 m_levelQuit、+0x78 m_rhOK、+0x7C m_rhPtr（**部分帧为 0**）、+0x80 m_eventPrograms；CRunFrame 对象地址会被复用，不能当身份。
- 【v1.7】RunHeader 实测偏移：+0x18 rhApp、+0x1C rhFrame、+0x30 rhQuit(short)、+0x38 rhQuitParam(dword)、+0x3C rhNObjects、+0x54 rhNumberOi、+0x58 rhOiList（Phase 5 读对象用）。
- 【v1.7】frame handle→index 映射（build 286）：`[2,4,3,7,5,6,12,8,9,10,11,1,13,0,15,14,17,16,18,22,19,23,20,21,24,25,28,26,27,29,30,31,32,33,34,35]`。事件里的 Jump to frame 存 **handle**，运行时用 `CRunApp.m_frame_handle_to_index` 换算；ctfak dump 的 targetFrameName 按 index 反查，**名字会错**（例：Extras 点 little Baby target=27 显示 "Final Encounter"，实际进入 index 26 "8-bit Baby Game" = mapping[27]）。
- 【v1.7】跳帧协议（实测）：`*(int*)(rh+0x38)=frame_handle; *(short*)(rh+0x30)=3` → 运行时置 frame->m_levelQuit=3、m_Frame=0、`m_nextFrame=handle_to_index[handle]` 并加载目标帧。**禁止循环写 frame->m_levelQuit**（对已释放帧写会导致进程消失）。
- 【v1.7】warp 后帧内初始化完整：直接跳帧进入的 Breaker Room 出现 DANGER LV 与 "Press SPACE to play mascot response audio"，Final Encounter 出现 USAGE / POWER 98% / 12 AM / 三监视器与鼠标指针（截图见 docs/RUNTIME_SCENE.md）。
- 【v1.7】Final Encounter（index 27）唯一静态入口 = 帧 10 Funtime Auditorium G116（target 28 → index 27）；Breaker Room（index 9）静态入口 = 帧 4 load 路由 G6（target 8 → index 9，go_to=6）。
- 【v1.7】对象读取链（Phase 5 待用）：CRunFrame+0x4C 实例 handle→index 表、+0x50 实例数组、RunHeader+0x58 OI 列表；对象名/类型与 application.json 的 objects/frames 对应关系待实测确认。
- 【v1.8】OI 表实测：stride=146（0x92）；entry+0x00 = oilOi（**OI 句柄，不是表下标**）、+0x04 = oilType（实测 2=Active、7=Counter、3=String、6=Lives、32/33=扩展）、+0x5A 起 oilName（UTF-16LE，24 字符）；RunHeader+0x54 numberOi 是 int（非 u16）。
- 【v1.8】对象头实测（hoAdRunHeader 处即 rh，对象基址 = 命中-8）：hoNumber@0x00、hoSize@0x04、hoAdRunHeader@0x08、hoAddress@0x0C、hoHFII@0x10、hoOi@0x12、hoNumPrev@0x14、hoNumNext@0x16、hoType@0x18、hoCreationId@0x1A、hoOiList@0x1C、hoCommon@0x44、hoX@0x4C、hoY@0x54、hoImgWidth@0x60、hoImgHeight@0x64、hoFlags@0x7C、hoOffsetValue@0x80、hoLayer@0x84。hoOi 是 OI 句柄：必须先用 oilOi 线性查得 OI 索引，再校验 `hoOiList == oiList + idx*146`。
- 【v1.8】计数器当前值（实测）：`value = ~(int16)*(obj+0x20A)`。标题页 option 1→2→3 对应 +0x208 dword 0xFFFE0000→0xFFFD0000→0xFFFC0000；Final Encounter hour/power left/count seconds、Breaker Room Freddy/got you/danger level 4 用同一公式的读数与画面/事件一致。
- 【v1.8】帧对象地址复用（实测）：同一 CRunFrame base 可能是不同帧；state 映射必须以 `frame+0x7C` 的 rh 变化为准重建（日志 `state| frame memory reused, rebuilding map`）。
- 【v1.8】对象枚举法（实测）：`m_los`（frame+0x50）**不能**当 LO 数组用；改为全内存搜 dword==rh（对象 hoAdRunHeader），对象基址=命中-8，再按对象头字段校验。
- 【v1.8】state 扫描成本（实测，pid 6756）：每次映射重建 670–855 regions / 350–780 MB / 230–360 ms；仅在场景或 rh 变化时触发；250 ms Poll 本身只读少量字段，5 s 心跳写一行 state 摘要。
- 【v1.8】state 实机证据：`logs/framework/slmodf-20261026-012353-6756.log`、`logs/probe/p5state4-gui-fe.png`、`p5state4-gui-breaker2.png`、`p5state4-gui-breaker3.png`、`p5state5-option2.png`、`p5state5-option3.png`；完整说明见 docs/RUNTIME_STATE.md §7。
- 【v1.9】scene 快速路径实测（pid 24152）：`scene| fast path ready app=00F00848 via runheader rh=105BE938 frame=00F30FE0`；三次 warp 均走 `scene| fast idx=…`，识别延迟=Extras 1684 ms / Breaker 2064 ms / Final 3218 ms（等于帧加载时间，不再是扫描延迟）；日志 `logs/framework/slmodf-20261026-014052-24152.log`。
- 【v1.9】帧对象地址复用再次实测：Extras/Breaker/Final 的 CRunFrame base 均为同一地址（title 同址），因此帧身份只能靠 `scene::Poll` 重读字段 + rh 变化判定，将 base 当作场景 ID 是错的。
- 【v1.9】状态录制器实测（pid 2904）：`logs/state/state-20261026-015801-2904.jsonl` 95 行；`scene_changed=1` 四行分别 scene 0/1/9/27，counters 3/10/30/19；同名计数器以 `compute#2…` 区分；周期行随游戏变化（power left 94、final stage 2、count seconds 23）。
- 【v1.9】插件 ctx.game 实测（pid 28348）：`HelloPlugin update #601 … scene=9:Breaker Room counters=[Freddy=0 got you=0 danger level 4=1]`，`#901 … scene=27:Final Encounter counters=[got you=0 final stage=0 hour=12 power left=99 side=2]`；说明插件只通过只读快照获取状态。
- 【v1.10】音效路径最终确认：mmfs2.dll 通过序号 1 导入 DirectSoundCreate；WINMM mciSendCommandA/W、PlaySoundA/W、waveOut* 共 7 个 hook 安装成功但整局 0 次调用 → 音效只走 DirectSound。
- 【v1.10】流式 chunk 事实：Fusion 按固定 1.2 s chunk 播放样本（chunk_bytes=rate*ch*(bits/8)*6/5），probe dump 与样本 PCM data 前缀逐字节相同（tools/p6a_dump_verify.py），整段样本哈希因此永不命中；改用前缀/chunk FNV-1a 64 后一次命中。
- 【v1.10】多 vtable：primary buffer 的 Play 实现 0x69293890、secondary 0x69293990，需按目标地址分别 detour（Play 8 变体 / SetVolume 6 变体）；日志 audio| play hook #n target=… enable=MH_OK。
- 【v1.10】识别/柔化/回滚实测：pid 19712 识别 name="scream op5-7" how=3 chunk=0 scream=1；pid 24028 soften … off_cb=-1204 readback=-1204；pid 15276 audio_soften=0 时 0 条 soften。
- 【v1.10】配置默认值：audio_probe=0、audio_soften=0、audio_gain_percent=25；默认不改变任何音效（安装框架本身静默）。
- 【v1.11】帧 27 音频链实测（pid 23104）：`got you=1` → `audio| play #7 … match=154 handle=303 name="scream op5-6" how=3 chunk=0 scream=1`，同毫秒 `soften … gain=25% off_cb=-1204 … (readback=-1204)`；dump `logs/audio/dump-23104-007-105840-44100-1-16.bin`。
- 【v1.11】`ctx.audio` 插件能力实测（pid 20980）：config `audio_soften=0` 时 SoftJumpscares 调 `set_soften(1,25)` → 日志 `audio| plugin set_soften=1 gain=25%`，帧 27 scream 的 SetVolume 生效（readback -1204）；插件 UI 见 `logs/probe/p6e-plugin-ui.png`。
- 【v1.11】插件 UI 原语扩展：`ui.slider_int(label,value,min,max)` 与 `ui.button(label)` 已接入 ImGui；v0 共 7 个原语（text/text_disabled/separator/same_line/checkbox/slider_int/button）。
- 【v1.11】chunk #1+ 观察：音效续流时引擎不重新调用 `Play`（现有日志无 chunk=1/2/3），Play 钩子只能看到 chunk #0；SetVolume 是 buffer 级属性，chunk #0 上设置的音量对同一 buffer 后续写入同样生效。长样本实机证据仍缺，不影响已验收的柔化效果。
- 【v1.11】离线回归：`tests/build_harness.sh` 的 natives 列表补齐 `audio_hook` 后链接成功；`--wndproc-only/--imgui-only/--plugins` 全 PASS，`SUMMARY4 pass=1 total=6 vtx=2630`（`logs/framework/harness-20261026-035518.log`）。

- 【v1.12】Night 2 Ballora Gallery 自然死亡（pid 24092）：`jumpscare` 0→1 @04:41:46.548（distance 582→601）→ death @04:41:48.046（约 1.5 s，`random=5`）；该计数器是死亡前约 1.5 s 的唯一自然信号。
- 【v1.12】Night 2 Breaker Room 自然死亡（pid 24092）：Freddy 1→2 @04:47:47.229、4 @04:47:49.234、6+got you=1 @04:47:51.238、interface=100 @04:47:52.740、got you=2 @04:47:54.243、death @04:47:56.062（`random=3`）；与事件组 58–61 吻合，Freddy≥6 到死亡约 4.8 s。
- 【v1.12】Night 5 Final Encounter 自然游玩（pid 28120，05:08:09 入场）：hour 每 90 s 一档（12→1 +89.24 s，随后 +90.26/+89.74/+90.25/+89.75）；power left 99→7 约 470 s（~5.1 s/点）；final stage 0→1 +3.51 s、1→2 +29.58 s。
- 【v1.12】Night 5 死亡链：got you 0→2 @05:15:58.285 → death @05:15:59.971（+1.686 s），死亡时 power left=7、三扇门均未关（AFK）；death 行 `random=1` → 约 5.2 s 后进入帧 26 8-bit Baby Game。
- 【v1.12】Night 5 keycard 闸门：自然路径=帧 10 Funtime Auditorium 经 keycard 闸门进入帧 27；存档 keycard=0 时播样例 #50 "denied" 且约 2 s 后 got you=1 死亡，keycard=1 时放行并播 "granted"；keycard 由帧 26 8-bit Baby Game 完成时 `DO "Ini" act #87 keycard = 1` 写回（小游戏奖励）。
- 【v1.12】帧 27 计数器语义修正：`near` 全程 0 且事件无引用（禁用）；`side`（1/2/3）是已核对计数器中唯一在死亡前持续变化的方向信号；`viewing cam` 取 1/2/5；`got you` 到死亡仅约 1.7 s（不能作预警触发）；Ennard `alterable[3]==99` 表示到达门口，需扩展 Active alterable 读取。
- 【v1.13】`on_draw_overlay` 实测（pid 24336）：插件日志 `callbacks load=1 update=1 draw=1 overlay=1`；`overlay.cpp` 在 GUI 之后画前景，GUI 关闭仍绘制；原语 `ui.viewport/text_size/text_at/rect_filled/rect/line` 在 2560×1440 与 1280×720 下工作正常。
- 【v1.13】Active alterable 读取实测：`state::FindObjectAlterable` 从 rVal→rvpValues→CValue[26] 读值，索引 0-based；只监视 Ennard/flip it，GUI 显示 `Ennard[0..3]` 与 `flip it[0..3]`；pid 21076/24336 均读到 Ennard[3] 变化。
- 【v1.13】帧 27 预警链实测（pid 24336）：Ennard[3] 0→1 @06:08:17.329（low/left）→0 @06:08:23.345→5 @06:09:53.347（low/vent）→6 @06:10:11.394（high/vent）→99 @06:10:23.428（attack/center），death @06:10:24.196（99→death 0.77 s）。
- 【v1.13】默认关闭/gating：pid 19872 启动日志 `enabled=false debug=false approach=true`；插件 `CONFIG.enabled=false` 时不绘制，状态日志也仅在 enabled/debug 时写；默认行为不打扰原版游玩。
- 【v1.13】预警只读边界：插件仅读 alterables/counters，不写游戏内存、不注入输入、不改计时/AI/胜负；overlay 为纯绘制。
- 【v1.13】文档：`docs/THREAT_RULES.md` 新建；`docs/PLUGIN_API_V0.md` 补 on_draw_overlay/alterables/原语；`framework/README.md` 与 `logs/probe/README.md` 已同步 Phase 7 行为与证据。
- 【v1.14】帧 9 分级实测（pid 19272/13216）：Freddy 自然序列 1→2→4→6（跳奇数）；阈值 2/4/6 已实装，high Freddy=4、low Freddy=2 截图有效；写 Freddy=6 后插件 alert=attack，约 0.75–1.0 s 切 death 帧 14。
- 【v1.14】帧 9 `beat` 语义：G58（Freddy≥6 && got you==0）**无 beat 条件**；G63（interface≥800 && beat==0 → beat=1, got you=0）、G64（beat==1 → Freddy=5）。pid 1408 beat=1 + Freddy=6 时插件静默但游戏仍死亡 → beat 不是护盾。
- 【v1.14】帧 9 攻击抓拍工具：`tools/p7_attack_capture.py` 写全部同名 counter 并按 `--delays 0.15,0.35,0.55` 多点截图；pid 13216 d150/d350 有效（jumpscare+攻击框），d550 已切帧无效。
- 【v1.14】默认关闭回归（pid 3584）：`enabled=false debug=false approach=true`，帧 9 写 Freddy=4 无 overlay/日志；截图 `p7-default-off-3584.png`。
- 【v1.14】清场/存档：游戏与 launcher 无残留；config 恢复 `framework.ini.p5nat-orig`（SHA `dc8be338a064fa72f435ea131493ac6b483fd6d0bbb29a805414990992f2e410`，record=0）；存档 SHA `6a67732756457b348a7d46b388b904183a58babc67553a2979427ac4eaa5e17b`、current=1、raw `[sl] intro=1 current=1`。
- 【v1.15】全局音频实测（pid 29632/30284）：帧 8 `audio| play … name="scream op5-8" scream=1` + soften readback -1204；帧 10 `scream op5-2` handle=148 + soften（同局 Static2 scream=0 不软化）；帧 19 死亡 `scream op5-8` + soften。空白名单=全部 scream，7/7 样本覆盖。日志 `slmodf-20261026-161906-29632.log`、`slmodf-20261026-162935-30284.log`。
- 【v1.15】全游戏威胁矩阵：`plugins/ThreatTelegraphs/main.lua` v0.3.0（948 行）覆盖帧 8/9/10/11/13/19/27/31，帧 16（windpad 不在 alterable 白名单）/帧 26（碰撞瞬时）不做；同帧只显示最高优先级一条；离线 50 断言 + luac -p 通过；静态依据 `research/p7-global/threat-matrix.md`。帧 27 `side` 修正为 1=左/2=中/3=右，可作 `Ennard[3]==0` 的方向低警。
- 【v1.15】QoL 鼠标释放实测（pid 29632）：Insert 打开 GUI → `qol| mouse released gui=1 foreground=1`；关闭 GUI → `qol| mouse captured again`；Alt-Tab 到 DSH → `gui=0 foreground=0` 释放。实现为 hook `user32!ClipCursor/SetCursorPos/ShowCursor`，`qol_release_mouse=1` 默认开；未装成仅日志 `installed=0/3`，不崩溃。
- 【v1.15】QoL 密码盘小键盘实测（pid 19316）：`p6_trigger_scare` 写 `Script Event=607`、`prompt=5`、`death timer=0`，向子窗口 PostMessage `WM_KEYDOWN VK_NUMPAD5` → `chain 0→1`、prompt 消费；日志 `qol| keypad numpad digit=5 prompt=5 chain=0 vk=0x65`。守卫 scene19/Script Event==607/prompt 1..9/answer==0/chain<10/got you==0；NumLock 必须开。
- 【v1.15】QoL Private Room 快捷键实测（pid 29536）：`qol| private flip it 0 -> 2` 后面板切到 CAM01-07；`left/vent/right door -> closed` 后三处门指示灯红→绿；A/D/W/S 走 `WriteAlterable`+`WriteCounter` 状态注入（跳过门动画但指示灯同步）。合成鼠标点击实测不被游戏识别，故不走点击路线。
- 【v1.15】版本/构建：`SLMODF_VERSION` 0.5.0-p8、overlay 副标题 Phase 8；F10 dev 光标冻结已删除；`build.sh` + `build_harness.sh` 成功，离线三模式 PASS（`logs/framework/harness-20261026-164347/164351/164356.log`）。
- 【v1.16】QoL 门动画实测（pid 22180，log `slmodf-20261026-223202-22180.log`）：`state::WriteAnimation` 写 `raAnimForced`（+0x1CE，`anim+1`）；关门 anim=12 → `forced=13, on=12, frames=10`，0.295 s 播完 → `forced=14, alt0=2`；开门 anim=14 → `forced=15, on=14, frames=10` → `alt0=0`。灯对比截图 `p8-fr27-door-lamp-compare.png`。
- 【v1.16】QoL Doorslam 音效（pid 22180）：`Workspace/assets/sfx/Doorslam.wav`（本地解包 seed）+ `PlaySoundA SND_FILENAME|SND_ASYNC|SND_NODEFAULT`；日志 `qol| door sfx path=... found=1`、`winmm| PlaySoundA ... flags=0x00020003`。
- 【v1.16】帧 27 监控面板原生链（pid 9908，log `slmodf-20261026-231055-9908.log`）：`trigger cursor` 移入 `flip trigger` 矩形 + `trigger ready=1` → 游戏 Create `Active 8`、播 #299 `flipup2`、`flip it[0]` 0→2（23:11:57.402/408）；反向 → #300 `putdown`、回 0（23:12:29.398）。截图 `p8-flip-native-9908.png`。合成输入不可用：游戏每帧把光标重置到客户区中心（1707×960 虚拟客户区/物理 2560×1440 @150%），SendInput/PostMessage 均无法命中。
- 【v1.16】帧 27 预警静默条件修复：`hour>=6` → `hour==6`（Private Room hour 12→…→6，12 AM 不是天亮）；离线 harness 四用例（hour 12/st 5→low、12/6→high、6/5→无、6/99→无）验证通过。
- 【v1.16】QoL 新增 native API：`state::ReadObjectRect`（hoX@0x4C/hoY@0x54/hoImgWidth@0x60/hoImgHeight@0x64）、`WriteObjectPosition`、`ReadAlterableLive`（任意 Active CValue type0）、`WriteAnimation`（raAnimForced）；构建 23:14，DLL 1,607,168 B，离线三模式 PASS。


- 【v1.17】ThreatTelegraphs v0.4.0 实机（pid 20584，log `slmodf-20261027-001644-20584.log`）：帧 31 `Ennard[3]` 1/2/4 对应 `current volume` 目标 5/15/50；`Ballora count` 每 200 ms -1、归零推进一段并重置 50；自然局 low→high→attack 分别为 `00:20:58`/`00:21:07`/`00:21:17`，99 后约 10 s got you。
- 【v1.17】帧 31 多通道/安全提示：攻击全屏（左门未关）与中心 Minireena 低警同屏（`p9-sc31-attack-002117.png`）；写 `left is closed=1` + `Ennard[3]=0` 后日志 `alert=none dir=-`，截图绿字「左门安全」（`p9-sc31-safe-002759.png`，2.5 s 保持）。
- 【v1.17】帧 8 Ballora 距离（注入采样）：`distance=430` → 显示 400/600「正在回退」；`distance=560` → 557/600「马上停下！」（`p9-sc8-low_d150.png`/`p9-sc8-high_d150.png`）。distance 由 W 键 +1/100 ms、松开 -1/30 ms，>600 且 `dancer[0]==0` 触发 jumpscare。
- 【v1.17】字体全字库：`GetGlyphRangesChineseFull()` + `Build()` 后探测 U+6C27 → `overlay| ImGui 1.91.9b init ok ... font_atlas=2048x4096 fonts=1 cjk=1 glyph_oxygen=1`；「氧气不多了」的「氧」正常（`p9-sc31-oxygen_d350.png`）；配置 `font_path`（空=自动）与 `font_size=14`（10–40）。
- 【v1.17】插件离线断言：`tools/tt_offline_check.lua` 载入真实 `plugins/ThreatTelegraphs/main.lua` + fake ctx，10 项全过（ETA、安全提示、多通道、帧 8 位置行、GUI 滑条禁用）。另新增 dev 工具 `p8_alert_watch.py`/`p8_safe_capture.py`/`p8_inject_capture.py`（写 counter/alterable 后连拍并抓 alert 日志）。
- 【v1.18】task-4 研究（`research/p7-global/ballora-timing.md`）确认帧 31 脉冲周期 `(50 − Ballora Diff)×0.2 s`（Diff3→9.4 s 实测 9.273–9.528；Diff8→8.4；Diff13→7.4），真实剩余量 = `clamp(Ballora count − Ballora Diff,0,50−Diff)` 个脉冲；v0.4.0 的 `count/5` 高估 ETA 约 `Diff/5` 秒。
- 【v1.18】帧 31 到门 4/10 + 门开**不是**立即 99：下一脉冲才 99 → got you=2 同帧 → 0.75–1.0 s 死亡；门开时最长约 9.4 s 关门窗口，门关则 1.25–5.01 s 撤退。通风口 5/6 无事件组置位（死代码）；入场 `Ballora Diff` 伪值实测 12464 持续约 47 s，需 0.5 s 稳定门控。
- 【v1.18】ThreatTelegraphs v0.4.1 离线断言 30 项全过（door 倒计时/3·9 high/5·6 静默/垃圾 Diff 门控后重武装/Diff13 周期/多通道/hour6 静默/got you attack/safe 开关）；本轮无 native 改动，未重启游戏（pid 11292 仍在标题）。
- 【v1.19】帧 31 Freddy 精确模型：alterable[0] 侧别 1 左/2 右、[1] 语音序号 1..4、[2] 在场 250、[3] 攻击语音变体 1..3、[6] 阶段 0/1/2/4/3/5；alt6=3→同侧门、alt6=5→对侧门（Group344-347）；关门播放 #358 并 alt6=0（Group348-351）；死亡由 Bonnie scare + #303 scream op5-6 + got you=3 触发。
- 【v1.19】Bonnet 行走速度：Active 19 每 tick 移动 (-3,0)，满足 Bonnet Diff 阈值（1/5/10/15）的组数 N 决定速度 180×N px/s（diff=5 实测 359.4 px/s ≈ N=2），目标 x≈-230；点鼻子置 Active 19 alt[1]=1（#59 SwitchPush）→ 动画 12 → 销毁；alt2>70 也会销毁。
- 【v1.19】RunHeader.rhObjectList 偏移实测：镜像结构体 + pack(2) 得 rhApp 0x18/rhFrame 0x1C/rhQuit 0x30/rhQuitParam 0x38/rhNumberOi 0x54/rhOiList 0x58 与实测吻合 → rhObjectList=0x1278、rhNObjects=0x3C；每项 8 字节 {objPtr,routine}，hoOi 为 OI 句柄，遍历结果与全内存扫描对象数完全一致。
- 【v1.19】动态对象读取：published 快照不含事件 Create 的对象（帧 31 Active 19）；ReadObjectRect/ObjectRval/WriteObjectPosition 增加 rhObjectList 回退后，Active 19 的 rect/alterable 可实时读取（日志 state| live rect fallback name=Active 19 x=1581 y=828 w=700 h=700），插件 API ctx.game.object_rect/alterable 可用。
- 【v1.19】字幕不采用音频驱动的理由：392 个样本中 214 个 dump 是 MP3 帧（如 Freddy_Intro 194,350 B 首字节 ff fb），gen_sound_table.py 只认 RIFF PCM → 指纹表仅 178 条、无 Freddy 语音；状态驱动字幕（alt 索引→固定文本）确定且无需解码。

- 【v1.20】布局 B 路径规则（实测）：框架根 = `<game>\\SLModFramework` = launcher 所在 `bin` 的父目录；游戏根 = 框架根父目录（校验 `SisterLocation.exe`）。launcher 从自身目录连剥两级得框架根、再剥一级得游戏根；DLL 用 `GetModuleFileNameA(self)` 连剥两级得框架根。config=`<framework>\\config`、plugins=`<framework>\\plugins`、logs=`<framework>\\logs\\framework`、state=`<framework>\\logs\\state`、sfx=`<framework>\\assets\\sfx`；日志含 `framework=` / `game_dir=` 行。版本 0.7.0-p8。
- 【v1.20】布局 B 真机冒烟（2026-10-27 05:35，launcher pid 22376 → game pid 28860，hwnd 0x00E203DE）：launcher 日志 framework/game_dir/exe/dll 正确、指纹 OK、注入后 init status=ok version=0.7.0-p8；DLL 自动生成 `<framework>\\config\\framework.ini`、plugin scan root=`<framework>\\plugins`、HelloPlugin loaded=1（summary total=1 loaded=1）；GUI 0.7.0-p8 显示标题画面（Present 3103/FPS 60.0），截图 `Workspace/logs/probe/p8-layoutB-gui.png`；离线三模式 PASS（harness 1,509,376 B）。
- 【v1.20】发行/部署工具：`Workspace/framework/deploy.sh`（`--with-docs`/`--with-sfx`，校验游戏根存在、复制 bin/config 模板/HelloPlugin、建 logs/docs/assets）；`Workspace/logs/runtime` 为指向 `<game>\\SLModFramework\\logs` 的目录 junction；dev 工具 p9_launch_test.ps1 / p8_safe_capture.py / p8_alert_watch.py / p7_keypad_helper.py 已改布局 B 路径。

- 【v1.21】字节级可复现构建：DLL/launcher/harness 链接加 `-Wl,--no-insert-timestamp`，并移除 `slmodf.cpp` 启动日志里的 `__DATE__/__TIME__`（DLL 唯一逐次变化字节）后，连续两次独立构建三产物 SHA-256 完全一致（DLL 1,617,920 B / `fd83f195f220e584712dd89be2b7b177d2c596c737cba40b9b43f59f4e420b1e`、launcher 31,744 B / `22daac7fe3a197e7b0bd29395274361eefa02f96def7dd42df05ac3fc02b85ea`、harness 1,517,056 B / `248ab25b34fde79a047d85718a6fe89b3c57eb66aa8fe4ca8f8627ec558cb24d`）。
- 【v1.21】最终发行包：`release/SLModFramework-1.0.0-win-x86.zip` 805,520 B / SHA-256 `f8932e2513efbb70ae662d67ae51e21c62f84afc75bcfcb5a27b90980acc7859`，18 文件（`bin/`、`config/framework.ini.example`、`plugins/HelloPlugin/`、`docs/`、`LICENSE`、`THIRD_PARTY_NOTICES*`、`README.md`）；ZIP 内路径前缀 `SLModFramework/` → 解压即得 `<game>\SLModFramework\` 层级；包内二进制哈希与 `BUILD_MANIFEST.sha256` 一致，不含 logs/assets/dump。
- 【v1.21】P8.2 稳定性实测：`SLMODF_LOG_ROTATE_BYTES=4096` 启动实机日志出现 `[log] rotated (threshold 4096 bytes, 5 backups)` 并生成 `.1`..；`ExportDiagnostics()` 生成 `diagnostics-<ts>.zip`（离线约 75 KB，`tar.exe` 打包成功）；GUI「恢复默认配置」写 `framework.ini.bak-<ts>` 并重载默认值；插件连续 30 次运行时错误自动禁用并持久化 `enabled=0`（`plugin| AUTO-DISABLED ... after 30 consecutive errors`）。
- 【v1.21】GUI 高 DPI 与自动化坐标：1440@150% 下主窗口内容高于屏幕导致底部按钮不可达；`SetNextWindowSizeConstraints` 限高 `DisplaySize.y-60*s` + `SetNextWindowSize(FirstUseEver)` 后滚动可达底部。本机 pwsh 为 DPI-unaware，`SetCursorPos`/`GetCursorPos` 用逻辑坐标（物理/1.5，逻辑屏 1708x961），点击 ImGui 按钮须按此换算（实测「导出诊断包」「恢复默认配置」均命中）。

### 7.2 待实机验证（未知）

1. IDirect3DDevice9 的 Present/Reset 实际参数（D3DPRESENT_PARAMETERS：窗口模式/交换效果/多重采样）；Alt+Tab/最小化/分辨率变化时的 Reset 行为。

   【v1.4】Present/Reset 参数已记录：窗口模式 windowed=1、后台缓冲 2560×1440（Reset 前曾 1258×664，Reset 后回 2560×1440）、刷新率=0（跟随显示器）；Alt+Tab/最小化切换仍待补。
2. 键鼠消息实际落在主窗口还是子窗口 Mf2EditClassTh；WndProc 子类化目标。
3. jumpscare 音频实际调用链：DirectSound CreateSoundBuffer/Play/SetVolume，还是 WINMM/MCI；buffer 是否带 CTRLVOLUME、创建/复用模式、指纹稳定性。【v1.10】已解：音效只走 DirectSound（WINMM/MCI/waveOut 0 次调用）；buffer 为 1.2 s 流式 chunk（三档字节数），柔化用 SetVolume 成功且 GetVolume 回读一致；指纹稳定性=依赖样本 PCM 前缀，构建更新会失效、需重生成表。【v1.11】帧 27 `scream op5-6`（#303）已实测识别与柔化（pid 23104）；chunk #1+ 无独立 Play 事件、长样本证据仍缺（列为已知限制）。
4. R286 的对象列表/计数器读取路径与跨场景/跨启动稳定性。【v1.7】PAMU→CRunApp→CRunFrame→RunHeader 链已实测（见 §7.1）。【v1.8】state 模块已实现并实机验证：PAMU→CRunApp→CRunFrame→RunHeader→OI→对象头→计数器（`~(int16)(obj+0x20A)`），帧 9/帧 27 读数与画面/事件一致；剩余：跨启动/跨场景长期稳定性样本、自然游玩复核值语义、对象数少于 application.json 实例数的原因。【v1.9】scene 快速路径已实装（常态识别延迟降至帧加载时间）；recorder 与 ctx.game 已能把该链路产出的值留痕/供插件读取；剩余仍为跨启动长期稳定性与自然游玩语义复核。【v1.12】自然游玩三局已复核：Ballora 帧 8、Breaker 帧 9、Final Encounter 帧 27 的计数器读数与死亡时间对齐（见 §7.1 与 docs/P5_NATURAL_VERIFICATION.md）；剩余跨启动长期稳定性样本、对象数少于 application.json 实例数的原因。
5. 帧 27 的 got you/final stage/Ennard 方向的读取值与触发时机。【v1.12】已实测：got you 0→2 后约 1.7 s death（太晚，不能作预警）；final stage 0→1 +3.5 s、1→2 +29.6 s；Ennard 方向已可读取 Active alterable（见第 9 项），`near` 全程 0 不可用。
6. 计数器读取时机、预警提前量与误报率。【v1.12】自然数据候选：Ballora `jumpscare` 提前约 1.5 s；Breaker Freddy≥6 到 death 约 4.8 s；Private Room `side` 为已核对计数器中唯一持续方向信号。【v1.14】帧 9 阈值按自然序列 1,2,4,6 定为 2/4/6；pid 19272/13216 单局触发正常，多局误报/漏报率仍未采样。【v1.17】帧 31 自定义夜（Angry Ballet，用户 m10402）：Ballora 路径每段约 10 s、音量 0/5/15/50 与状态严格对应；v0.4.0 已按「实时位置 + ETA」提示，仍需多局自然采样统计误报/漏报与 ETA 误差。
7. 杀软/Steam overlay 对注入与 Hook 的干扰（本机未装 Steam overlay，需另测）。
8. Fusion 运行时「Jump to frame」入口（`mmfs2.dll` 纯序数导出、签名未知）。【v1.7】该项已解：Jump to frame = RunHeader.rhQuitParam(=handle) + RunHeader.rhQuit=3（详见 §7.1），warp 后帧内初始化实测完整。仍未解：帧 4 路由器在自然游玩中的回跳链路（不影响 warp 使用）。

9. 【v1.13】Active 对象 alterable 读取已解：`state::ObjectInfoAlterable/FindObjectAlterable` 从 rVal→rvpValues→CValue[26] 读值（0-based，仅监视 Ennard/flip it）；Ennard[3] 实测 0→1→0→5→6→99。剩余：`flip it` 的 alterable[0] 死亡链 A/B 语义（见第 10 项）。
10. 【v1.16】已解：`flip it[0]` 语义 0=放下/1=翻起中/2=启用/3=翻下中；真实触发链=隐藏 `trigger cursor` 进入 `flip trigger` 矩形 + `trigger ready=1` → Group63/64 Create `Active 8` + 强制动画 + Speaker #299/#300。QoL S 键已按此实现（pid 9908 实测 0→2→0、音效句柄 299/300）。
11. 【v1.12】死亡小游戏选择：death 帧 `random` 1/3/5 与是否进入帧 26 8-bit Baby Game 的关系未解（仅 random=1 观测到进入）。
12. 【v1.12】Night 5 存活到 6 AM 的条件与 hour=6 边界未实测；power left 在死亡时为 7 而非 0。【v1.16】预警侧 `hour==6`（通关时刻）已修复并离线验证；完整自然存活到 6 AM 仍未采样。

## 8. 风险登记与回滚（【v1.1】新增）

| 风险 | 触发条件 | 影响 | 缓解 | 回滚 |
|---|---|---|---|---|
| 游戏更新导致指纹漂移 | Steam 自动更新 | 依赖内部状态的功能失效 | 三项指纹门禁；新版本先跑 P2a | 禁用状态类插件，仅保留 GUI/日志 |
| 注入被拦截/杀软误报 | 杀软启发式、Steam overlay | 框架无法加载或游戏异常 | 提供 HUD-only 降级说明；多引擎扫描 | 不注入即可原版运行 |
| Overlay 崩溃/卡死 | Present/Reset hook 异常、设备丢失 | 游戏黑屏/卡死 | 最小 hook 面；设备丢失重建；失败即卸载 | 移除 Hook 并恢复原 WndProc |
| 音频 Hook 误伤/静音 | DSound 包装异常、指纹错配 | 对话/音乐被改或静音 | 帧白名单+索引双重判定；异常旁路 | 关闭插件恢复原播放 |
| 许可证风险 | AGPL/GPL 代码混入 | 分发违规 | 只读使用 AGPL 工具；第三方目录隔离 | 移除相关代码/依赖 |
| 预警误报 | 计数器读取错误/推断规则过宽 | 玩家误判，体验受损 | 证据分级；未实机验证规则默认关闭 | 关闭对应规则 |
| Steam 文件校验覆盖 | 用户执行验证/更新 | 修改被还原 | 不修改原版文件；补丁清单可重建 | 重新生成补丁或直接使用原版 |
| PAMU 扫描竞态/漏检 | 启动早期扫描过快、堆尚未就绪 | 门禁误判 degraded、状态插件被禁用 | 300 ms×15 s 重试 + 字段校验；失败时明确 unsupported 并禁用状态类插件 | 重启游戏/重跑 loader；不影响 GUI 与日志 |
| 游戏内存扫描触发未处理 AV（已修复） | 旧 ScanPAMU 裸指针遍历全部已提交内存；游戏启动期并发释放/重保护页面 | worker 线程 AV → 锁屏下 WER 挂起整进程，游戏不建窗、框架无 init 标记 | 16:28 改为 MEM_PRIVATE + ReadProcessMemory 分块读，扫描统计入日志；回归 4 次正常 | 若再现：查日志 scan 行与 WER 事件；不改代码 |
| 启动 SHA-256 成本 | 每次启动全量哈希 950 MB | 启动延迟约 4–5 s | P8 评估大文件抽样/缓存（仅已验证机器生效）；保留跳过选项 | 关闭指纹门禁即回退旧行为（不推荐） |
| 会话锁屏导致渲染测试失败 | 锁屏/安全桌面、远程桌面断开 | CreateDevice=0x8876086A、游戏黑屏、overlay 无法上屏 | harness 退出码 2 SKIP；p3_final_check.ps1 先判 LogonUI；文档写明判据 | 解锁后重跑；不改代码 |
| Reset/设备丢失后 ImGui 失效 | 分辨率变化、Alt-Tab、驱动重置 | GUI 不显示或绘制异常 | Reset/DEVICELOST 时 Invalidate→重建设备对象；harness 第 100 帧 Reset 回归 | 关闭 GUI 继续原版渲染 |
| 会话锁屏打断长时间验证（已解决） | 无人值守、用户离开 | 15:47–20:17 锁屏 4.5 h，GUI 上屏/截图全部阻塞 | 任务期间 SetThreadExecutionState 阻止锁屏（用户授权）+ 解锁监听器自动补跑 | 解锁后重跑 p3_final_check.ps1 |
| SW_RESTORE 改变窗口状态 | 自动化脚本置前或恢复窗口 | 后台缓冲被重设为 1280×720，画面/截图尺寸变化 | 只 SetForegroundWindow；不调用 ShowWindow | 重启游戏 |
| FindWindow 失效误导自动化 | ANSI Fusion 窗口类 | 脚本找不到窗口 → 输入投递/截图失败 | 统一 EnumWindows+pid+类名前缀；必要时人工 window_capture | 手动定位窗口 |
| CJK 字体缺失 | 系统无 msyh/simhei/simsun 等 | 面板中文显示 ???? | 候选字体列表 + 回退默认字体并记日志；必要时改英文标签 | 安装系统字体 |
| 截图 DPI 虚拟化 | 非 DPI 感知脚本 | 截图分辨率/尺寸与实际不符 | 仅作辅助证据，关键结论用 window_capture 与 draw 遥测 | 调整脚本 DPI 感知 |
| 插件拖慢渲染线程 | on_update/on_draw_ui 耗时过长 | 掉帧/卡顿 | v0 仅允许轻量 UI/日志；P8 前加 per-plugin 耗时统计与超时告警 | 禁用该插件 |
| 插件清单写坏/被占用 | SetEnabled 时文件被 AV/编辑器占用、MoveFileEx 失败 | 启停状态不生效 | 先写 tmp 再原子替换，失败 SetError 并在 GUI 显示错误 | 手动编辑 plugin.ini 或重启 |
| 插件 API 破坏性变更 | 未来升 API 版本 | 旧插件不兼容 | 清单与 plugin_info 双校验，旧插件标 api_mismatch 不加载；保留框架回退版本 | 回退框架 DLL |
| 配置文件首次自动创建 | 首次运行或配置丢失 | 用户对新增文件困惑 | 文档与 `framework.ini.example` 说明；损坏时保留 `.bad-<ts>` 备份 | 删除 framework.ini 即恢复默认 |
| warp 后状态与自然游玩不一致 | 直接跳帧跳过入场初始化/计数器初值不同 | 采集数据偏差，误判预警时机 | 双轨证据：warp 调通+自然游玩复核；记录进入方式 | 仅使用自然游玩数据 |
| Ghidra 逆向投入超预期 | 纯序数导出、无符号，函数定位困难 | P5 进度受阻 | 【v1.7】风险已解除：改用「自然跳帧字段追踪 + 内存差分」直接得出跳帧协议，未投入 Ghidra | 停止逆向，改用 P5.0a |
| state 全表扫描拖慢心跳 | 场景切换/rh 变化触发映射重建 | 重建 230–360 ms 内 worker 心跳延迟（不影响游戏渲染线程） | 【v1.8】仅在 scene/rh 变化时重建；【v1.9】scene 快速路径已实装，常态只读 `app+0x1D8` + 帧字段，扫描仅作兜底 | 临时调大轮询间隔或关闭 state（GUI/日志仍可用） |
| 计数器数值语义未复核 | 只按取值公式（~(int16)(obj+0x20A)）读取 | 值正确但语义/阈值可能误读，影响 P7 预警规则 | 【v1.8】双轨证据：warp 采样 + 自然游玩复核；规则未复核前默认关闭 | 关闭对应规则/插件 |
| 录制 JSONL 体积与隐私 | 常开 `record=1`（默认关闭） | 长时间游玩文件持续增长，含场景与计数器名（不含个人路径） | 【v1.9】GUI 开关 + `record_interval_ms`；文档标注默认关闭、分享前检查 | 关闭录制或删除 `logs/state/` |
| 音频指纹与构建绑定 | 样本 PCM 前缀 FNV 指纹由本地 ctfak dump 生成 | 游戏更新/重装后指纹表过期 → 识别失败（不会误柔化，match=-1） | 【v1.10】指纹表随 tools/gen_sound_table.py 再生成；scream 需唯一命中才柔化；audio_soften 默认关闭 | 重生成指纹表或关闭 audio_soften；仍失败则退回只读探针 |
| 插件争用音频全局状态 | 多个插件同时调用 `ctx.audio.set_soften/reset_soften/set_names` | 后写者覆盖前者，行为不确定 | `last-wins` 语义；日志记录 `audio| plugin set_soften/reset/whitelist`；默认 config 全关 | 禁用插件或调 `reset_soften()` 回到 config 值 |

| keycard 前置被误判为作弊/逻辑缺陷 | 复核时直接改存档 current/keycard | 误改游戏进度或误报游戏 bug | 仅用备份存档补齐前置，结束后恢复原档并记录 | 恢复 backups/save/ 备份 |
| 私室预警信号不足 | `near` 恒 0、`got you` 仅约 1.7 s 提前量 | 无法给出有效方向/提前预警 | 用 `side` + 扩展 Active alterable(Ennard.alterable[3]==99) 做规则；未复核默认关闭 | 关闭规则/插件 |
| 死亡小游戏随机性 | death 帧 `random` 值选择 8-bit 小游戏 | 复核路径不固定、难复现 | 记录 random 值并按需重复采样 | 不影响主体功能 |
| 预警窗口过短（99） | Ennard[3]=99 后 0.77 s 即 death | 玩家来不及反应，误以为预警无效 | 高警(4/6/10)即提示关门；99 仍显示“攻击已判定/立即关门”并闪烁；文档写明窗口；默认关闭 | 关闭插件/规则 |
| 双场景覆盖但仍有未覆盖 | 首版覆盖帧 27 与帧 9 | 其他场景无预警，形成漏报预期 | 文档明确覆盖范围；插件只对 scene 9/27 生效；后续继续补场景 | 关闭插件或等待后续版本 |
| beat 被误解为护盾 | 帧 9 `beat=1` 时插件静默 | 玩家可能误以为 beat 能阻止攻击，但 G58 无 beat 条件 | THREAT_RULES/main.lua 明确“静默≠护盾”；默认关闭；attack 判定优先于 beat 静默 | 关闭插件/规则 |
| 默认关闭被误认为故障 | 插件默认 `enabled=false` | 用户安装后看不到提示，以为插件坏 | GUI 插件区显示“已加载/已禁用”与开关；README/THREAT_RULES 写明默认关闭 | 在 GUI 勾选启用或改 CONFIG |
| 门状态快照滞后 | 计数器 500 ms 级刷新、门动画期间读数 | 高警可能在门已关/未关瞬间误判 | 只读快照 + 高警提示“关门”而非断言；多局采样后校准 | 关闭规则 |
| overlay 原语仅前景回调 | 只提供 on_draw_overlay 的 DrawList 原语 | 无法在 GUI 窗口内复用同一套坐标/裁剪逻辑，复杂 UI 需用 on_draw_ui | API 文档写明两类回调边界；首版只做简单文本/矩形/线 | 改用 on_draw_ui 或降低视觉复杂度 |
| 全游戏预警规则未实机标定 | 帧 8/10/11/13/19/31 阈值来自静态分析 | 可能误报/漏报，影响体验 | 默认关闭；首轮自然游玩开 debug 记录实际值再调；帧 13 默认禁用 | 关闭对应场景规则 |
| QoL 门动画依赖对象内布局 | `raAnimForced` 偏移 0x1CE 由 pack(2) 布局推导（`hoOffsetValue=0x242` 交叉验证） | 游戏更新若改对象布局，强制动画可能写错地址或无效 | 校验 hoSize≥0x220；更新后重跑 `p8_anim_probe.py`；失败时仅门状态变化（指示灯同步） | 关 `qol_private_hotkeys` |
| 密码盘小键盘依赖前置状态 | 仅帧 19、Script Event==607、prompt 1..9、NumLock 开 | 不满足时按键无反应或被游戏当鼠标 | 严格守卫+日志 `qol| keypad`；文档说明 | 关 `qol_keypad_numpad` 用鼠标 |
| 鼠标释放 hook 未装成静默失效 | user32 导出 hook 被 AV/其他 overlay 占用 | 失焦仍锁鼠标或误释放 | 启动日志 `qol| cursor hooks installed=3/3`；失败仅日志不崩溃 | 关 `qol_release_mouse` |
| 合成输入被游戏忽略（光标每帧回中） | SendInput/PostMessage 用于 3D UI | 点击/拖拽类操作全部无效 | 已确认走内存/触发器注入；文档写明不要用合成点击；坐标改用对象实时矩形而非硬编码 | 回退为原生鼠标操作 |
| Doorslam 素材缺失 | 发布包不含游戏解包素材、或用户未 seed | 门动画正常但无声 | `qol_door_sfx` 播放失败只记日志；`assets/README` 说明 seed 步骤；缺失时静音 | 关 `qol_door_sfx` |
| 全字库字体图集占用显存 | 全中文字库 2048×4096 RGBA | 低显存/老 GPU 可能纹理创建失败或挤占其它资源 | 实测 60 FPS 无影响；失败回退默认字体并记日志；提供 `font_path` 子集字体与 `font_size` 调节 | 关闭 GUI/HUD 或改小字号 |
| Ballora ETA 依赖计时器与 Diff | `Ballora count` 每 200 ms 递减；周期随 `Ballora Diff` 变化（`(50−Diff)×0.2 s`）；切换场景/暂停时可能不推进 | 倒计时与实际到门时间有 ±1 脉冲偏差；v0.4.0 曾把到门误判为 attack（已修正） | 按 v0.4.1 公式推算并在门开时给 high+倒计时；单帧抖动不改分级；多局采样校准 `scene31_ballora_urgent` | 关闭 `scene31_ballora` |
| 帧 31 QoL 快捷键未扩展 | QoL 的 A/D/W/S 只处理 scene==27 | 自定义夜无法用快捷键关门（仍可鼠标或内存注入） | 文档写明限制；后续把 QoL 门操作泛化到 scene31 并区分真实输入来源 | 用鼠标操作或关闭插件 |

| 动态对象回退偏移漂移 | Fusion 运行时布局随版本变化 | 事件创建对象（Bonnet 等）读不到 | rhObjectList=0x1278 仅对 build 286；校验 rhApp/rhFrame 与帧指针、失败返回 unsupported 并记日志 | 关闭 object_rect/alterable API 或禁用对应规则 |
| 字幕文本与语音错配 | 状态索引与语音样本映射错误、或用户构建不同 | 字幕内容错误 | 状态驱动映射仅 build 286 校验；字幕默认关；示例只放固定文本 | 关闭 subtitles |
| Bonnet 生成随机 | random(30) < Bonnet Diff | 测试/实机可能长时间不生成 | 只在 Active 19 存在时提示；文档写明概率；复现用 diff=30（约 2 s 生成） | 关闭 Bonnet 规则 |
| 游戏更新导致指纹门禁拒绝 | Steam 更新后 EXE SHA 变化 | 启动器 exit 3、拒绝注入（预期安全行为） | 文档写明期望/实际哈希与手动核验流程；不静默降级；新版本适配需重跑 P2a | 用户可回退游戏版本或等待框架适配 |
| 发行包目录与代码路径漂移 | 手工组包/旧文档沿用 Workspace 路径 | 框架找不到 config/plugins 或日志写到意外位置 | 布局 B 由 launcher/DLL 从自身位置推导；deploy.sh 校验游戏根；P8.5 用发行 ZIP 演练；文档标死 <game>\\SLModFramework | 删除安装目录重装；日志 framework= 行可诊断 |
| 公开发布流程无 git 凭据 | 本机无 credential.helper，git push 会挂起等输入 | 发布中断 | 用 cred_use（exec 已开启）把 GH_TOKEN 喂给白名单 git 命令，GIT_TERMINAL_PROMPT=0；身份 KIRARITenshiChan | 本地保留 release tag/包哈希，凭据恢复后重推 |
| 日志/config 路径显示 logs\framework\..\..\config | util.cpp 用 logDir 相对推导（功能正常） | 日志不美观、排障时误解 | 【v1.21】复核后未改（不影响功能），保留为美化项：下次框架改动统一为 `<framework>\config` 绝对路径 | 无功能影响，可延后 |
| 首发范围被示例插件需求拖大 | 用户/后续任务要求把 SoftJumpscares/ThreatTelegraphs 纳入 1.0 | 发布时间失控 | P8.0 范围冻结 + 本节"明确延期事项"；示例插件单列独立更新 | 回到 Phase 8 草案重新评估 |
| 发行包上传后损坏或与源码不一致 | GitHub Release 上传中断/误覆盖 | 用户下载到坏包 | Release 附 `SHA256SUMS`；发布后从 Release 重新下载校验包内 `BUILD_MANIFEST.sha256` 并再跑一次 P8.5 快速冒烟；tag 与源码同版本 | 用 `make_release.sh` 重建并重新上传 |
| 诊断包含本机路径泄露 | 用户把 `diagnostics-*.zip` 公开发布 | 泄露 Windows 用户名/安装路径 | 诊断包只含版本/配置/日志/sidecar，不含内存 dump 与游戏资源；`TROUBLESHOOTING.md` 与 KI-21 提示分享前检查 | 删除含路径的诊断包/日志 |

---

## 状态记录模板

每完成一个 Phase，在这里更新：

| 阶段 | 状态 | 验收日期 | 备注 |
|---|---|---|---|
| Phase 0 — 环境档案 | 已完成（静态+运行时） | 2026-10-25 | 产物见 docs/PREFLIGHT.md、docs/BUILD_FINGERPRINT.md、docs/RUNTIME_PROBE.md |
| Phase 1 — 静态逆向 | 已完成（静态） | 2026-10-25 | 六份文档已产出；实机校验转 P5/P7 |
| Phase 2a — 只读注入探针 | 已完成 | 2026-10-25 | docs/RUNTIME_PROBE.md + tools/p2a_probe/ + logs/probe/ |
| Phase 2 — 加载机制 | 已完成 | 2026-10-25 | framework/ + docs/LOADING_AND_LIFECYCLE.md；6 次启退、指纹负例 exit 3 |
| Phase 3 — GUI Overlay | 已完成 | 2026-10-25 | 真实设备 Hook/上屏/HUD/CJK/输入屏蔽/Reset/重复启退均实测；仅 Alt+Tab/手动分辨率变化转 P4 回归；见 docs/OVERLAY_INPUT.md §7 |
| Phase 4 — 插件宿主 | 已完成 | 2026-10-25 | config_version=1/损坏恢复；plugin.ini + 生命周期；Lua 5.4.7 沙箱；API v1；GUI 启停/持久化；HelloPlugin；离线 6 夹具 + 真机验证（docs/PLUGIN_API_V0.md） |
| Phase 5.0 — 调试场景直达 | 已完成 | 2026-10-26 | 存档辅助 tools/save_night.py；场景识别 scene 模块；CRunApp/CRunFrame/RunHeader 偏移 + handle→index 映射 + 跳帧协议（rhQuitParam/rhQuit=3）；tools/p5_jump.py 实测直达帧 9 / 帧 27（未用 Ghidra）；docs/RUNTIME_SCENE.md |
| Phase 5 — GameAdapter | 已完成 | 2026-10-26 | 【v1.9】state/DebugInspector + scene 快速路径 + recorder + 插件 `ctx.game` 均实机验证；【v1.12】用户真人自然游玩三局复核（Ballora Gallery / Breaker Room / 帧 27 Final Encounter）完成，计数器语义与攻击时间对齐；文档 docs/P5_NATURAL_VERIFICATION.md。可选：数据来源/信度标签、对象数差异原因。 |
| Phase 6 — SoftJumpscares | 已完成 | 2026-10-26 | 【v1.10】DirectSound 流式 chunk 指纹识别（pid 19712）、25% 柔化 + readback（pid 24028）、回滚 0 条 soften（pid 15276）；【v1.11】帧 27 scream op5-6（handle 303）识别+柔化（pid 23104）、SoftJumpscares 插件化与 ctx.audio（pid 20980）、GUI 取证（pid 28380）、离线三模式 PASS。chunk #1+ 长样本证据列为已知限制（不阻塞验收）。docs/AUDIO_HOOK.md、docs/PLUGIN_API_V0.md §4.1 |
| Phase 7 — ThreatTelegraphs | 进行中（示例插件 v0.5.1；帧 8/9/10/11/19/27/31 规则就绪，Freddy/Bonnet/字幕已实机；剩余延后至框架完工后） | — | 【v1.13】on_draw_overlay 绘制 API、Active alterable（Ennard/flip it）、帧 27 低/高/攻击预警（pid 24336，99→death 0.77 s）；【v1.14】帧 9 Breaker Room 分级 2/4/6 实机验证（pid 19272/13216/1408/3584）；【v1.15】v0.3.0 覆盖 8 场景（帧 8/9/10/11/13/19/27/31；帧 16/26 不做；帧 13 默认禁用），SoftJumpscares 全局 7 scream；docs/THREAT_RULES.md；【v1.17】v0.4.0 Ballora 实时位置/到门倒计时、多通道告警并存、安全提示、字体全字库（DLL 0.6.0-p8，pid 20584）；【v1.18】v0.4.1 按时序研究修正 Ballora 模型（到门 high+倒计时、`(50−Diff)×0.2 s` 周期、通风口 5/6 死代码、入场伪值门控），离线断言 30 项全过。剩余：自然采样、多局误报率、多威胁并发。 | 【v1.19】v0.5.0 Freddy 精确侧别/阶段（alterable[0/1/2/3/6]）+ 状态字幕试点、Bonnet 行走 ETA/点鼻子（`Active 19` 实时位置）；v0.5.1 修 flip 快照滞后；native 动态对象回退 rhObjectList=0x1278 + `ctx.game.object_rect/alterable`；离线 64 断言；证据 pid 23520/29848。
| Phase 8-QoL — 游玩体验补丁（【v1.15】新增） | 已完成（【v1.16】动画/音效补齐） | 2026-10-26 | 失焦解锁鼠标、密码盘小键盘、Private Room A/D/W/S；【v1.16】门动画 raAnimForced + Doorslam、监控面板原生 trigger 链（pid 22180/9908）；版本 0.5.0-p8；docs/QOL.md；build/harness 三模式 PASS |
| Phase 8 — 稳定与首次公开发布 | 【v1.21】P8.1–P8.5 完成；P8.6 待发布（GitHub Release v1.0.0） | 2026-10-27 | 用户 m12195 拍板全部落地：布局 B / MIT+公开 / GitHub v1.0.0 / 中文为主双语摘要 / 只带 HelloPlugin。v1.0.0 冻结：DLL `fd83f195…`（1,617,920 B）、launcher `22daac7f…`（31,744 B）、harness `248ab25b…`（1,517,056 B），字节级可复现、离线四模式 PASS；发行 ZIP 805,520 B / SHA-256 `f8932e25…`（18 文件）；干净安装→GUI（滚动/诊断包/配置重置）→卸载→原版对照演练通过（`docs/P8_ACCEPTANCE_DRILL.md`，截图 `logs/probe/p85-final-gui-top.png` / `p85-final-gui-bottom.png` / `p85-original-uninstalled.png`）。剩余：GitHub 发布 + tag v1.0.0。 |
| Phase 9 — 字幕系统（提案） | 提案（未实现） | — | 设计见 Phase 9 节；ThreatTelegraphs v0.5.1 状态字幕为试点；待 P8 后启动 |

### 下一轮工作建议（【v1.21】）

1. **P8.6 GitHub 首发（立即）**：用 KIRARITenshiChan 身份创建公开仓库并推送源码（MIT；不含游戏素材/dump/日志/recon），打 tag `v1.0.0`，Release 附 `SLModFramework-1.0.0-win-x86.zip` + `SHA256SUMS`；凭据走 `cred_use`（exec 喂 GH_TOKEN，`GIT_TERMINAL_PROMPT=0`），推送前 `git status` 确认只含白名单文件。
2. **发布后回归**：从 GitHub Release 重新下载 ZIP，校验 SHA-256 与包内 `BUILD_MANIFEST.sha256`，再跑一次 P8.5 干净安装快速冒烟（防上传损坏）。
3. **插件优先主线（m12162）**：Phase 9 字幕系统（`ctx.ui.subtitle/subtitle_clear`，状态驱动为主）；SoftJumpscares / ThreatTelegraphs 示例插件剩余完善（帧 8/10/11/13/19 自然采样、多局误报率、帧 31 QoL A/D/W 扩展）；均以插件独立版本发布，不改框架二进制。
4. **记账**：每次计划修改仍按 备份→CHANGELOG→SHA256SUMS 流程；本版备份 `SL-Mod-Framework-Plan_v1.20_20261027-070501.md`。
5. **风险跟踪**：游戏更新导致指纹拒绝（文档化为预期行为）、诊断包含本机路径（KI-21）、GitHub 凭据/仓库可见性、上传包与源码一致性。

---

## 变更记录

| 版本 | 日期 | 摘要 | 备份文件 |
|---|---|---|---|
| v1.0 | 2026-10-25 05:39 | 初版（421 行，22454 B） | — |
| v1.1 | 2026-10-25 | 【v1.1】标注：P0/P1 进度与实测数据、帧号更正（Night 5=帧 27）、§2 技术路线（32 位工具链/D3D9 证据/资源路线改 DirectSound）、P2a 探针、P6 路线改运行时 Hook、P7 证据、§7 已知事实、§8 风险、§5 备份规范 | plan-backups/SL-Mod-Framework-Plan_v1.0_20261025-1455.md |
| v1.2 | 2026-10-25 | 【v1.2】标注：P2a 完成并写回运行时事实（注入/模块/窗口/D3D9/DirectSound/解包/PAMU/退出缺陷）、ASLR 与地址约束、P2/P3/P5/P6/P8 细化、§4 两条硬边界、§7 事实账本、§8 风险、状态表更新 | plan-backups/SL-Mod-Framework-Plan_v1.1_20261025-153252.md |
| v1.3 | 2026-10-25 | 【v1.3】标注：Phase 2 完成记录（loader/指纹门禁/init 标记/心跳/退出码）、Phase 2a 加载时序补充、§2.2 设备级 Hook 安装时机、P3 实测与阻塞记录（MinHook 必须 Enable/子窗口 Mf2EditClassTh/离线 harness/输入链路/WM_CLOSE 生命周期/ImgUI 离线构建/锁屏 CreateDevice 0x8876086A/门禁负例/配置鲁棒性/draw 遥测/HUD 检查/安全模式）、§7 事实账本 +7、§8 风险 +4、Phase 8 指纹门禁负例验收、状态表与下一轮建议更新 | plan-backups/SL-Mod-Framework-Plan_v1.2_20261025-202825.md |
| v1.4 | 2026-10-25 | 【v1.4】标注：Phase 3 完成记录（解锁后真实 D3D9 设备 Present/Reset hook、GUI 上屏、HUD 独立层、CJK 字体与 DPI、输入屏蔽、Reset 恢复、重复启退/泄漏、keep-awake、验收脚本三处修复）、P3 任务清单勾选与验收结论、§7.1 事实 +8、§7.2 Present/Reset 参数更新、§8 风险 +5、状态表与下一轮建议更新 | plan-backups/SL-Mod-Framework-Plan_v1.3_20261025-204420.md |

| v1.5 | 2026-10-25 | 【v1.5】标注：Phase 4 完成记录（配置版本化与损坏恢复、插件清单/生命周期、Lua 5.4.7 沙箱、插件 API v1、GUI 插件状态与启停持久化、HelloPlugin、离线 6 夹具）、Phase 4 交付/验收结果、§7.1 事实 +8、§8 风险 +4、状态表与下一轮建议更新 | plan-backups/SL-Mod-Framework-Plan_v1.4_20261025-211904.md |
| v1.6 | 2026-10-25 | 【v1.6】标注：用户批准新增 Phase 5.0 调试场景直达（P5 前置，debug warp；存档辅助 + Extras→帧 27 + Ghidra 定位帧跳转，6h 止损）；§1.3 边界、§4 三条安全边界（warp 边界/warp 状态非自然/存档可恢复）、§7.1 事实 +4（存档格式/帧路由未闭合/Extras 入口/mmfs2 纯序数导出）、§7.2 未知项 +1、§8 风险 +2、状态表与下一轮建议更新；顺带删除一条重复的 v1.5 变更记录 | plan-backups/SL-Mod-Framework-Plan_v1.5_20261025-232931.md |
| v1.7 | 2026-10-26 00:50 | 【v1.7】标注：Phase 5.0 完成记录（存档工具、scene 检测、跳帧协议、handle 映射、直达帧 9 与帧 27）、当前阶段与状态表更新、§7.1 事实 +8、§7.2 两项已解、§8 Ghidra 风险解除、下一轮建议更新 | plan-backups/SL-Mod-Framework-Plan_v1.6_20261026-005009.md |
| v1.8 | 2026-10-26 01:35 | 【v1.8】标注：Phase 5 进展（state 模块 + DebugInspector 首版实机验证、计数器值公式与偏移、OI/对象头布局、rh 复用重建、扫描成本）、Phase 5 任务清单勾选、§7.1 事实 +7、§7.2 第 4 项更新、§8 风险 +2、状态表与下一轮建议更新 | plan-backups/SL-Mod-Framework-Plan_v1.7_20261026-013258.md |
| v1.9 | 2026-10-26 02:10 | 【v1.9】标注：Phase 5 收尾（scene 快速路径 01:40、状态录制器 01:58、插件 `ctx.game` 只读状态 02:00、离线三 PASS、真机证据）、Phase 5 任务清单 recorder 勾选与插件侧接入、§7.1 事实 +4、§7.2 第 4 项更新、§8 风险 2 处更新 +1 行、状态表与下一轮建议更新 | SL-Mod-Framework-Plan_v1.8_20261026-020330.md |
| v1.10 | 2026-10-26 02:54 | 【v1.10】标注：Phase 6 突破（DirectSound 流式 chunk 指纹识别、多 vtable Hook、scream 柔化 25% + readback、回滚 0 条 soften）、Phase 6 任务/交付/验收勾选、Phase 6 进展记录块、§7.1 事实 +5、§7.2 第 3 项已解、§8 风险 +1（音频指纹与构建绑定）、状态表 Phase 6 进行中、下一轮建议更新并清理重复 5–7 行 | SL-Mod-Framework-Plan_v1.9_20261026-025440.md |
| v1.11 | 2026-10-26 04:03 | 【v1.11】标注：Phase 6 收尾（帧 27 scream op5-6 识别+柔化、SoftJumpscares 插件化与 ctx.audio、ui.slider_int/button、GUI 取证、harness 重链接回归）、Phase 6 任务/交付/验收勾选、v1.11 进展记录块、§7.1 事实 +5、§7.2 第 3 项更新、§8 风险 +1（插件争用音频全局状态）、状态表 Phase 6 已完成、下一轮建议更新 | plan-backups/SL-Mod-Framework-Plan_v1.10_20261026-040306.md |
| v1.12 | 2026-10-26 | 【v1.12】标注：Phase 5 自然游玩复核完成（三局：Ballora Gallery / Breaker Room / Private Room 帧 27；入口与 keycard 修正、计数器语义与攻击时间对齐）、Phase 5 验收第 3 条补证、Phase 7 两目标补自然阈值、§7.1 事实 +6、§7.2 新增 4 项未解、§8 风险 +3、状态表 Phase 5 已完成、下一轮建议改为 P7 优先、修正 v1.11 遗留的当前阶段/文档版本（仍写 v1.10） | plan-backups/SL-Mod-Framework-Plan_v1.11_20261026-052746.md |
| v1.13 | 2026-10-26 06:20 | 【v1.13】标注：Phase 7 首版（on_draw_overlay 前景绘制 API、Active alterable 读取 Ennard/flip it、plugins/ThreatTelegraphs 帧 27 低/高/攻击预警、pid 24336 实机全链路、默认关闭）、Phase 7 任务/交付/验收勾选与进展记录、§7.1 事实 +6、§7.2 第 9/10 项更新、§8 风险 +5、状态表 Phase 7 进行中、下一轮建议改为 P7 收尾 → P8 | plan-backups/SL-Mod-Framework-Plan_v1.12_20261026-061947.md |
| v1.14 | 2026-10-26 06:40 | 【v1.14】标注：Phase 7 帧 9 Breaker Room 分级（Freddy 2/4/6）、攻击态 d150/d350 抓拍、beat 静默非护盾边界、默认关闭回归；Phase 7 任务/交付补勾、v1.14 进展记录、§7.1 事实 +5、§7.2 第 6 项更新、§8 双场景/beat 风险、状态表与下一轮建议更新 | plan-backups/SL-Mod-Framework-Plan_v1.13_20261026-064345.md |
| v1.15 | 2026-10-26 16:45 | 【v1.15】标注：Phase 7 全游戏扩展（ThreatTelegraphs v0.3.0 覆盖 8 场景、帧 16/26 不做、帧 27 side 修正）+ SoftJumpscares 空白名单 7/7 scream 三场景实测 + P8-QoL（失焦解锁鼠标、密码盘小键盘、Private Room A/D/W/S）+ 版本 0.5.0-p8/F10 清理/build-harness PASS；新增 v1.15 进展块、任务/交付/验收补勾、§7.1 事实 +6、§8 风险 +5、状态表新增 Phase 8-QoL 行、下一轮建议更新 | SL-Mod-Framework-Plan_v1.14_20261026-165137.md |
| v1.16 | 2026-10-26 23:14 | 【v1.16】标注：QoL 原版动画/音效补齐（门 `raAnimForced` 12/14 + Doorslam 音效 pid 22180；监控面板 `trigger cursor` 原生链 #299/#300 pid 9908）+ 帧 27 预警 `hour>=6`→`hour==6` 修复（离线 harness 四用例）+ 新增 native API（ReadObjectRect/WriteObjectPosition/ReadAlterableLive/WriteAnimation）+ 构建与离线三模式 PASS；新增 v1.16 进展块、§7.1 事实 +5、§7.2 第 10/12 项更新、§8 风险 3 行、状态表 Phase 8-QoL 更新、下一轮建议更新 | SL-Mod-Framework-Plan_v1.15_20261026-232009.md |
| v1.17 | 2026-10-27 00:3x | 【v1.17】标注：ThreatTelegraphs v0.4.0（Ballora 实时位置/到门倒计时、多通道告警并存、安全提示、帧 8 距离行）+ Overlay 字体全字库（`GetGlyphRangesChineseFull`/`font_path`/`font_size`，修复「氧」）+ DLL 0.6.0-p8；新增 v1.17 进展块、§7.1 事实 +5、§7.2 第 6 项更新、§8 风险 +3（字体显存/Ballora ETA/帧 31 快捷键）、状态表 Phase 7 更新、当前阶段/下一轮建议更新 | SL-Mod-Framework-Plan_v1.16_20261027-003549.md |
| v1.18 | 2026-10-27 01:0x | 【v1.18】标注：ThreatTelegraphs v0.4.1 按 task-4 时序研究修正 Ballora 模型（到门 4/10 门开改为 high+关门倒计时而非 attack、period=(50−Diff)×0.2s、3/9 high、通风口 5/6 死代码、入场 Diff 伪值 0.5s 稳定门控）；新增 v1.18 进展块、§7.1 事实 +3、§8 Ballora ETA 风险行更新、状态表 Phase 7 更新、当前阶段/下一轮建议更新；离线断言 30 项全过；docs/THREAT_RULES.md §14 | SL-Mod-Framework-Plan_v1.17_20261027-005248.md |
| v1.19 | 2026-10-27 03:1x | 【v1.19】标注：ThreatTelegraphs v0.5.0/v0.5.1（帧 31 Freddy 精确门/语音 + 状态字幕试点、Bonnet 行走倒计时/点鼻、flip 实时读取修正）、native 动态对象回退 rhObjectList+0x1278 与 ctx.game.object_rect/alterable、离线 64 断言；新增 v1.19 进展块、Phase 9 字幕系统设计提案、§7.1 事实 +5、§8 风险 +3、状态表/下一轮建议（按 m11423 回 P8 主线）、变更记录；docs/THREAT_RULES §17 | SL-Mod-Framework-Plan_v1.18_20261027-031454.md |
| v1.20 | 2026-10-27 05:4x | 【v1.20】标注：整合用户 Phase8.md 草案为 Phase 8（P8.0–P8.6，含首发范围冻结/布局 B/构建硬证据/日志轮转·诊断包·配置重置/素材审计/中文文档/发布门槛）；固化 m12195 拍板（布局 B、MIT+公开、GitHub v1.0.0、中文为主双语摘要、只带 HelloPlugin、身份 KIRARITenshiChan）；§0 新增原则 8 插件优先（m12162）；§1.3 首发不含示例插件；实施布局 B（launcher/DLL 路径推导、config/plugins/logs/sfx 相对框架根、deploy.sh、dev 工具路径、junction），版本 0.7.0-p8，离线三模式 PASS + 真机冒烟；§7.1 事实 +3、§8 风险 +5、状态表 Phase 8 进行中、下一轮建议改为 P8 子阶段；备份 SL-Mod-Framework-Plan_v1.19_20261027-054239.md（984 行 / 166,666? B / SHA e1b19200…），新版 1176 行 / 184,131 B / SHA d039a8f2…，标记数 19 |
| v1.21 | 2026-10-27 07:0x | 【v1.21】标注：Phase 8 收尾——P8.1 字节级可复现构建 + 最终发行 ZIP（805,520 B / SHA-256 `f8932e25…` / 18 文件）、P8.2 日志轮转·诊断包·配置重置·插件自动禁用、P8.3 MIT/第三方声明/素材审计、P8.4 `docs/release/` 六份中文文档、P8.5 干净安装→GUI→卸载→原版对照演练、版本冻结 1.0.0（DLL `fd83f195…` / launcher `22daac7f…` / harness `248ab25b…`）；新增 v1.21 进展块、P8.0–P8.5 完成状态、发布门槛 10/11 勾选、§7.1 事实 +4、§8 风险 +2 并更新日志路径行、状态表 Phase 8 更新、下一轮建议改为 P8.6 发布 + 插件优先；修正 v1.20 变更记录占位符；备份 `SL-Mod-Framework-Plan_v1.20_20261027-070501.md` | SL-Mod-Framework-Plan_v1.20_20261027-070501.md |

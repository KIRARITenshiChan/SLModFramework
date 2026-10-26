# Phase 2a 运行时只读探针报告（RUNTIME_PROBE）

- 日期：2026-10-25（本机时间 15:06–15:10）
- 目标：验证 Phase 2a 检查清单——注入可行性、进程/模块/窗口/显示事实、渲染与音频后端、PAMU/CCN 内存可读性、退出行为。
- 结论等级：**[实测]** = 本机运行日志/截图/文件实测；**[推断]** = 由实测与静态证据推理；**[待实测]** = 尚需后续阶段确认。
- 关联计划：v1.1 §Phase 2a、§2.2、P5、P6、P3。

## 0. 结论摘要

1. **注入可行且可复现**：32 位 launcher 用 `CreateProcessW(..., CREATE_SUSPENDED)` + 远程线程 `LoadLibraryW` 成功把只读探针 DLL 注入游戏（两次运行均成功，HMODULE=0x67E10000）。【实测】
2. **游戏可直接启动，无需 Steam、无 DRM**：直接运行 `SisterLocation.exe` 即进入标题页；父进程=launcher，进程内无 steam_api/overlay 模块。窗口标题 **"Sister Location"**，窗口类 **Mf2MainClassTh**，实际版本显示 **v1.121**（标题页左下角）。【实测】
3. **Fusion 打包资源在运行时释放到 `%TEMP%\\mrtXXXXXX.tmp\\`**：每次启动解包 8 个内嵌文件（mmfs2.dll、mmf2d3d9.dll、mmf2d3d8.dll、2 个 mfx、1 个 mfx、mp3flt.sft、waveflt.sft），解压后 SHA-256 与 EXE overlay 内的压缩负载逐一对应；旧临时目录不会自动清理。【实测】
4. **渲染后端确认 D3D9**：`d3d9.dll` 已加载，`mmf2d3d9.dll`（自临时目录）已加载并只导入 `Direct3DCreate9`；NVIDIA 用户态驱动 `nvd3dum.dll` 已加载（说明 HAL 设备已创建）。【实测】
5. **音频后端确认混合路径**：`DSOUND.dll` 已加载，mmfs2.dll 按 **序号 1** 导入 `DirectSoundCreate`；同时导入 WINMM 的 `mciSendCommandA/W`、`timeSetEvent` 等（MCI 路径存在）。实际的 jumpscare 样本走哪条路径 [待实测]（P6 探针）。【实测 + 待实测】
6. **PAMU（CCN 头）在进程内存中可读**：两次运行都在私有可读写堆区找到 `PAMU`，字段 productVersion=3、productBuild=286 与静态文件一致；但地址每次都变（0x016257D8 vs 0x00E76008），**禁止硬编码**；内存结构比文件头少一个 runtimeSubversion 字段，不能直接按文件字节布局解析。【实测】
7. **窗口为无边框全屏、按桌面分辨率 2560×1440 渲染**：窗口 style=0x17020000（无 WS_CAPTION / WS_THICKFRAME，含 WS_MAXIMIZE）、rect=client=屏幕=2560×1440、DPI=144（150%），进程 DPI 感知=2（Per-Monitor）。另有子窗口 **Mf2EditClassTh**（2560×1440，可见）承载输入/渲染。显示模式 2560×1440@32bpp/165Hz。【实测】
8. **退出行为异常（游戏原生）**：对主窗口发 WM_CLOSE（任务管理器式优雅关闭）后，进程以 **0xC0000005 (3221225477)** 退出；**未经注入的原版进程同样如此**，因此这是游戏/Fusion 全屏退出缺陷，不是探针引入。【实测，两次探针 + 一次原版对照】

## 1. 方法与安全边界

产物：
- 源码：`Workspace/tools/p2a_probe/launcher.c`、`probe.c`、`build.sh`
- 二进制：`Workspace/tools/p2a_probe/launcher32.exe`、`SLProbe32.dll`（PE32 i386，winlibs i686 gcc 16.2.0 构建）
- 日志：`Workspace/logs/probe/launcher-20261025-150634.log`（run1）、`launcher-20261025-150949.log`（run2）、`probe-run1.log`、`probe-run2.log`
- 截图：`Workspace/recon/out/p2a-window-title.png`（2560×1440 标题页）
- 抽出的运行期模块：`Workspace/recon/out/runtime-temp/`（mmfs2.dll 等 8 个文件，本地分析用，不公开分发）

安全边界（与计划 §0/§4 一致）：
- 探针 DLL **不写游戏内存**、不装 Hook、不调用游戏函数、不改配置/存档；只做 EnumProcessModules / VirtualQuery / 只读扫描 / EnumWindows。
- launcher 只写自己的日志、远程进程内 dll 路径字符串（注入参数）和临时参数页；结束后对游戏窗口发 WM_CLOSE，超时才 TerminateProcess。
- 不覆盖 Steam 原始文件；游戏可照常由 Steam 或直接启动。

构建/运行命令：

```bash
bash Workspace/tools/p2a_probe/build.sh
# 运行（PowerShell，日志在 <游戏目录>\\Workspace\\logs\\probe\\）
Start-Process -FilePath "<游戏目录>\\Workspace\\tools\\p2a_probe\\launcher32.exe" -ArgumentList '--wait-ms','120000'
```

## 2. 注入与生命周期（结果）

| 项目 | run1（15:06:34） | run2（15:09:49） | 证据 |
|---|---|---|---|
| 游戏 PID | 2744 | 27152 | launcher log / probe log |
| CreateProcess(SUSPENDED) | 成功 | 成功 | launcher log |
| 远程 LoadLibraryW 地址 | 0x7565D870 | 0x7565D870 | launcher log（kernel32 基址 0x75640000，同一启动会话内稳定） |
| 远程 LoadLibraryW 返回 HMODULE | 0x67E10000 | 0x67E10000 | launcher log |
| 探针线程启动 | 注入后主线程恢复前已加载 DLL | 同 | probe log `+00009.016 ==== SLProbe32 attached` |
| 完整回收 | probe.done 写入后 WM_CLOSE | 同 | launcher log |
| 退出码 | 3221225477 (0xC0000005) | 3221225477 | launcher log |

- 进程为 32 位 WOW64 进程，命令行仅 EXE 路径，工作目录=游戏目录，父进程=launcher。【实测】
- 注入发生在主线程恢复前；探针线程先 Sleep 9s 再采集，未观察到游戏启动失败或画面异常。【实测】
- Windows 加载了兼容性 shim `apphelp.dll` + `AcLayers.DLL`（Explorer/系统对旧 EXE 的 Application Compatibility 层），与游戏本身无关，但说明缺省环境存在兼容层；后续测试应记录该状态。【实测】

## 3. 模块与加载路径（关键子集）

两 run 共 100 个模块；以下为关键项（基址为 run2；完整的 100 项见 `probe-run2.log` 的 `MOD` 行）：

| 模块 | 基址 run2 | 大小 | 路径 | 说明 |
|---|---|---|---|---|
| SisterLocation.exe | 0x006B0000 | 0x10D000 | 游戏目录 | 外层 Fusion standalone stub（PE 镜像仅 ~1MB，其余为 overlay） |
| mmfs2.dll | 0x67D90000 | 0x78000 | `%TEMP%\\mrt635A.tmp\\` | Fusion 2.5 运行时（版本资源 3.0.286.6 "MMF2 Runtime Library"/Clickteam）【实测】 |
| mmf2d3d9.dll | 0x030B0000 | 0x11B000 | 同上 | D3D9 渲染器，本次实际使用；只导入 d3d9!Direct3DCreate9【实测】 |
| mmf2d3d8.dll | （未加载） | 432128 B（解包后） | 同上（仅在临时目录里） | D3D8 备选渲染器，本机未加载【实测】 |
| d3d9.dll | 0x69FD0000 | 0x179000 | System32 | Direct3D 9【实测】 |
| nvd3dum.dll | 0x05270000 | 0x2CA8000 | DriverStore（NVIDIA） | D3D9 用户态驱动；设备已创建【实测，间接证据】 |
| DSOUND.dll | 0x69260000 | 0x76000 | System32 | DirectSound【实测】 |
| WINMM.dll / winmmbase.dll | 0x72B60000 / 0x69F30000 | — | System32 | MCI/计时器路径【实测】 |
| DDRAW.dll | 0x7A400000（run1 0x799C0000） | 0xEC000 | System32 | 被 mmfs2 导入（DirectDrawCreate）【实测】 |
| Perspective.mfx | 0x10000000 | 0x8000 | 临时目录 | Fusion 扩展对象 DLL，已加载【实测】 |
| kcini.mfx | 0x10E00000 | 0x7000 | 临时目录 | 已加载【实测】 |
| kcclock.mfx | 0x67D50000 | 0x21000 | 临时目录 | 已加载【实测】 |
| mp3flt.sft | 0x041D0000 | 0x6000 | 临时目录 | 音频过滤/解码 DLL，已加载【实测】 |
| waveflt.sft | 0x041E0000 | 0x6000 | 临时目录 | 同上【实测】 |

重要含义：
- **mmf2d3d8.dll 虽被解包但未加载**：本机走 D3D9；P3 的 Hook 目标应是 `d3d9.dll` / `mmf2d3d9.dll`。【实测】
- 8 个解包文件中，mfx/sft 也按 DLL 加载（PE 文件），不是纯数据。【实测】
- 未发现任何 Steam / 第三方 overlay 模块；不存在 Steam DRM 组件。【实测】
- 模块基址跨运行有 ASLR 变化（如 mmf2d3d9 0x038E0000→0x030B0000、DDRAW 0x799C0000→0x7A400000），**任何硬编码基址都会在重启/重开游戏后失效**。【实测】

## 4. Fusion 打包资源的运行时解包（新事实）

overlay 内 8 个文件在启动时被解压到 `%TEMP%\\mrtXXXXXX.tmp\\`（目录名每次随机，如 mrt6B19.tmp / mrt615B.tmp / mrt635A.tmp），解压后哈希与 EXE 内负载一一对应：

| 文件 | 压缩负载(B) | 解压后(B) | 解压后 SHA-256（前 16 位） | 本机是否加载为模块 |
|---|---|---|---|---|
| mmfs2.dll | 470,528（原样） | 470,528 | 1cb098cfa35e4692 | 是 |
| mmf2d3d9.dll | 556,551 | 1,128,960 | fce87bf918787de7 | 是（实际渲染器） |
| mmf2d3d8.dll | 215,078 | 432,128 | beb827c2d35dec1b | 否 |
| Perspective.mfx | 5,860 | 15,360 | 090925a4cd961f22 | 是 |
| kcini.mfx | 5,398 | 28,672 | 52cdd0d5265dbe45 | 是 |
| kcclock.mfx | 49,996 | 110,592 | f2319a7359fbcb88 | 是 |
| mp3flt.sft | 4,215 | 24,576 | cd5bd7cc59eaf42b | 是 |
| waveflt.sft | 3,633 | 8,192 | 41136b09b033a20b | 是 |

- 除 mmfs2.dll 外其余 7 个在 pack 内是 zlib 流（头 `78 DA`），mmfs2.dll 为 raw PE。解压结果与临时目录文件 SHA-256 完全一致（三个不同 run 的临时目录内容哈希一致）→ 解包过程确定。【实测】
- 旧临时目录在进程退出后仍保留（已见 3 个）；这是 Fusion 的行为，不是探针造成的。是否影响 Steam 验证/磁盘策略见风险登记。【实测】
- 含义：**不存在“安装时替换资源文件”的路径**——每次启动都会重新解包，P6 只能走运行时 Hook（与 v1.1 的资源路线一致）。【实测支撑】

## 5. 窗口 / 显示 / DPI

| 项目 | 实测值 | 证据 |
|---|---|---|
| 主窗口标题 | `Sister Location` | window_list / probe WIN 行 |
| 主窗口类 | `Mf2MainClassTh` | 同上 |
| 子窗口类 | `Mf2EditClassTh`（client 2560×1440，visible=1） | probe `CHILD` 行 |
| style / exstyle | 0x17020000 / 0x00000010 | probe WIN 行 |
| rect / client | (0,0)-(2560,1440) / 2560×1440 | 同上 |
| style 解读 | WS_VISIBLE\|WS_CLIPSIBLINGS\|WS_CLIPCHILDREN\|WS_MAXIMIZE\|WS_MINIMIZEBOX，无 WS_CAPTION/WS_THICKFRAME → **无边框** | 常量解读 |
| 屏幕 / 显示模式 | 2560×1440 / 2560×1440@32bpp 165Hz，单显示器 | probe DISPLAY/DISPLAYMODE |
| 窗口 DPI | 144（150% 缩放） | probe dpi=144 |
| 进程 DPI 感知 | 2 = Per-Monitor DPI Aware | probe DPI awareness |
| 实际游戏版本 | 左下角显示 `v 1.121` | `recon/out/p2a-window-title.png` |

- 结论：本机表现为**无边框全屏**，游戏按桌面分辨率（2560×1440）出图，内部 1280×720 的场景分辨率被缩放。P3 的 ImGui overlay 必须处理 150% DPI 与“窗口=全屏”尺寸；输入焦点实际落在 `Mf2EditClassTh` 子窗口（后续 P3 需对哪个窗口做 WndProc 子类化需实测）。【实测 + 推断】
- 该截图为标题页：NEW GAME / CONTINUE 1 / EXTRAS / CUSTOM NIGHT、`SISTER LOCATION`、右下 `Copyright © 2016 Scott Cawthon`。【实测】

## 6. 渲染后端证据（P3 的输入）

1. `mmf2d3d9.dll` 已加载（不在 System32，而在临时解包目录），说明运行时选择 D3D9 渲染器；`mmf2d3d8.dll` 未加载。【实测】
2. `mmf2d3d9.dll` 的导入表只含 `d3d9.dll!Direct3DCreate9`（设备/交换链通过 COM vtable 调用，无逐个函数导入）；因此 overlay 可（a）IAT patch `Direct3DCreate9` 或（b）inline hook `d3d9.dll` 的 `IDirect3DDevice9::Present`/Reset。【实测 + 推断】
3. `nvd3dum.dll` 已加载 → D3D9 HAL 设备已创建；本机 NVIDIA GPU。【实测】
4. 由于窗口是无边框最大化窗口（非独占全屏的典型 `WS_POPUP`+显示模式切换），当前实现很可能是**窗口化交换链 + 无边框窗口**；D3D9 设备参数（D3DPRESENT_PARAMETERS：Windowed/BackBufferFormat/SwapEffect/多采样/VSync）**未实测**，必须在 P3 用 hook 记录。【推断，待实测】
5. 目前**没有**任何 d3d9 代理/overlay 残留；游戏目录无 d3d9.dll（不会被我们的 DLL 搜索路径劫持）。【实测】

## 7. 音频后端证据（P6 的输入）

- mmfs2.dll 导入：`DSOUND.dll` **序号 1**（=DirectSoundCreate）、`WINMM.dll` 的 `mciSendCommandA/W`、`timeSetEvent`、`timeGetTime`、`timeKillEvent`、`DirectDrawCreate`、文件/堆/窗口 API。【实测（objdump）】
- 运行期 `DSOUND.dll`、`WINMM.dll`、`winmmbase.dll` 均加载；`mp3flt.sft`、`waveflt.sft` 作为 DLL 加载。【实测】
- 推断：样本音效（scream/jumpscare）大概率经 DirectSound 缓冲播放；音乐/MCI 路径可能走 `mciSendCommand`。**不能只 Hook 一条路径**；P6 必须先用探针记录 jumpscare 场景实际调用（CreateSoundBuffer/Play/SetVolume 或 mciSendCommand），再决定 Hook 点。【待实测，已列入 P6 前置】
- 音量控制：若游戏用 DirectSound 的 SetVolume 而非系统音量，SoftJumpscares 可在缓冲级降音量——这正是 v1.1 计划 P6 的假设，现证据支持，但需实机验证缓冲识别。【推断】

## 8. PAMU / CCN 头在进程内存中（P5 的输入）

两次运行都在进程内存找到 `PAMU` 结构（只读扫描，未修改）：

| run | 地址 | 所在区域 | 字段（偏移 +4/+8/+12/+16） | 前 20 字节 |
|---|---|---|---|---|
| run1 | 0x016257D8 | 私有堆（未记录 region） | 0x302 / 3 / 286 / 112 | `50 41 4d 55 02 03 00 00 03 00 00 00 1e 01 00 00 70 00 00 00` |
| run2 | 0x00E76008 | base=0x00E76000 size=0xC9000，MEM_PRIVATE，PAGE_READWRITE | 0x302 / 3 / 286 / 112 | 同上 |

解读【实测 + 推断】：
- `PAMU` + runtimeVersion=0x302(770) + productVersion=3 + productBuild=286 与静态文件头一致；说明运行时确实把 CCN 头读进内存。
- 内存结构与文件头**不同**：文件头在 runtimeVersion 后还有 runtimeSubversion=0，而内存里没有该字段（或已被运行时结构替代），所以**不能直接按文件字节布局解析内存**。
- 命中点位于堆分配块起始 +8（0x00E76008），疑为分配器 8 字节头，需在 P5 用注入适配器做结构验证。
- 地址每次启动都变（0x016257D8 → 0x00E76008），**任何硬编码地址方案直接否决**；P5 必须走“运行时扫描 + 结构校验/签名”或从模块导出/CRunApp 指针链获取。【实测】
- 仅找到 CCN 头结构，尚未定位 CRunApp、帧对象表、计数器（Freddy/danger level 4/got you/final stage 等）。这些是 P5 的工作项，计划 v1.1 已列候选读取链，本报告确认“内存可读”但未确认“指针路径稳定”。

## 9. 退出行为（重要风险，需写入 P3/P8）

- 用 `taskkill`（等价 WM_CLOSE，优雅）关闭**原版未注入**进程：退出码 **-1073741819 = 0xC0000005**（访问违例）。【实测】
- 两次注入探针运行（同样 WM_CLOSE 关闭）：退出码 **3221225477 = 0xC0000005**。【实测】
- 即：**WM_CLOSE 退出时游戏崩溃是原生行为**，与注入无关；可能发生在全屏 D3D9 设备销毁路径。影响：
  1. P3 的“安全关闭/随进程退出”必须假设 `DLL_PROCESS_DETACH` 可能来不及执行；插件状态/配置必须在运行期持续落盘，不能只依赖退出时保存。
  2. 测试矩阵不能把“退出码非 0”当作框架缺陷；要以“原版对照 + 窗口是否关闭 + 无残留进程”判定。
  3. 正式使用建议用游戏内退出/标题页 EXIT（待实测是否同样崩溃）或 Steam 停止；P8 文档需写明该已知缺陷与预期退出码。
- 是否与全屏/DPI/显卡驱动相关 [待实测]；不同退出方式（Alt+F4、游戏菜单）需在 P3 测试。【待实测】

## 10. 计划 v1.1 → 后续阶段的影响

已消除的未知（可写入 v1.2 计划/验收）：
- 注入机制（挂起+远程 LoadLibraryW）在目标版本可用；DLL 可为 32 位自研、可在游戏主循环前加载。
- 渲染=D3D9（mmf2d3d9），窗口=无边框全屏+子窗口 Mf2EditClassTh，DPI/分辨率事实明确。
- 音频=DirectSound(WINMM/MCI 并存)，Hook 点候选明确但仍需场景实测。
- 资源不可安装期替换（每次启动解包到随机临时目录）→ P6 只能运行时 Hook。
- PAMU 内存可读但布局与文件不同、地址随机 → P5 适配器必须带结构校验/扫描。

仍需在后续阶段实测（未在本报告解决）：
1. D3D9 设备/交换链具体参数（Present/Reset 参数、是否窗口化、VSync、backbuffer 格式、多采样）——P3 Hook 记录。
2. 输入路由：键盘/鼠标消息落在主窗口还是 Mf2EditClassTh；ImGui 输入屏蔽要在哪个窗口做子类化——P3。
3. jumpscare 样本的实际播放调用链（DirectSound 缓冲 vs MCI）——P6 前置探针。
4. CRunApp / 帧对象 / 计数器的指针链与稳定性——P5（需要注入式读取适配器）。
5. 标题页→Night2 帧 9→Night5 帧 27→jumpscare 的实机时序与状态值——P5/P7。
6. 退出方式差异（WM_CLOSE 崩溃是否在所有退出路径出现）与残留进程——P3/P8。

## 11. 复现步骤（本机）

1. 构建：`bash Workspace/tools/p2a_probe/build.sh`（winlibs i686 gcc 16.2.0；输出 PE32 i386）。
2. 运行：`Start-Process ...\launcher32.exe -ArgumentList '--wait-ms','120000'`。
3. 采集：launcher 注入 → 探针在 +9s / +20s 两次快照 → 写 `Workspace/logs/probe/probe.done` → launcher 发 WM_CLOSE 并等待退出。
4. 读取：`Workspace/logs/probe/probe-run*.log`、`launcher-*.log`；临时解包目录 `%TEMP%\\mrt*.tmp`。
5. 原版对照：直接 `Start-Process SisterLocation.exe`，18s 后 `taskkill /PID <pid>`，观察退出码（本机为 0xC0000005）。

## 12. 证据文件索引

| 文件 | 内容 | 大小 |
|---|---|---|
| `logs/probe/launcher-20261025-150634.log` | run1 注入与退出日志 | 1004 B |
| `logs/probe/launcher-20261025-150949.log` | run2 注入与退出日志 | 1004 B |
| `logs/probe/probe-run1.log` | run1 探针快照（模块/窗口/PAMU） | 29,938 B |
| `logs/probe/probe-run2.log` | run2 探针快照（含增强项：显示模式、子窗口、PAMU region、DPI） | 31,297 B |
| `recon/out/p2a-window-title.png` | 标题页截图（2560×1440，v1.121） | 742,973 B |
| `recon/out/runtime-temp/*` | 运行期解包出的 8 个模块（本地分析，不公开分发） | 约 1.9 MB |
| `tools/p2a_probe/*.c` | 探针源码（可复现） | — |

---
*本报告由 Lead 于 2026-10-25 生成；所有结论按 [实测]/[推断]/[待实测] 标注，未标注处不得当作已验证事实。*

# Phase 0 前置勘察（PREFLIGHT）

- 状态：已完成（静态部分 2026-10-25；运行时部分已由 Phase 2a 探针于 2026-10-25 补全，见 §6/§7）。
- 对应计划：Phase 0（v1.1 第 101–123 行）；证据等级图例遵循 `Workspace/README.md`：
  - 【实测】本机直接测量/命令输出；
  - 【静态】从游戏文件或数据中静态解析得到；
  - 【推断】由多条证据推理，尚未运行时验证；
  - 【待实机】必须运行游戏才能确认。

## 1. 目标版本与安装形态

| 项 | 值 | 证据 |
| --- | --- | --- |
| 游戏目录 | `E:\SteamLibrary\steamapps\common\Five Nights at Freddy's Sister Location` | 【实测】 |
| 目录内容 | 仅 1 个文件 `SisterLocation.exe`，无外部 cfg/资源/dll | 【实测】 |
| EXE 大小 | 950,556,728 字节 | 【实测】 |
| EXE SHA-256 | `c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73` | 【实测】 |
| 文件 mtime | 2026-10-25 05:14:05（本机时间） | 【实测】 |
| PE 架构 | PE32 / i386（32 位 x86），GUI 子系统 | 【实测】 |
| 链接器时间戳 | 2016-06-30T09:03:15Z（UTC） | 【实测】 |
| Fusion 版本 | productVersion=3，**productBuild=286**，buildTypeName="Windows EXE Application" | 【静态】application.json |
| 应用名/作者 | "Sister Location" / "Scott Cawthon" | 【静态】Name/Author chunk |
| Steam AppID | 506610（appmanifest_506610.acf） | 【实测】 |
| Steam buildid | 1497593（= TargetBuildID） | 【实测】 |
| Steam depot | 506611，manifest 749438696463073559 | 【实测】 |
| Steam SizeOnDisk | 950,556,728，与 EXE 大小一致 | 【实测】 |

- 版本推断：build 286 + 2016-06-30 链接时间与 SL PC 末版 1.12/1.121（2016-12）构建期吻合；Steam buildid 1497593 很可能就是该末版包。【推断，中高】建议在需要时用 `steamcmd +app_info_print 506610` 复核（本机未装 steamcmd，SteamDB 对脚本有 Cloudflare 拦截）。
- 无 Steam DRM/集成迹象：外层 EXE 不导入 `steam_api*.dll`，无 SteamStub/.bind 节区；PCGamingWiki 亦记录本作为单个免 DRM 可执行文件。**因此可绕过 Steam 直接启动**，但本项目仍以正版安装为准，不改动 Steam 文件。【实测 + 外部资料，中高】
- 单一 EXE 形态意味着：图像/音频/事件全部内嵌在 EXE 的 949 MB overlay 中，**不存在可直接替换的外部资源文件**。这是 Phase 6「资源替换」路线被改为运行时 DirectSound Hook 的根本原因。【实测】

## 2. 可复现指纹

- 采集脚本：`Workspace/recon/scripts/pe_probe.py` 至 `pe_probe4.py`；结果 JSON 在 `Workspace/recon/out/`。
- 锁定指纹：`Workspace/analysis/data/fingerprint.json`。
- 校验脚本：`Workspace/tools/verify_fingerprint.py`（本机实测：退出码 0 表示与指纹一致）。
- 判定规则（写入 P8 版本门禁）：SHA-256 或大小任一不符 → 视为「未验证版本」，禁用依赖内部状态/内存布局的插件，日志明确报告；不得静默沿用旧偏移。

## 3. 本机开发环境（2026-10-25 实测）

| 项 | 值 |
| --- | --- |
| OS | Windows_NT 10.0.26100 x64，16 核 |
| Python | 本机 PATH 下 python（仅标准库用于 recon，无第三方依赖） |
| Node.js | 本机 PATH 下 node（用于解析 JSON/事件文本） |
| 32 位 C++ 工具链 | `D:\devtools\winlibs-i686\mingw32\bin\gcc.exe`（winlibs i686-posix-dwarf-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3，target `i686-w64-mingw32`）【实测】 |
| 32 位 Windows 头/库 | 该工具链内含 `include/d3d9.h`、`include/dsound.h`，`lib/libd3d9.a`、`lib/libdsound.a`、`lib/libpsapi.a`【实测】 |
| 构建系统 | CMake/Ninja 可用（scoop shims），但当前探针用直接 gcc 命令即可 |
| 64 位工具链 | 系统另有 x64 mingw/clang，**不可用于注入 32 位游戏进程** |
| 工具链安装记录 | `Workspace/tools/install_winlibs_i686.py` + 下载缓存 `Workspace/tools/cache/winlibs-i686.zip`（SHA-256 已记录） |

## 4. 运行环境预期（静态配置，待 Phase 2a 实测）

- 窗口：header 中 windowWidth=1280、windowHeight=720；flags: `MaximizedOnBoot=true`、`NoHeading=true`、`DontDisplayMenu=true`、`OneFile=true`、`Protected=true`；newFlags: `VSync=true`。→ 预期为**无标题栏、启动即无边框全屏 1280×720**（PCGamingWiki 描述一致）。【静态 + 外部资料，中高】
- 渲染器：otherFlags `Direct3D9or11=true`、`Direct3D8or11=false`，overlay 内嵌 `mmf2d3d9.dll`（备选 `mmf2d3d8.dll`）→ 预期 D3D9 渲染路径。【静态，高】实际加载哪个 DLL 待 P2a 实测。
- 音频：mmfs2.dll 导入 `DSOUND.dll`（DirectSoundCreate）与 `WINMM.dll`；扩展含 mp3flt.sft/waveflt.sft → 预期 DirectSound + 内嵌解码器。【静态，中高】实际播放缓冲与音量调用待 P2a/音频 Hook 实测。
- 输入：Fusion 运行时通过 Windows 消息/键盘状态读取；全屏与窗口切换行为待实测。

## 5. 可回滚与备份策略

- 永不覆盖 `SisterLocation.exe` 及 Steam 目录内其它原始文件；所有产物在 `Workspace/` 下。
- Steam 客户端「验证游戏文件完整性」可作为官方恢复手段；记录 buildid=1497593 以便识别更新。【实测】
- 计划文件每次修改前备份到 `Workspace/plan-backups/` 并在 `plan-changes/CHANGELOG.md` 记录；备份校验见 `plan-backups/SHA256SUMS.txt`。
- 分析产物（frame 布局、事件转储、抽出的 mmfs2.dll）只留本地 `Workspace/recon/`，不进入公开分发。【项目原则 §0.6】

## 6. Phase 2a 检查清单（2026-10-25 已由探针实测，结果见 `docs/RUNTIME_PROBE.md`）

| # | 检查项 | 状态 | 结果摘要 |
|---|---|---|---|
| 1 | 启动方式/进程/窗口/DPI | ✅ 实测 | 直接运行可玩（无需 Steam）；32 位 WOW64 进程；标题 `Sister Location`、类 `Mf2MainClassTh`、无边框全屏 2560×1440、DPI 144/150%、Per-Monitor |
| 2 | 实际加载模块与基址 | ✅ 实测 | mmfs2/mmf2d3d9/d3d9/DSOUND/WINMM/DDRAW/nvd3dum 均加载；mmf2d3d8 未加载；基址有 ASLR |
| 3 | 渲染 API 参数（Present/Reset） | ⏳ 未完成 | 已确认 D3D9（mmf2d3d9 只导入 `Direct3DCreate9`），设备参数需 P3 Hook 记录 |
| 4 | 音频后端确认 | ✅ 部分 | DSOUND 已加载、mmfs2 按序号 1 导入 `DirectSoundCreate`；WINMM/MCI 并存；jumpscare 实际调用链待 P6 前置探针 |
| 5 | PAMU/CRunApp 内存可读性 | ✅ 部分 | 内存中找到 PAMU（build 286），但地址随机、布局与文件头不同；CRunApp/计数器指针链未定位（P5） |
| 6 | 场景切换与 jumpscare 实机时序 | ⏳ 未完成 | 需 P5 状态适配器 + 实际游玩；探针未自动进入 Night2/Night5 |
| 7 | 退出行为与残留 | ✅ 实测 | WM_CLOSE 关闭时进程以 0xC0000005 退出，**原版未注入同样如此**（原生缺陷）；未观察到残留进程 |

**验收**：1/2/4/5/7 项已有带时间戳日志证据（见 RUNTIME_PROBE.md 第 12 节索引）；3/6 项转入 P3/P5 继续。

## 7. Phase 2a 实测结果（2026-10-25，摘要）

完整报告见 `docs/RUNTIME_PROBE.md`。要点：

- **注入**：launcher（`CreateProcessW(CREATE_SUSPENDED)` + 远程 `LoadLibraryW`）两次运行均成功，游戏正常进入标题页；可作 P2/P3 的加载机制。【实测】
- **窗口**：标题 `Sister Location`、类 `Mf2MainClassTh`、子窗口 `Mf2EditClassTh`（承载输入/渲染）；style 0x17020000（无 CAPTION/THICKFRAME，含 MAXIMIZE）→ 无边框全屏；client=屏幕=2560×1440；DPI=144（150%），进程 Per-Monitor DPI Aware；标题页显示版本 **v1.121**。【实测】【截图 `recon/out/p2a-window-title.png`】
- **渲染**：D3D9 确认（mmf2d3d9 已加载 + nvd3dum 已加载；mmf2d3d8 未加载）。mmf2d3d9 仅导入 `d3d9.dll!Direct3DCreate9`，设备函数走 COM vtable → P3 可 IAT patch 或 inline hook Present/Reset。【实测 + 推断】
- **音频**：DSOUND 已加载，mmfs2 导入 `DirectSoundCreate`（序号 1）+ WINMM MCI；jumpscare 走哪条路径待 P6 实测。【实测】
- **资源解包**：Fusion 每次启动把 overlay 内 8 个文件解包到 `%TEMP%\mrt*.tmp`（随机目录、旧目录不清理、哈希三 run 一致）→ **安装期替换不可行**，P6 只能运行时 Hook。【实测】
- **内存**：PAMU 头存在于私有堆（build 286），但地址每次启动变化（0x016257D8 / 0x00E76008）、内存布局比文件头少 runtimeSubversion → 禁止硬编码，P5 需结构校验/扫描。【实测】
- **退出缺陷**：WM_CLOSE → 0xC0000005，原版对照同 → **游戏原生行为**；P3 安全关闭不能依赖退出时保存，P8 测试需以原版对照判定。【实测】

**对计划的影响**：P2a 目标达成；P3 的渲染/窗口输入假设已被实测收窄；P5/P6 的“运行时读取/Hook”路线得到证据支持但仍需各自的消费级探针。

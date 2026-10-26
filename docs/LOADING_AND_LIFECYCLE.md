# 加载与生命周期（Phase 2 交付）

- 状态：**完成**（2026-10-25），框架版本 `0.1.0-p2`
- 对应计划：Phase 2「注入/启动机制与基础设施」
- 相关产物：`Workspace/framework/`、`Workspace/logs/framework/`、`Workspace/recon/out/p2-framework-title.png`
- 证据等级：本文所有运行时数值均为 **实测**（2026-10-25 15:35–15:40，本机同一 EXE）；推断项单独标注。

---

## 1. 组件与职责

| 组件 | 路径 | 职责 |
|---|---|---|
| loader | `Workspace/framework/bin/launcher32.exe` | 启动游戏、校验 EXE 指纹、注入框架 DLL、等待初始化、自动化回归（可选自动关窗）、写加载日志 |
| framework DLL | `Workspace/framework/bin/SLModFramework32.dll` | 进程内框架最小骨架：日志、运行时版本门禁、生命周期与心跳；**只读，不 Hook、不写游戏内存** |
| 构建脚本 | `Workspace/framework/build.sh` | winlibs i686 gcc 16.2.0（`D:/devtools/winlibs-i686/mingw32`）交叉构建 32 位 PE |
| 日志目录 | `Workspace/logs/framework/` | loader 日志、DLL 日志、init 标记、指纹 sidecar |

设计原则（与计划 §0/§4 一致）：先观测后修改；失败必须可诊断；框架任何失败都不得让原版无法游玩；DllMain 只创建线程，不做加载/等待/IO 之外的重活；不依赖退出回调保存状态。

## 2. 加载流程（实测时序）

1. loader 读取目标 EXE，流式计算全文件 SHA-256（950,556,728 B，约 4–5 s，约 190 MB/s），与已知指纹比较；不匹配直接退出（exit 3），**不创建游戏进程**。
2. `CreateProcessW(..., CREATE_SUSPENDED)` 挂起启动；写 `launch-<pid>.json`（exe 路径/SHA-256/大小/是否已验证）供 DLL 记录。
3. `VirtualAllocEx + WriteProcessMemory + CreateRemoteThread(kernel32!LoadLibraryW)` 注入 `SLModFramework32.dll`（绝对路径）；远程线程返回的非零 HMODULE 作为成功标志（实测 `0x67EB0000`）。
4. `ResumeThread` 恢复游戏；DLL 的 DllMain 只创建 worker 线程。
5. worker 线程按序执行：初始化日志 → 读 exe 信息与 sidecar → **提前安装 `d3d9!Direct3DCreate9` Hook（`InstallEarly`，P3 起；`hook_d3d9=0` 可关）** → 等待 `mmfs2.dll` 加载 → 模块快照 → 运行时 PAMU 版本门禁（带重试）→ 写 init 标记 → 等待渲染/音频模块出现 → 心跳。
6. loader 轮询 `init-<pid>.done`（默认最长 30 s），把 status 打到控制台/日志：`ok` / `degraded` / `fail`。
7. 交互运行时 loader 等待游戏退出；`--test-exit N` 时在初始化后 N 秒枚举 pid 主窗口并 `PostMessage(WM_CLOSE)`，等待退出并记录退出码。

### 2.1 关键实测时间点（同一台机器，多次运行）

| 事件 | 实测值 | 说明 |
|---|---|---|
| `mmfs2.dll` 可被 `GetModuleHandle` 看到 | 注入后 **125–235 ms**（P2 测量） | P3 起 worker 先做 `InstallEarly`（LoadLibrary d3d9 + MinHook，约 80–120 ms），到达等待点时通常已是 `waited_ms=0`（17:15 实测） |
| 运行时 PAMU 头首次可定位 | 0–312 ms（有时需要第 2 次扫描） | 见 §4.1 的竞态与重试 |
| `mmf2d3d9.dll` 加载 | PAMU 门禁后 **250–437 ms** | 渲染器是**后加载**的，不能启动即 Hook |
| `d3d9.dll` 加载 | 与 `mmf2d3d9.dll` 同时（`wait=0`） | 实测基址 0x69FD0000 |
| `dsound.dll` 加载 | 同上（`wait=0`） | 实测基址 0x69260000 |
| `mmf2d3d8.dll` | 全程未加载 | 与 P2a 结论一致：实际渲染器是 D3D9 |
| 标题页可截屏 | 注入后约 13 s 内已稳定 | 截图见 §7 证据 |

> 结论（P3 直接依赖）：**框架不能在 DLL 加载时立刻 hook 设备 vtable**（设备还不存在），但可以在设备创建前 hook `d3d9!Direct3DCreate9` 导出函数——P3 的 `InstallEarly` 即此路线：先截 `Direct3DCreate9`，等 `CreateDevice` 成功拿到 `IDirect3DDevice9*` 后再 hook 其 vtable[16] `Reset`/vtable[17] `Present`。这样即使 `mmf2d3d9.dll` 是后加载的也不会错过设备创建。

## 3. 版本门禁

三层，任一层失败都必须显式记录，不允许静默继续：

1. **文件指纹（loader，强门禁）**：SHA-256 = `c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73`；大小 950,556,728 B。不匹配 → 退出码 3，不注入、不启动。`--no-verify` 仅供诊断：继续注入，但 sidecar 写 `verified=0`、DLL 日志 `loader_fp| … verified=0`，版本门禁随后判 `fail / fingerprint_mismatch`（见 §3.3）。
2. **运行时结构指纹（DLL）**：只读扫描已提交的**私有内存**（MEM_PRIVATE）找 `PAMU` 魔数，要求 `runtimeVersion==0x302` 且 `productBuild==286`。找到 → `ok`；mmfs2 已加载但 15 s 内找不到 → `degraded`（不启用依赖内部结构的插件）；mmfs2 60 s 未加载 → `fail`。
3. **模块版本（已具备证据，P2 未写成硬门禁）**：`mmfs2.dll` 文件版本 3.0.286.6（P2a 实测）；P3 之后可加入产品/运行时版本联合校验。

### 3.1 PAMU 扫描的实测约束

- PAMU 头地址**每次启动都不同**（实测 0x01447630 / 0x01009840 / 0x009A6B18 / 0x00A98BB0 / 0x00B0CC60 / 0x00A7B6D0 / 0x00ED0B70），**禁止硬编码**；结构也比文件头少 `runtimeSubversion` 字段（内存布局：PAMU + runtimeVersion + productVersion + productBuild + 0x70）。
- 内存里存在**假候选**：EXE 映像区偏移 0x006B378C 处有字节序列 `PAMU`，但字段是垃圾（rt=0x1112850F、pb=900）。因此必须校验字段，不能见魔数就认；现在按区域类型过滤后（见下）该假候选已不再进入扫描。
- 扫描策略（2026-10-25 16:28 加固后）：只 `VirtualQuery` 遍历 **MEM_PRIVATE + committed + 非 GUARD/NOACCESS/不可读** 区域；用 **`ReadProcessMemory(GetCurrentProcess())` 以 64 KiB 块读入缓冲区**再找魔数，**不做裸指针解引用**；单区上限 64 MiB；总预算 512 MiB / 12 s；命中后逐字段校验。结束/命中都记录 `pamu| scan ok/done regions=… bytes=… read_fail=… elapsed_ms=…`。实测只需 **14–15 个私有区 / 约 0.31–0.43 MB / <1 ms** 就命中，扫描面比旧实现小两个数量级。
- **竞态**：PAMU 头并不总在 mmfs2 加载后立刻可读。实测有一次首扫未命中（进程 20244，首扫 11 ms 结束、found=0），随即被判 `degraded`。修复：**每 300 ms 重试、最长 15 s**，并记录 attempts/elapsed；修复后 6 次运行均 `ok`（其中一次需要 2 次扫描、312 ms）。→ 该重试逻辑不是冗余，是稳定性必需。

### 3.2 启动偶发挂起（根因已定位并修复，16:24–16:30）

- **现象**：16:06（pid 19988）、16:24（pid 7776）、16:26（pid 26228）三次运行，注入成功、框架日志正常写到模块快照，但游戏主窗口 60 s 内始终不出现、loader 等不到 init 标记（rc=2）；日志最后一行固定是 EXE 映像假候选 `pamu| cand addr=006B378C region_base=006B1000 region_size=0008A000 rt=0x1112850F pv=2160459776 pb=900`，之后无 `attempts=`、无 gate/init 行；进程仍存活（Responding）。
- **根因**：旧 `ScanPAMU` 用裸指针遍历**所有**已提交内存（含映像/映射区）。游戏启动期并发释放/重保护页面时，VirtualQuery 与解引用之间页面失效 → worker 线程发生 access violation。线程未处理 AV，锁屏会话下 WER 会挂起整个进程（对话框在安全桌面不可见），于是游戏主线程也停在建窗之前、窗口永不出现。
- **修复**：`ScanPAMU` 只扫 MEM_PRIVATE，并用 `ReadProcessMemory(GetCurrentProcess())` 分块读取（不可读时返回 FALSE 而不是崩溃），假候选随映像区一起被排除。日志加 `scan ok/done regions/bytes/read_fail/elapsed_ms` 便于回归判定。
- **回归**：修复后 16:28:56 / 16:29:28 / 16:30:00 / 16:30:56 连续 8 次运行全部 `gate=ok`（`regions=14–15 bytes≈0.31–0.43 MB read_fail=0 elapsed_ms=0`）、窗口正常创建、无挂起（16:30:56 那次还带 F2 热键与输入吞噬自动化）。
- **教训**：游戏进程内的"只读"探测也必须假定内存随时可能被改写/释放；跨线程共享的游戏内存读取一律走安全读取封装，并保留可观测的扫描统计。

### 3.3 未验证版本负例（17:09–17:10 实测）

- 方法：把 `SisterLocation.exe` 复制到 `Workspace/tmp/fp-test/` 并**追加 1 字节**（不改原文件），SHA-256 变为 `93df18d4…3134341`、大小 950,556,729 B；用 `launcher32.exe --exe <副本> --no-verify --test-exit 15` 走完整注入。
- loader：`FINGERPRINT MISMATCH expected=c60e…2c73` → `--no-verify: continuing on unverified build (framework will report degraded)` → 注入成功；框架 `loader_fp| sha256=93df… size=950556729 verified=0`、`WARNING sha256 differs from expected build fingerprint`、`gate| fingerprint_ok=0`、`gate| fail: exe fingerprint not the verified build`；init 标记 `status=fail detail=fingerprint_mismatch`；loader 打印 `framework initialized: FAIL status reported` 并以 **rc=2** 退出（游戏本身仍能从副本启动并因原生 WM_CLOSE 缺陷以 0xC0000005 结束）。
- 该负例同时验证了 `verified` 语义：17:10 前 `ReadSidecarFingerprint` 在解析到 sha256 时就把 `verified` 置 1，导致不匹配副本也报 `verified=1`；已改为读取 sidecar 的 `verified=` 字段（日志前后对比 `logs/probe/fp-negative-framework.log` vs `fp-negative2-framework.log`）。
- 顺带修复：`--exe`/`--dll` 用正斜杠时，loader 的 CreateProcess 工作目录拆分只认 `\\`，会以 `err=267 (ERROR_DIRECTORY)` 启动失败；现在参数解析后统一 `NormalizeSlashes()` 再使用。证据 `logs/probe/fp-negative2.out`。

## 4. 生命周期与退出

- worker 线程：初始化完成后每 5 s 写一条 `alive| uptime_ms=... mmfs2=...` 心跳；上限 12 h（防长期挂机日志无限增长），到上限写 `worker exit (heartbeat cap)` 后线程结束，框架不再做任何事（P2 无后台任务）。
- **没有可靠 shutdown 回调**：实测 `WM_CLOSE` 会让进程以 `0xC0000005`（3221225477）结束；无注入的原版对照同样如此（P2a）。这是游戏原生缺陷，不是框架引入。因此框架的持久状态只允许落在**已 flush 的日志/标记文件**里，不允许依赖 `DLL_PROCESS_DETACH` 或 atexit 做收尾。
- P2 无 Hook、无设备/线程/全局状态需要卸载；P3 安装 Hook 后必须假定“进程可能直接消失”，不做退出清理也可接受，但**不得**因为退出崩溃影响游戏内行为。
- 多次启退（6 次框架运行 + 原版对照）未出现启动失败、卡死或框架相关崩溃；退出码均与原生对照一致。

## 5. 文件与日志约定

| 路径 | 内容 |
|---|---|
| `Workspace/framework/bin/` | `launcher32.exe`、`SLModFramework32.dll`（构建产物，可随时重建） |
| `Workspace/logs/framework/loader-<yyyymmdd-HHMMSS>.log` | loader 全流程日志（指纹、pid、HMODULE、init 状态、退出码） |
| `Workspace/logs/framework/slmodf-<yyyymmdd-HHMMSS>-<pid>.log` | DLL 日志（版本、指纹 sidecar、mmfs2 等待、模块、PAMU 候选、门禁、late modules、心跳） |
| `Workspace/logs/framework/init-<pid>.done` | 初始化结果（status/detail/version/pid/pamu/exe），loader 判据 |
| `Workspace/logs/framework/launch-<pid>.json` | loader 写入的指纹 sidecar，供 DLL 记录 |

日志均为 ASCII，逐行 flush；路径不进游戏目录之外（游戏目录内只写 `Workspace/`）。

## 6. 实测记录汇总（2026-10-25）

| # | pid | init | PAMU 地址 | 备注 |
|---|---|---|---|---|
| 1 | 5888 | ok | 0x01447630 | 首次端到端成功；WM_CLOSE 退出 0xC0000005 |
| 2 | 5864 | ok | 0x01009840 | 复现成功 |
| 3 | 8708 | ok | 0x009A6B18 | 注入后截取标题页（§7），40 s 自动关窗 |
| 4 | 20244 | degraded | — | 首扫竞态（无重试），暴露问题 |
| 5 | 20060 | ok | （attempts=2, 312 ms） | 重试逻辑验证 |
| 6 | 26016 | ok | 0x00A98BB0 | 重跑验证 |
| 负例 | — | — | — | `--exe notepad.exe`：SHA-256 不匹配 → exit 3，未启动进程 |

指纹校验耗时约 4–5 s（128 MiB 一档打印进度）；这是每次启动的固定成本，P8 可考虑缓存校验结果（按文件大小+mtime 失效），P2 不做。

## 7. 证据

- 截图（注入框架后标题页，框架此时不绘制任何东西，画面与原版一致）：`Workspace/recon/out/p2-framework-title.png`（2560×1440，窗口 `Mf2MainClassTh`）。
- 日志：`Workspace/logs/framework/`（6 组 loader+DLL 日志与 init 标记）。
- P2a 报告：`Workspace/docs/RUNTIME_PROBE.md`（注入/模块/窗口/D3D9/DSOUND/%TEMP% 解包/PAMU/退出原始证据）。

## 8. 复现方式

```bash
# 构建（在仓库根，即游戏目录）
"/d/Scoop/apps/git/current/bin/bash.exe" Workspace/framework/build.sh

# 交互运行：正常打开游戏并注入，关闭游戏窗口即结束
Workspace/framework/bin/launcher32.exe

# 自动化回归：初始化后 20 秒自动 WM_CLOSE
Workspace/framework/bin/launcher32.exe --test-exit 20

# 负例：验证指纹门禁（应 exit 3，不启动目标）
Workspace/framework/bin/launcher32.exe --exe "C:\Windows\System32\notepad.exe"
```

退出码约定：0 = 框架初始化 ok/degraded 且游戏正常结束（含已知原生 0xC0000005）；2 = 未等到 init 标记/status=fail；3 = 指纹不匹配；4 = 远程注入失败；1 = 参数/文件/启动错误。

## 9. 已知限制（P3/P8 需处理）

1. 无干净退出回调（原生 0xC0000005）→ 不能依赖卸载路径；测试判定需与原版对照。
2. 每次启动 Fusion 先解包到新的 `%TEMP%/mrt*.tmp`（原生行为，不清理）；框架不写游戏目录，文档需提示可安全删除临时目录。
3. PAMU 扫描：16:28 前裸解引用曾导致 worker 线程 AV + 启动挂起（§3.2 已修）；当前用私有区 + ReadProcessMemory，仍有预算上限，极端内存布局可能漏检 → 门禁把“找不到”判为 degraded 而非 ok。
4. 早期模块快照（mmfs2 后立即）只有约 39 个模块；渲染/音频模块在其后 0.25–0.44 s 才出现。任何 Hook 安装逻辑必须等待目标模块。
5. loader 每次全量 SHA-256（~5 s）；后续可加“大小+mtime”缓存，但缓存只在指纹已验证过的机器上使用。
6. 尚未实现：热键/GUI/插件加载（P3/P4）；P3 将在本骨架的 worker 线程上加 D3D9 设备发现与 ImGui。

## 10. 与下一步（P3）的接口

- P3 的渲染 Hook 目标：`IDirect3DDevice9::Present`（vtable #17）/ `Reset`（#16），在 `mmf2d3d9.dll` 加载后、设备创建后安装（P2 已给出等待点）。
- P3 输入：窗口 `Mf2MainClassTh` 子类化 + ImGui `WantCapture`；按键默认 Insert。
- P3 必须复用本文件的“初始化标记 + 心跳 + 无退出回调”模型；GUI 初始化失败要能回到纯原版行为。
- 待 P3 实测：`D3DPRESENT_PARAMETERS`（窗口化/全屏/DPI 150%）、设备 Reset 时机、`Mf2EditClassTh` 子窗口与输入焦点关系。

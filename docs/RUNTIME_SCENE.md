# RUNTIME_SCENE.md — 运行时场景识别与开发期跳帧（Phase 5.0b）

状态：**已完成 2026-10-26 00:46**（场景识别上机验证；跳帧协议破译并实测抵达 Night 2 Breaker Room 与 Night 5 Final Encounter）
证据等级：全部为【实测】——真实 Steam 版 v1.121（build 286，SHA-256 c60e1c79…2c73）进程内观测 + 截图。

---

## 1. 结论摘要

1. 框架已能**实时读出当前 Fusion 帧**（帧号 + 帧名 + 尺寸/层数/实例数），作为 GameAdapter 的场景来源（`Workspace/framework/native/scene.cpp`，Phase 5 将改为更快的读法，见 §6）。
2. 已定位 **CRunApp / CRunFrame / RunHeader** 三个运行时结构在本版本的字段偏移（§3）。
3. 破译了 **frame handle → frame index** 映射：事件里的 "Jump to frame" 存的是 **handle**，运行时用 `CRunApp.m_frame_handle_to_index` 换算成 index 再加载；ctfak dump 的 `targetFrameName` 是按 index 反查的，**名字会错**（§4）。
4. 破译并实测了**跳帧协议**：`RunHeader.rhQuitParam = frame handle`，`RunHeader.rhQuit = 3`（§5）。
5. 已实测抵达：Warning(0)、title screen(1)、Elevator(2)、Breaker Room(9)、Extras(18)、Girl Voice(25)、8-bit Baby Game(26)、**Final Encounter(27)**。

### 实测截图（证据）
| 场景 | 帧号 | 截图 |
| --- | --- | --- |
| Breaker Room（Night 2 断电器房，含 DANGER LV HUD） | 9 | `Workspace/logs/probe/p5-warp-breaker-room.png` |
| Final Encounter（Night 5 Private Room，USAGE/POWER 98%/12 AM/三监视器） | 27 | `Workspace/logs/probe/p5-warp-final-encounter.png` |

---

## 2. 当前 scene 模块（Phase 4 产物，Phase 5 待替换）

文件：`Workspace/framework/native/scene.cpp` + `scene.h` + `frames_table.h`（由 `Workspace/tools/gen_frames_table.py` 从 `Workspace/analysis/data/frames.json` 生成，36 帧定义）。

两条查找路径：
1. **AnchorScan**：扫 `MEM_PRIVATE` 可读区域找 dword == `PAMU 地址`（框架启动时算出的 PAMU 副本地址），候选结构 `S = hit - 0x34`，校验 `*(S+0x34)==pamu`、`*(S+0x38)=CRunFrame*`（用帧几何+名字验证）、`*(S+0x3C)=RunHeader*` → 拿到当前帧。
2. **PairScan（兜底）**：按 (w,h) 预过滤全部区域里形如帧结构的块（读 +0x10 宽字符名字指针、+0x2C 层数、+0x44 实例数），再与 36 帧表逐字段比对。
3. 全量扫描限频 1000 ms，单次上限 8 s/64 MB；缓存命中时每 poll 只 2 次 ReadProcessMemory。
4. 轮询 250 ms（`slmodf.cpp` 心跳循环），日志 `scene| detected/changed idx=.. name=".." w=.. h=.. layers=.. inst=.. base=.. rh=..`。

实测延迟：正常帧 0.1–2 s；部分帧（Extras/8-bit game/Girl Voice/Elevator 等 rh=0）需要 anchor 失效 → PairScan 兜底，**最长观测到 14–20 s**。→ Phase 5 GameAdapter 改为 §6 的快速路径。

---

## 3. 运行时结构偏移（本版本实测）

### CRunApp（`app`）
扫描判定：RW 区域里 `dword==36 (m_frameMaxIndex)` + `+4 m_frameMaxHandle (36)` + `+8 指针` 指向 36 个 WORD 且是 0..35 的排列 → 命中地址即 `&m_frameMaxIndex`；**CRunApp base = 命中 - 0xC4**。

| 偏移 | 字段 | 实测值/说明 |
| --- | --- | --- |
| +0x80 | m_name | 指针，**UTF-16LE** "Sister Location" |
| +0xC4 | m_frameMaxIndex | 36 |
| +0xC8 | m_frameMaxHandle | 36 |
| +0xCC | m_frame_handle_to_index | 指针 → WORD[36] |
| +0xD0 | m_frameOffset | 指针 |
| +0x1D8 | m_Frame | 当前 CRunFrame 指针（加载中为 0） |
| +0x1DC | m_bResizeTimer | 0 |
| +0x1E0 | m_refTime | tick 计数 |
| +0x1E4 | m_appRunningState | 3=running，1=加载/切换中 |
| +0x1E8 | m_startFrame | 0 |
| +0x1EC | m_nextFrame | 下一帧 **index**（运行中=当前 index；切换瞬间=目标 index） |
| +0x1F0 | m_nCurrentFrame | 当前帧 **index**（切换中=-1） |

### CRunFrame（`frame`）
| 偏移 | 字段 | 实测 |
| --- | --- | --- |
| +0x00 / +0x04 | FrameHeader w/h | 如 1900×1000 |
| +0x10 | m_name | 指针，**UTF-16LE**（如 "Breaker Room"） |
| +0x2C | m_nLayers | 7 |
| +0x30 | m_pLayers | 指针 |
| +0x44 | m_loMaxIndex | 109 |
| +0x48 | m_loMaxHandle | 实例数 |
| +0x4C | m_lo_handle_to_index | 指针（实例 handle→index 表，Phase 5 读对象用） |
| +0x50 | m_los | 指针（实例数组） |
| +0x74 | m_levelQuit | 退出码（跳帧时被运行时置 3） |
| +0x78 | m_rhOK | 1=事件已初始化 |
| +0x7C | m_rhPtr | RunHeader 指针（**部分帧为 0**） |
| +0x80 | m_eventPrograms | 指针 |

**注意**：CRunFrame 对象地址会被复用（同一地址在 title/Extras/Breaker Room 反复出现），所以"帧地址"不能当身份，必须用帧名/几何或 index（`m_nCurrentFrame`）。

### RunHeader（`rh`）
| 偏移 | 字段 | 说明 |
| --- | --- | --- |
| +0x18 | rhApp | CRunApp* |
| +0x1C | rhFrame | 当前 CRunFrame* |
| +0x30 | rhQuit | short，跳帧协议写 3 |
| +0x32 | rhQuitBis | short |
| +0x38 | rhQuitParam | dword，跳帧时=**目标 frame handle** |
| +0x3C | rhNObjects | 实例数 |
| +0x54 | rhNumberOi | OI 数 |
| +0x58 | rhOiList | OI 列表指针（Phase 5 读对象名/类型用） |

SDK 参考：`Workspace/recon/src/MMF2Exts/MMF2SDK/Inc/cncr.h:196-305`（CRunFrame）、`Cncf.h:2731-2833`（RunHeader）。

---

## 4. frame handle → index 映射（本版本，32 项非恒等）

```
handle:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35
index :  2  4  3  7  5  6 12  8  9 10 11  1 13  0 15 14 17 16 18 22 19 23 20 21 24 25 28 26 27 29 30 31 32 33 34 35
```

- 关键项：handle 0→2 Elevator、1→4 load、8→9 Breaker Room、9→10 Funtime Auditorium、11→1 title、14→15 game over、15→14 death、27→26 8-bit Baby Game、**28→27 Final Encounter**、26→28 Frame。
- 决定性验证：Extras 帧点 "little Baby"（事件 target=27，dump 名字显示 "Final Encounter"）实际进入 **index 26 8-bit Baby Game** = `mapping[27]` ✓。
- 推论：**所有静态 dump 的 "Jump to frame N" 都要过这张表**。据此：Final Encounter 的唯一静态入口是帧 10 Funtime Auditorium G116（target 28）；Breaker Room 的静态入口是帧 4 load 路由 G6（target 8，go_to=6）。
- 注意：本表是当前 build（286）的；换版本必须重新从内存读表（`p5_warp.py dump`），**不要写死**。

---

## 5. 跳帧协议（实测，开发期 debug 用）

自然跳帧时窗口内观测（`Workspace/tools/p5_trace.py` 2 ms 轮询，日志 `Workspace/logs/probe/trace-continue.log`）：

```
  6132.5ms cur=title screen  nxt=1  run=3  lq=0  rh=.. quit=3  qp=0x00000019   <- 请求：handle 25 (Girl Voice)
  6146.9ms cur=title screen  nxt=1  run=3  lq=3  rh=00000000 quit=None        <- 帧被释放
  6149.3ms cur=0x00000000    nxt=25 current=-1 run=1   ...                     <- m_nextFrame=index 25
  6277.5ms cur=Girl Voice    nxt=25 run=1 ...                                  <- 新帧加载
 10611.8ms cur=Girl Voice    nxt=25 run=3 lq=0 quit=3 qp=0x00000000            <- 再次跳帧：handle 0 (Elevator)
 10628.9ms cur=0 nxt=2 current=-1 ...                                          <- handle 0 -> index 2
```

协议（写内存即可触发，等价于事件 "Jump to frame"）：
1. `*(int*)(rh + 0x38) = frame_handle`
2. `*(short*)(rh + 0x30) = 3`
3. 运行时随后：frame->m_levelQuit=3 → m_Frame=0 → `m_nextFrame = handle_to_index[handle]` → 加载目标帧。

工具：`python Workspace/tools/p5_jump.py --pid <pid> <handle> --log Workspace/logs/probe/jump.log`
- 只写 2 个字段，写后不改；**不要循环写 frame->m_levelQuit**（早期实验 A 对已释放帧循环写导致进程消失）。
- 已实测：Elevator → handle 8 → Breaker Room（9）；Breaker Room → handle 28 → Final Encounter（27）。日志 `jump handle=8 expected_index=9 from='Elevator' ...`。

**边界**：这是开发期取证工具，**不进插件 API、不写进产品功能**（计划 v1.6 P5.0b 约束）。产品功能只读场景/状态，不主动改帧。

---

## 6. 后续（Phase 5 要做的事）

1. **快速场景读取**：启动后扫描一次 CRunApp（≈100 ms，命中即缓存），之后每 tick 只读 `app+0x1D8 (m_Frame)` → 帧字段+名字；场景变化读取延迟从"秒级"降到"帧级"。保留现有 anchor/pair 兜底。**状态：已完成（2026-10-26 01:40）**。

   实现（`Workspace/framework/native/scene.cpp`）：`g_appAddr` 缓存 CRunApp；`ReadAppFrame()` 校验 `app+0xC4 (maxIndex==36)`/`+0xC8 (maxHandle)` 后读 `app+0x1D8`；`CacheAppFromRh()` 从 RunHeader+0x18 取 app；`ScanRunApp()` 在无 rh 时扫描全部已提交可读区找 `(36, maxHandle≥36, handle→index 表指针)` 签名（节流 1.5 s 重试，不再一次性放弃）。缓存丢失（连续 5 s 读不到）才回退 anchor/PairScan。

   实测证据（`p5_fastpath_check.ps1`，2026-10-26 01:40–01:41，pid 24152）：启动 1.5 s 内 `scene| fast path ready app=00F00848 via runheader rh=105BE938`；三次跳帧全部走 fast 分支且检测延迟 = 帧加载时间：Extras(18) 1684 ms、Breaker Room(9) 2064 ms、Final Encounter(27) 3218 ms；日志行形如 `scene| fast idx=9 name="Breaker Room" ...`（之前 rh=0 时走 PairScan，最坏 14–20 s）。

   **注意**：CRunFrame 的**对象地址会被复用**（title/Extras/Breaker/Final 实测都是同一 base，如 0x04D63098），因此"帧身份"只能用名字+尺寸识别，不能用指针；这也正是 `scene::Current().base` 在快速路径下保持不变的原因（正常现象）。
2. **对象/计数器读取**（DebugInspector、ThreatTelegraphs 依赖）：**状态：已完成并自然复核** —— `native/state.cpp` 已实现并实机验证（对象 104/73、计数器 30/19、标题页 option 1→2→3 与 FE hour/power/final stage 同步）；2026-10-26 自然游玩完整记录 Night 2 Breaker Room 与 Night 5 帧 27 死亡链（`docs/P5_NATURAL_VERIFICATION.md`）。细节与偏移见 `RUNTIME_STATE.md` §7。注意 `frame+0x4C/0x50`（m_los）实测**不是**可用的实例数组，改用「全内存搜 dword==rh + 对象头校验」。
3. **场景状态记录器**：按时间戳记录 `scene idx + 关键对象值`，用于 ThreatTelegraphs 的双轨证据（warp 取证 + 自然游玩复核）。**状态：已完成（2026-10-26）**。实现 `native/recorder.cpp` + `record=1`/`record_interval_ms`；自然复核输出 `logs/state/state-20261026-050107-28120.jsonl`（Night 5 帧 27 全 500 ms 采样），详见 `docs/P5_NATURAL_VERIFICATION.md`。

---

## 7. 复现步骤（本机）

```powershell
# 1) 启动游戏（框架会注入并写日志）
Start-Process 'Workspace\framework\bin\launcher32.exe' -ArgumentList @('--test-exit','1800')
# 2) 跳到指定 frame（handle 见 §4）
python Workspace/tools/p5_jump.py --pid <pid> 8      # Breaker Room
python Workspace/tools/p5_jump.py --pid <pid> 28     # Final Encounter
# 3) 截图（Agent 工具）或 p5_extras_probe.ps1 -Action shot
```

## 8. 相关文件

- 工具：`Workspace/tools/p5_warp.py`（扫描/dump，`dump` 会打印映射表）、`p5_jump.py`（跳帧）、`p5_trace.py`（字段追踪）、`p5_runapp.py`、`p5_extras_probe.ps1`（启动/点击/按键/截图）、`p5_start_game.ps1`、`p5_fastpath_check.ps1`（快速路径三步跳帧延迟验收）、`p5_state_check.ps1`（state/DebugInspector 验收）。
- 日志：`Workspace/logs/probe/trace-continue.log`、`jump.log`、`jump-fast.log`、`warp.log`、`runapp-dump.json`、`p5fast-run.log`/`p5fast-report.txt`、`p5state6-report.txt`。
- 截图：`Workspace/logs/probe/p5-warp-breaker-room.png`、`p5-warp-final-encounter.png`。
- 框架源码：`Workspace/framework/native/scene.cpp`（现版）、`Workspace/framework/native/frames_table.h`。
- 静态数据：`Workspace/recon/dumps/Sister Location/JSON/application.json`、`Workspace/analysis/frame-index.md`。

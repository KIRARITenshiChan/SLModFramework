# FNAF: Sister Location — 运行时对象/计数器读取（P5 GameAdapter 基础）

状态：2026-10-26 01:10 实测建立；2026-10-26 01:30 追加「框架内 state 模块」实测（build 286 / SL v1.121 / 32 位进程）
证据等级：**实测**（附进程地址与屏幕对照）。所有地址均随进程变化，禁止硬编码。

## 1. 结论摘要

| 项目 | 结论 |
| --- | --- |
| CRunApp 定位 | 扫描可写内存找 `dword==36` 且 `+4==36` 且 `+8` 指向 36 个 WORD 的 0..35 排列 → 命中点即 `&m_frameMaxIndex`，**CRunApp = 命中 - 0xC4** |
| 当前帧 | `*(CRunFrame**)(app+0x1D8)`（`m_Frame`），帧名 UTF-16LE 在 `frame+0x10` |
| RunHeader | `frame+0x7C`（`m_rhPtr`，仅部分帧非 0）；更稳的路径：`rh+0x18 == rhApp`、`rh+0x1C == rhFrame` |
| OI 表 | `rh+0x58` 指针，条目 stride **146 (0x92)**，名字 UTF-16LE 在 **entry+0x5A**；`rh+0x54` 是 int 型 `numberOi` |
| 对象枚举 | 全内存搜 `dword == rh`：每个命中地址 A 都是对象的 `hoAdRunHeader`（对象基址 = A-8）；用 `hoOi/hoType/hoSize/hoNumber` 与 OI 表校验。**hoOi 是 OI 句柄（oilOi=entry+0），不是表下标**：须先在 OI 表按 oilOi 找到 idx，再校验 `hoOiList == oiList + idx*146` |
| **计数器值** | **`obj+0x20A` 处 int16 s；显示值 `value = ~s`（即 `-s-1`）**。计数器对象 `hoSize` 恒为 578 (0x242) |

## 2. 计数器值编码（本轮关键发现）

```
value = ~(int16)*(uint16_t*)(object + 0x20A)   // 按位取反；等价于 -s-1
```

同一对象 `+0x208` 的 32 位值是 `(~value) << 16`（高 16 位为 one's complement，低 16 位恒 0）。
本帧内所有计数器共用同一偏移；非计数器对象（type 2 Active / 32 扩展）该位置语义不明，**只能用于 type 7（Counter）**。

### 验证证据（2026-10-26 01:05–01:07，pid 22508）

| 场景 | 对象 | 屏幕显示 | 内存读数（公式还原） |
| --- | --- | --- | --- |
| Final Encounter | power left | `POWER 92%`（截图 `Workspace/logs/probe/p5-fe-power.png`） | 92 ✅ |
| Final Encounter | hour | `12 AM` | 12 ✅ |
| Final Encounter | count seconds | 每秒 +1（01:05:37→01:05:44：22→27） | 与时间同步 ✅ |
| Final Encounter（刚 warp 进入） | power left / hour / final stage / got you / side / viewing cam | 初始状态 | 100-1=99? 实测 99（warp 后已开始递减）；hour=12；final stage=0；got you=0；side=2；viewing cam=1 ✅ |
| Breaker Room | Freddy / danger level / danger level 4 / got you / beat / interface | 事件程序状态（DANGER LV 条） | Freddy 在 01:06:51 由 0→1，01:06:55 回到 0（危险累积确实变化），其余按静态事件初值 ✅ |
| title screen | option | 菜单高亮项（NEW GAME=0/CONTINUE=1/EXTRAS=2/CUSTOM=3） | 初始 1（CONTINUE）；按 ↓ 依次 2→3→0（回绕），与静态事件 G16/G17 一致 ✅ |

## 3. 对象头偏移（实测，32 位）

| 偏移 | 字段 |
| --- | --- |
| +0x00 | hoNumber (short) |
| +0x04 | hoSize (int)；计数器=578，Active 常见 752 |
| +0x08 | hoAdRunHeader (ptr) ← 全内存搜 rh 时命中点即此 |
| +0x12 | hoOi (short) |
| +0x18 | hoType (short)：2=Active、7=Counter、3=String、6=Lives、32=扩展、33=Ini 扩展 |
| +0x1C | hoOiList (ptr) == oiList + oiIdx*146（可用于校验） |
| +0x44 | hoCommon (ptr → OC 结构，ocIdentifier 含对象标识 "CNTR" 等) |
| +0x4C / +0x54 | hoX / hoY (int) |
| +0x60 / +0x64 | hoImgWidth / hoImgHeight |
| +0x7C | hoFlags (short) |
| +0x84 | hoLayer (int) |

## 4. 读取流程（推荐实现，供 C++ GameAdapter）

1. 由 scene 模块拿到当前 `CRunFrame*` 与 `RunHeader*`（scene anchor: struct+0x38=frame、+0x3C=rh）。
2. 从 `rh+0x18` 取 `CRunApp*`；之后每帧只需 `*(app+0x1D8)` + 帧名校验即可快速得到当前帧（无需重复全表扫描）。
3. 帧变化时（或首次）：全内存搜 `dword==rh` 建对象表（有效对象 = hoOi 能在 OI 表按 oilOi 查到 + hoType 匹配 + 0x40<hoSize<0x8000 + hoNumber>0 + `hoOiList == oiList + idx*146`）。**注意：只比较帧基址不够，CRunFrame 内存会被复用；应同时比较 `frame+0x7C` 的 rh 是否变化，变化即重建**。
4. 计数器读数：读 `obj+0x20A` 的 int16，`value = ~s`。读取前建议校验对象地址仍在可读区、`hoOi` 未变。
5. 帧切换后立即丢弃旧对象表（帧对象地址会被复用，不可当身份）。

性能注记（Python 实测）：`p5_inst.load()` 一次约 2 s（含全内存扫描）；C++ 下同样的 rh 扫描预计 50–200 ms，可在后台线程按场景变化触发。

## 5. 局限与未解

- 只验证了整数计数器；小数计数器（若有）的编码未验证。
- 隐藏计数器（Freddy/got you 等）同样会更新（Breaker Room 实测），但极端情况下游戏若完全不执行该对象逻辑，字段可能保持初值 —— 读取结果一律标注为「读到的运行时值」，不作为推断依据本身。
- 对象枚举依赖 `rh` 指针；部分帧 `frame+0x7C` 为 0（如 Extras），此时无法建对象表，应报告 unknown。
- 帧 9 / 帧 27 的语义（危险阈值、死亡触发）仍以静态事件证据为准，运行时读数只用于判定「当前处于哪一阶段」。
- 对象枚举会有少量遗漏（Breaker Room 实例 109 → 校验通过 104；Final Encounter 77 → 73），差集不猜测。
- 全表重建成本：实测 C++ 全内存扫描 350–780 MB / 670–855 regions / 230–360 ms，仅在帧切换或 rh 变化时发生（worker 线程，不影响 renders）。
- 同名计数器（Breaker Room 的 20 个 `compute`）各自独立，按名字查找只返回第一个。

## 6. 相关文件

- 工具：`Workspace/tools/p5_inst.py`（对象表）、`Workspace/tools/p5_objects.py`（结构读取原语）、`Workspace/tools/p5_jump.py`（跳帧）、`Workspace/tools/p5_runapp.py`（CRunApp 扫描）。
- 结构文档：`Workspace/docs/RUNTIME_SCENE.md`（CRunApp/CRunFrame/RunHeader 偏移、handle→index 映射、跳帧协议）。
- 截图：`Workspace/logs/probe/p5-fe-power.png`、`Workspace/logs/probe/p5-warp-breaker-room.png`、`Workspace/logs/probe/p5-warp-final-encounter.png`。

## 7. 框架内 state 模块（Phase 5 实现，2026-10-26 01:12–01:28）

### 7.1 代码与集成

- 新增 `Workspace/framework/native/state.h` / `state.cpp`：namespace `state`，接口 `Init/Poll/Status/StatusName/SceneIndex/ObjectCount/CounterCount/CounterName/CounterValue/CounterValid/FindCounter/FormatSummary`。
- 线程模型：worker 心跳每 250 ms 调 `scene::Poll(); state::Poll();`；BuildMap 只在帧切换（去抖一 tick）或 rh 变化时执行；结果发布到双缓冲，GUI 无锁读。
- 常量：`STATE_STRIDE=146`、`STATE_OI_NAME_OFF=0x5A`、`STATE_VALUE_OFF=0x20A`、`STATE_SCAN_LIMIT=0x40000000`(1 GiB)、`STATE_MAX_OI=512`、`STATE_MAX_COUNTERS=64`。
- `overlay.cpp`：新增「场景状态 (DebugInspector)」折叠区（状态/frame_index/对象数/计数器数/RunHeader + 逐计数器 `名字 = 值`，无效显示 `?`，底部标注只读公式）。
- `slmodf.cpp`：gate 后 `state::Init()`；心跳每 5 s 输出 `state| scene=... objs=... ctr=... rh=...` + 场景专用 watch（帧 1：option/started/night 2/beat1/keycard/beat 3；帧 9：Freddy/danger level 4/got you/beat/interface/restart 1；帧 27/31：hour/power left/final stage/got you/side/near/viewing cam/count seconds）。
- `build.sh` / `tests/build_harness.sh` native 列表加入 `state`；离线回归 `--wndproc-only` / `--imgui-only` / `--plugins` 全部 PASS。

### 7.2 实机验证（2026-10-26 01:23–01:28）

| 证据 | 内容 |
| --- | --- |
| `Workspace/logs/framework/slmodf-20261026-012353-6756.log` | `state| map frame=00F31E38 rh=060C7A38 ... oi=80 objs=105 counters=30`（Breaker Room）；`state| map frame=00F31E38 rh=2BBA6C70 ... oi=78 objs=73 counters=19`（Final Encounter）；`state| frame memory reused, rebuilding map (rh ... -> ...)` |
| `Workspace/logs/probe/p5state4-report.txt` | `state| scene=9 ... Freddy=0 danger level 4=1 got you=0 beat=0 interface=0`；`state| scene=27 ... hour=12 power left=99 final stage=0 got you=0 side=2 near=0 viewing cam=1 count seconds=1` |
| `Workspace/logs/probe/p5state4-gui-fe.png` | GUI DebugInspector：场景[27] Final Encounter、对象 73/计数器 19；hour=12、power left=96、final stage=1、side=2、count seconds=14 |
| `Workspace/logs/probe/p5state4-gui-breaker2.png` | GUI：场景[9] Breaker Room、对象 104/计数器 30；Freddy=0、danger level 4=1、got you=0、beat=0、interface=0 |
| `Workspace/logs/probe/p5state5-option2.png` / `p5state5-option3.png` | 标题页 GUI：游戏选中 EXTRAS 时 option=2；选中 CUSTOM NIGHT 时 option=3（与菜单高亮同步） |
| `Workspace/logs/framework/slmodf-20261026-012740-8708.log` | 标题页会话 `state| scene=1 ... option=1` → 按 ↓ 两次后 `option=3` |

### 7.3 复现

```powershell
"/d/Scoop/apps/git/current/bin/bash.exe" Workspace/framework/build.sh
pwsh -NoProfile -ExecutionPolicy Bypass -File Workspace\tools\p5_state_check.ps1 -Tag p5state5
# 手动验证数值语义（标题页）：↓ 键切选项，GUI 中 option 应同步 1→2→3→0
pwsh -File Workspace\tools\p5_extras_probe.ps1 -Action keys -Keys down -WaitMs 1200
```

- 相关源码：`Workspace/framework/native/state.h`、`state.cpp`、`overlay.cpp`、`slmodf.cpp`、`build.sh`。

## 8. 状态记录器（recorder，2026-10-26 01:45–01:58）

### 8.1 实现

- 源码 `Workspace/framework/native/recorder.h` / `recorder.cpp`；worker 线程每 `SCENE_POLL_MS`(250 ms) 在 `state::Poll()` 之后调用 `recorder::Poll()`。
- 开关注解：`Workspace/config/framework.ini` 的 `record=0/1`（默认 0）与 `record_interval_ms=100..10000`（默认 500，越界 warn 并保持旧值）；GUI「场景状态 (DebugInspector)」底部的 `状态录制 (JSONL)` 复选框可运行时开关，启用时显示输出路径。
- 输出：`Workspace/logs/state/state-YYYYMMDD-HHMMSS-<pid>.jsonl`，一行一个快照：
  `{"t":<GetTickCount>,"time":"YYYY-MM-DDTHH:MM:SS.mmm","scene":N,"scene_name":"…","w":…,"h":…,"layers":…,"instances":…,"rh":"0x%08lX","scene_changed":0/1,"counters":{名字:值,…}}`
- 写盘策略：场景切换必写（`scene_changed=1`）；同场景按 `record_interval_ms` 周期写；场景变化行与每 5 行 flush。
- **场景切换去抖（01:53–01:58 修的三处）**：
  1. state 的 OI/对象映射重建需要 200–400 ms，切换瞬间写出的 `scene_changed` 行原本 counters 为空 → recorder 现在会等 `state::Status()==1 && state::MapScene()==scene && state::ValuesReady()`，最长等 3 s 后强制写（`state::MapScene()`/`state::ValuesReady()` 是本次为 recorder 新增的只读访问器）。
  2. 帧内存复用（同 base 不同 rh）时 state 会写 `frame memory reused, rebuilding map` 并重建，记录器同样等到 map/values 就绪。
  3. 游戏里同名对象很多（Breaker Room 有 16 个 `compute`），JSON 重复键会被覆盖 → recorder 对第 2、3… 次出现写 `compute#2`、`compute#3`（拼接后缀后仍 ≤ 名字缓冲 96 B）。
- 停机：关闭勾选会 `fflush+fclose` 并打 `recorder| closed file=… lines=… flushes=…`；游戏被 WM_CLOSE 杀死时文件已按行 flush，最多丢最后 500 ms。

### 8.2 实机证据（pid 2904，`Workspace/logs/state/state-20261026-015801-2904.jsonl`，95 行）

| scene | scene_name | counters | 备注 |
| --- | --- | --- | --- |
| 0 | Warning | 3 | `go/played intro yet/count` |
| 1 | title screen | 10 | `option/started/night 2/playing intro/vis/go to extras/beat1/keycard/beat 3/end words` |
| 9 | Breaker Room | 30 | `Freddy/danger level 4/got you/beat/interface/…` + 16 个 `compute`（`compute#2`…`compute#16`） |
| 27 | Final Encounter | 19 | `hour=12/power left=99/side=2/final stage=0/got you=0/count seconds=0/viewing cam=1/…` |

- 周期行示例（同一文件尾部）：`"scene":27,…,"scene_changed":0,"counters":{…"power left":94,"final stage":2,"count seconds":23,…}` —— 数值随游戏刷新。
- 框架日志：`Workspace/logs/framework/slmodf-20261026-015801-2904.log` 中 `recorder| file=…`、`recorder| scene 9 "Breaker Room" counters=30 line=21`、`recorder| scene 27 "Final Encounter" counters=19 line=35`。
- GUI 截图：`Workspace/logs/probe/p5state11-gui-recorder.png`（DebugInspector 的 19 个计数器 + 选中的「状态录制 (JSONL)」+ 输出路径）。

### 8.3 用途与限制

- 用途：ThreatTelegraphs（Phase 7）需要用「攻击前置状态的实际时间线」证明预警提前量，JSONL 就是这条证据链（双轨证据规则的机器可读部分）。
- 限制：计数器名字来自 OI 表，可能重复（已加后缀）；只能记录 state 当前发布的 64 个计数器（`STATE_MAX_COUNTERS`）；warp 进入的帧同样会被记录，但 warp 属开发期手段，结论仍需自然游玩复核。
- 自然复核（2026-10-26）：Night 2 Breaker Room 与 Night 5 Final Encounter 各一次真人自然死亡已完整落盘，时间线/计数器语义修正见 `docs/P5_NATURAL_VERIFICATION.md`；已确认 `near` 计数器在帧 27 未被事件引用、`left/right/top is closed` 在 AFK 下仍会 toggle（因此不能只凭名字当玩家操作状态）。

## 9. 插件 API 的只读状态暴露（GameAdapter v0，2026-10-26 01:48）

- `plugin_host.cpp::PushGameContext()` 已把 scene/state 结果放进 `ctx.game`（插件仍拿不到任何原始地址）：
  `supported`（gate==1 且 scene 有效）、`scene_id`、`scene_name`、`scene_width`、`scene_height`、`counters_ready`、`counters`（名字→整数快照，最多 64 项）、`frame_id`、`fps`、`presents`、`framework_version`。
- `counter` 名字直接作 Lua 表键；同名计数器（如 `compute`）在插件表里只保留最后一个——需要区分的插件应结合场景语义使用，或在后续 API v1 引入按 OI 句柄查询。
- 实测 [pid 28348，`Workspace/logs/framework/slmodf-20261026-015004-28348.log`]：
  - `HelloPlugin update #601 … scene=9:Breaker Room counters=[Freddy=0 got you=0 danger level 4=1]`
  - `HelloPlugin update #901 … scene=27:Final Encounter counters=[got you=0 final stage=0 hour=12 power left=99 side=2]`
  - 同一会话后段 `scene=27 … counters=[got you=0 final stage=2 hour=12 power left=86 side=1]` → 插件侧数值随 state 刷新。
- 证据截图：`Workspace/logs/probe/p5state8-gui-final.png`（DebugInspector 在 Final Encounter 的计数器面板）。

# ThreatTelegraphs（Phase 7）— 攻击预警规则（v0.5.1）

状态：**v0.5.1（帧 31 Freddy 精确门/阶段 + Bonnet 倒计时 + 状态字幕 + 动态对象读取）**，2026-10-27 03:1x；历史：v0.4.2 Ballora 时序模型修正 + 门关后 hold 警示（§14/§15）；v0.4.3/v0.4.4 见 §16；v0.5.x 见 §17。门关不等于安全见 §15（事故复盘 Workspace/research/p7-global/incident-20261027-ballora-reopen.md）。
- 覆盖 8 个可读场景：帧 8 / 9 / 10 / 11 / 13 / 19 / 27 / 31；帧 16、26 不做提前预警（理由见 §3）。
- 帧 27（Night 5 Final Encounter）与帧 9（Night 2 Breaker Room）已完成**实机**分级验证；其余场景为静态分析 + 离线断言，**待自然游玩复核**。
- 插件目录：`Workspace/plugins/ThreatTelegraphs/`（`plugin.ini` + `main.lua`）。
- 默认：**关闭**（`CONFIG.enabled=false`）；规则开关不持久化，重启回默认。
- 静态依据：`Workspace/research/p7-global/threat-matrix.md`（503 行，10 场景致命链 + 阈值 + 提前量）。

## 1. 目标与边界

- 目标：把游戏**已经存在于内存中的威胁状态**以屏幕提示提前显示，给玩家留出关门/停止/闪光等反应时间。
- 只读：插件只读 `ctx.game.counters`（全部 type7 计数器）与 `ctx.game.alterables`（原生白名单仅 `Ennard`、`flip it`）；**不写内存、不改 AI、不改计时/胜负、不注入输入**。
- 不改原版体验：默认关闭；关闭时除加载日志外无任何屏幕绘制。
- 状态驱动，不用固定秒数：不同局/难度/小时的路径速度不同，阈值基于游戏自己的计数器。
- 分级语义：`low` = 威胁开始积累，仍有 3–30 秒反应；`high` = 已进入危险窗口，必须立刻动作（0.5–5 秒）；`attack` = 判定已发生（0–1 秒），只作反馈，不承诺提前量。
- 按方向各保留最高优先级一条（attack > high > low），多方向可同屏（如左门 high + 中心低氧）；attack 仍为全屏单条。

## 2. 覆盖矩阵

| 帧（index） | 场景 | 主要信号 | 提前量 | 状态 |
|---|---|---|---|---|
| 8 | Ballora Gallery | `distance` / `progress` / `jumpscare` / `backwards` | 1.7–22 s | 已实现，未实机标定 |
| 9 | Breaker Room | `Freddy` / `beat` / `interface` / `got you` | ~13 s（4→6） | **已实机验证** |
| 10 | Funtime Auditorium | `distance` / `recharge` / `got you` / `planned death` | 2.5–15 s | 已实现，未实机标定 |
| 11 | Parts and Service | `timer` / `ready to spawn` / `win` | 10–45 s | 已实现，未实机标定 |
| 13 | Under Desk | `Counter`（语义待标定）/ `steps` | 2.7–7 s（估） | **默认禁用** |
| 16 | Night 4 | 唯一连续量 `windpad[0]` 不在白名单 | — | **不做**（仅锁存） |
| 19 | Parts and Service 2 | 密码 `death timer` / `chain`；回合 `timer` | 1–3 s / 10–45 s | 已实现，未实机标定 |
| 26 | 8-bit Baby Game | 碰撞瞬时 / 仅 `died` | 0 | **不做**（仅锁存） |
| 27 | Final Encounter | `Ennard[3]` + 门 + `side` + `got you` | 1–12 s | **已实机验证** |
| 31 | Custom Level / Custom Night | Foxy progression / Bidy prog / Freddy count / 氧气 / Ennard | 3–40 s | 已实现，未实机标定 |

其他场景（标题、Baby's Room、game over 等）无专门规则；通用锁存仍运行。

## 3. 不做的场景及理由

- **帧 16（Night 4）**：唯一连续危险量是 `windpad.alterable[0]`（0..50 张力），但 `windpad` 不在原生 alterable 白名单（`state.cpp` 的 `kAlterableWatch` 只轮询 `Ennard` / `flip it`）；counter 只有阶段量（stage / start winding / climbers / wiggle），无法表达“还有多远”。扩展白名单需改 native 并重新编译，当前不做。
- **帧 26（8-bit Baby Game）**：死亡是碰撞或 `timer==0` 的同帧判定；唯一可读的 `died` 是死亡之后的状态。做 attack 也只能显示“已死”，没有产品价值。

## 4. 分场景规则（v0.3.0）

### 4.1 帧 8 — Ballora Gallery

状态量：`distance` 0..600（噪音/移动量）、`progress`（阶段）、`jumpscare`、`backwards`（回程）。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `jumpscare >= 1` | center | “来了 / Ballora 已经抓住你” |
| 静默 | `backwards >= 1` | — | 回程时游戏把 distance 清零 |
| high | `distance >= high`（默认 520；`progress>=300` 后 420） | center | “马上停下！”/“再动就来不及了” |
| low | `distance >= low`（默认 380；阶段后 300） | center | “Ballora 注意到你了”/“Baby 在催你，慢一点” |

理由：`distance>600` 即死（G69）；W 前进会 +10/s 或阶段后 +60/s，所以阶段后必须降阈值；`progress>=400` 创建 dancer，dancer 在场时 distance 有额外波动。

### 4.2 帧 9 — Breaker Room

状态量：`Freddy` 0..6、`got you`、`beat`、`interface`。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `got you >= 1` 或 `Freddy >= 6` | top | “Freddy 已抓住你 / 本局结束” |
| 静默 | `beat >= 1` 或 `interface >= 800` | — | 房间已通关 |
| high | `Freddy >= 4` | top | “Freddy 即将出现 4/6” + “准备应对” |
| low | `Freddy >= 2` | top | “Freddy 接近 2/6” |

阈值 2/4/6 的依据：自然局 pid 24092 的 Freddy 序列为 **1,2,4,6**（跳过奇数），3/5 阈值会漏档。实机证据：pid 19272（high/low）、pid 13216（attack d150/d350，窗口 0.75–1.0 s）、pid 3584（默认关闭）。**`beat` 只是显示静默，不是护盾**：G58 没有 beat 条件，开发期写 beat=1 再写 Freddy=6 仍会判死（pid 1408 实测）。

### 4.3 帧 10 — Funtime Auditorium

状态量：`distance` 0..600、`recharge` 0..50、`got you`、`planned death`。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `got you >= 1` 或 `planned death >= 1` 或 `distance >= 600` | center | “Foxy 已经扑过来了 / Foxy 到面前了” |
| high（闪光禁令） | `distance >= high+60` 且 `recharge >= 50` | center | “别闪光！/ 这一下会把它直接拉过来” |
| high | `distance >= 420` | center | “别动，快到了” |
| low | `distance >= 280` | center | “Foxy 在靠近，别出声” |

理由：`distance>600` 与在 500 以上按 Space 都会死；按 Space 一次 `distance += 50`，所以“有闪光且距离已高”比普通 high 更早提示。

### 4.4 帧 11 — Parts and Service

状态量：`timer`（120→0）、`ready to spawn`、`win`、`got you`。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `got you >= 1` | center | “Bonnie 已经来了” |
| 静默 | `win >= 1` | — | 已胜 |
| high（超时） | `timer <= 0` 且 `Script Event >= 164` | center | “已超时！/ 快把 Bonnie 引出来” |
| high | `timer <= 15` 且 `ready to spawn == 1` | center | “快把 Bonnie 引出来” |
| low | `timer <= 45` | center | “时间不多了” |

理由：`Script Event>=164` 后每秒 timer-1；`ready==1 && timer<=0` 时置 got you=1；timer 归零而 puppet 在场时不会立刻死，但 puppet 归位后下一帧即死，故超时用 high。

### 4.5 帧 13 — Under Desk（默认禁用）

状态量：`Counter`（疑似 X 坐标）、`steps`、`pulling`、`got you`。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `got you >= 1` 或 `Counter >= 800` | center | “来了 / 松开金属板” |
| 静默 | `steps >= 7` | — | 剧情已过 |
| high | `Counter >= 680` | center | “快停下！/ 别再拉金属板” |
| low | `Counter >= 500` | center | “板子拉太多了 / 松手” |

**默认关闭**：`Counter` 初值与拉动增量尚未实机标定（可能初值就很高），需 recorder JSONL 采样后再启用。

### 4.6 帧 19 — Parts and Service 2

状态量：密码盘 `chain`（0..10）、`death timer`（0..3）、`prompt` / `answer`；回合 `timer`、`ready to spawn`、`win`。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `got you >= 1` 或 `death timer >= 3` | center | “密码超时，它来了 / 本局结束” |
| 静默 | `win >= 1` 或 `chain >= 10` | — | 已胜 / 密码完成 |
| high（密码） | `chain < 10` 且 `death timer >= 2` | center | “再错就死！/ 快按对密码” |
| low（密码） | `chain < 10` 且 `death timer >= 1` | center | “密码要快” |
| high（回合超时） | `timer <= 0` 且 `Script Event >= 164` | center | “已超时！/ 快把它引出来” |
| high（回合） | `timer <= 15` 且 `ready == 1` | center | “快把它引出来” |
| low（回合） | `timer <= 45` | center | “时间不多了” |

注意：插件**读不到“该按哪个数字”**（prompt 是内部表达式），只能提示时间压力。密码盘每 1 秒 `death timer+1`，正确点击清零；到 3 触发 jumpscare。

### 4.7 帧 27 — Final Encounter

状态量：`Ennard.alterable[3]`（0 空闲；1–3 左路；4 到左门；5 通风口前半；6 到通风口；7–9 右路；10 到右门；99 攻击判定）、`left/right/top is closed`、`side`、`hour`、`got you`。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `got you >= 1` | center | “来了 / 监视器或门已经来不及” |
| attack | `Ennard[3] == 99` | center | “它到门口了 / 立刻关门” |
| 静默 | `hour == 6` | — | 天亮通关时刻（Private Room 的 hour 从 12→1→…→6，旧写 `>=6` 会在 12 AM 误静默，已修并离线验证） |
| high | `st == 4` 且 `left is closed == 0` | left | “关左门！” |
| high | `st == 6` 且 `top is closed == 0` | vent | “关通风口！” |
| high | `st == 10` 且 `right is closed == 0` | right | “关右门！” |
| low | `st` 1–3 / 5 / 7–9 | left/vent/right | “Ennard 在左侧/通风口/右侧” |
| low（side） | `st == 0` 且 `side` 1/2/3 | left/center/right | “Ennard 在左边/正面/右边” |

**`side` 修正**：`side` 不是旧版认为的“重叠方向”，而是 Ennard 与 left/middle/right side 碰撞的结果（G215–G217）→ 1=左、2=中、3=右；声像 pan 也对应 -50/0/+50（G234–G236）。所以它可以作为 `st==0` 时的方向低警。

**门已关不提示**：状态 4/6/10 且对应门已关时，游戏下一 tick 会把状态重置为 0，插件静默，避免假警。

**证据**：pid 24336（首版 low/high 日志与截图 `p7-gui-scroll-24336.png`、`p7-threat-high-vent-24336.png`；attack 截图抓晚了无效）、pid 21076/21088（状态机计时 0→5→6→99）。99 到切 death 约 **0.77 秒**。

### 4.8 帧 31 — Custom Level / Custom Night

状态量：46 个 counter，插件只使用语义明确的 A 组；`hour==6`（通关时刻）或 `got you>=1` 优先。

| 级别 | 条件 | 方向 | 文案 |
|---|---|---|---|
| attack | `got you >= 1` | center | “来了 / 本局已判定” |
| attack | `Ennard[3] == 99` | center | “它到门口了” |
| 静默 | `hour == 6` | — | 天亮 |
| high | Ennard 4/6/10 且对应门开 | left/vent/right | “关左/通风口/右门！” |
| high（Foxy） | `Funtime Foxy progression >= 5` 且右门开 | right | “关右门！/ Foxy 已经到右门” |
| high（Foxy） | progression >= 4 | right | “Foxy 要到右门了” |
| low（Foxy） | progression >= 3 | right | “Foxy 在靠近右门” |
| high（Bidybab） | `Bidy prog >= 4` 且通风口开 | vent | “关通风口！/ Bidybab 快进来了” |
| low（Bidybab） | `Bidy prog >= 3` | vent | “通风口有动静” |
| high（Freddy） | `Freddy count <= 5` 且任一门开 | left/right | “关左/右门！/ Freddy 到门口了” |
| low（Freddy） | `Freddy count <= 12` | center | “Freddy 在靠近” |
| high（氧气） | `Oxygen left <= 30` 且 `Minreena progress >= 1` | center | “氧气不多了 / 尽快结束” |
| low（Minireena） | `Minreena progress >= 1` | center | “屏住呼吸，别动” |

说明：
- 每帧只显示最高优先级一条，其余可在 debug 行查看。
- 各子机制用 `* Diff > 0` 判断本局是否启用（Custom Night 可调难度）。
- **氧气归零的死亡映射尚未找到**：氧气规则只作“低氧提示”，不承诺死亡时间。
- **Bonnet**（近身戳）没有任何前置 counter，同帧置 `got you=1`，无法预警，只能靠通用锁存。
- 左/右门与 Freddy 的精确对应依赖不可读的 Freddy alterable，当前用“任一门开”近似，可能偶尔提示错门。

## 5. 通用锁存（所有场景）

若当前场景没有专门规则，或专门规则未命中，插件检查 `got you` / `jumpscare` / `died` / `planned death` 任一 >= 1，显示 attack：“攻击已判定 / 本次已经来不及，下一条命注意”。

- 唯一 100% 可靠、但**零提前量**的规则。
- 专门规则命中 attack 时不再追加锁存，避免双提示。

## 6. 时机与置信度

- 帧 27：状态 1–3/5/7–9 可持续数秒到数十秒；到门（4/6/10 且门开）到 99 约 **10–12 秒**（随 hour/timer 变化，不可当固定值）；99 → 切 death **0.77 秒**。
- 帧 9：Freddy 4→6 约 10–13 秒；6 → death 0.75–4.8 秒（写值试验 0.75，自然局含动画 4.8）。
- 帧 8/10：`distance` 是连续量，但受随机重掷影响，提前量 1.7–22 秒不等。
- 帧 11/19：回合 timer 有 10–45 秒确定提前量；密码盘只有 1–2 秒。
- 帧 31：Foxy 30–40 秒/级（最好）；Bidy 3 秒/级；Freddy count 递减速率未标定；氧气按进度加速。
- 原则：**状态驱动**；规则不预测“还有几秒”，只报“现在处于哪一阶段”。

## 7. 屏幕布局

坐标以物理客户区像素为单位，随窗口高度缩放：`scale = CONFIG.scale * clamp(h/1440, 0.6, 2.0)`；`CONFIG.scale` 由 GUI 的“大小 (%)”控制（50–200%）。

| 告警 | 位置 | 视觉 |
|---|---|---|
| left | x=0.105w、y=0.55h | 黄/红牌 + 左边缘条（high） |
| right | x=0.895w、y=0.55h | 同上 + 右边缘条 |
| vent | x=0.50w、y=0.145h | 同上 + 顶部条 |
| top | x=0.50w、y=0.13h | 全宽顶部条（high） |
| center | x=0.50w、y=0.62h（普通）/ 0.40h（attack） | 中央牌；high 加全屏红框；attack 闪烁红框 |
| attack | 全屏红框 + 中心双行 | 判定/已死反馈 |

绘制使用 `on_draw_overlay(ui)` 与前景 DrawList 原语，**不打开 GUI 也可见**；GUI 打开时也照常绘制（预警在 GUI 之上）。

## 8. 安装、开关与 GUI

1. 目录：`Workspace/plugins/ThreatTelegraphs/plugin.ini`（`enabled=1` 表示允许加载，与规则开关 `CONFIG.enabled` 是两件事）。
2. 启动 launcher 后按 **Insert** 打开框架 GUI → “框架/插件”区找到 ThreatTelegraphs。
3. 控件：
   - “启用攻击预警” = 总开关（默认关）；
   - “调试信息” = 左上显示当前场景字段 + 命中规则 / reason；
   - “靠近阶段低警提示” = 是否显示 low；
   - “大小 (%)” = 50–200%；
   - “预览预警 (3 秒)” = 假警，检查显示效果；
   - “显示全部场景阈值” = 展开 8 个场景的阈值滑条（默认只显示当前场景）；
   - 每场景有启用复选框与关键阈值滑条。
4. 规则开关**不持久化**（当前版本不写配置文件），重启回到默认关闭；需要长期开启请改 `main.lua` 的 `CONFIG.enabled`。
5. 默认关闭验证：pid 3584 日志 `ThreatTelegraphs loaded enabled=false debug=false approach=true ... version=0.2.0`，warp 帧 9 写 Freddy=4 后无任何 overlay/告警；截图 `logs/probe/p7-default-off-3584.png`。v0.3.0 的默认值同样为 false，加载日志含 `scene13=false version=0.3.0`。

## 9. 验证状态与证据

### 9.1 已实机验证

- 帧 9 高/低/攻击：pid 19272 / 13216；截图 `p7n2-high-freddy4-19272.png`、`p7n2-low-freddy2-19272.png`、`p7n2-attack-13216_d150.png` / `_d350.png`；日志 `slmodf-20261026-062805-19272.log`、`slmodf-20261026-063654-13216.log`。
- 帧 9 beat 静默边界：pid 1408，日志 `slmodf-20261026-063753-1408.log`；写 beat=1 后插件静默但游戏仍判死。
- 帧 27 低/高：pid 24336，日志 `slmodf-20261026-060601-24336.log`；截图 `p7-gui-scroll-24336.png`、`p7-threat-high-vent-24336.png`。
- 帧 27 状态机：pid 21076/21088，日志 `slmodf-20261026-055838-21076.log`。
- 默认关闭：pid 3584（v0.2.0），v0.3.0 加载日志已确认 false。

### 9.2 仅静态/离线（待自然复核）

- 帧 8/10/11/13/19/31 的规则来自 `threat-matrix.md` 的 dump 分析；离线 `main.lua` 通过 `lua loadfile` + `luac -p` 与 50 条断言（各场景边界/静默/优先级），但**尚未在真实游玩中逐场景采样**。
- 帧 13 默认禁用，等标定。
- 帧 16/26 不做提前预警，只保留锁存。

## 10. 已知限制

- **未实机标定的场景可能误报/漏报**：帧 8/10/11/13/19/31 的阈值是静态推算；第一次自然游玩时建议开 debug，记录实际值再调。
- **attack 太晚**：帧 27 的 99、帧 9 的 Freddy=6、帧 19 的 death timer=3 都已判定，不可逆；真正有价值的是 high。
- **依赖 state 就绪**：`objects_ready=false` / `counters_ready=false`（换帧、对象表未就绪、内存复用重建）时不绘制，宁可漏报不误报。
- **计数器快照**：state 每 250ms 轮询一次，可能跳过瞬时值（帧 9 的 1,2,4,6 就是跳变）；阈值按实测序列取。
- **帧 31 多机制同开**：v0.4.0 起按方向各保留一条（可同屏多条），但同一方向仍只显示最高级；门与 Freddy 的精确对应不可读。
- **氧气**归零后果未找到，只做提示。
- **Bonnet**（帧 31）无法预警，只能事后锁存。
- **帧 16/26** 无提前预警。
- **没有声音提示**：仅视觉。
- **性能**：每帧只读字符串键 + 少量 DrawList 图元；实测 60 FPS 无可见影响。

## 11. 复现步骤

```powershell
# 1) 启动（会话必须已解锁；warp 仅开发期）
Start-Process -FilePath "E:\SteamLibrary\steamapps\common\Five Nights at Freddy's Sister Location\Workspace\framework\bin\launcher32.exe" -ArgumentList @('--test-exit','300') -WorkingDirectory "E:\SteamLibrary\steamapps\common\Five Nights at Freddy's Sister Location"

# 2) 等标题稳定后跳到目标场景：
python Workspace/tools/p5_jump.py --pid <PID> 28 --wait 25   # 帧 27
python Workspace/tools/p5_jump.py --pid <PID> 8  --wait 20   # 帧 9
python Workspace/tools/p5_jump.py --pid <PID> 9  --wait 20   # 帧 10
```

- 帧 27 预期：状态 1–3/5/7–9 黄色低警；4/6/10 且门未关红色高警 + “关门”；99 中心红框；切 14/death 后预警立即消失。
- 帧 9 预期：Freddy>=2 黄字“Freddy 接近 n/6”；>=4 红字“Freddy 即将出现 n/6”+“准备应对”+顶部红条；>=6 或 got you>=1 中心“Freddy 已抓住你 / 本局结束”+红色全屏边框。
- 其他场景：按 §4 阈值；debug 行会显示当前场景字段与命中规则。

## 12. 后续

1. 用户自然游玩采样：帧 8/10/11/19/31 的阈值校准（用 recorder JSONL 离线算死亡前 N 秒曲线）。
2. 帧 13 `Counter` 标定后决定是否默认启用。
3. 可选：给 `state.cpp` 的 `kAlterableWatch` 加 `windpad`（帧 16）——需重新编译 DLL，收益/成本待定。
4. 多局误报率统计（需要真人多局测试）。
5. 声音提示（可选，接 `ctx.audio`）。
6. v0.4.0 之后：Ballora 帧 31 的 `scene31_ballora_danger` 尚未用于 ETA 二次过滤；多局误报率仍待真人采样。

## 13. v0.4.0（2026-10-27 00:2x）— Ballora 实时位置 / 多通道 / 安全提示 / 字体

### 13.1 新增显示层

- **多通道并存**：告警按方向（left/right/vent/top/center）各保留最高级一条，可同屏最多 5 条；不再只画最高优先的一条。attack 仍单独全屏闪烁（叠加在所有通道之上）。实现：`on_update` 里 `by_dir` 去重 + `S.alerts`，`on_draw_overlay` 先画 telemetry、安全提示、非 attack 告警，最后画单个 attack。
- **安全提示**：某方向此前有 high/attack、现在该方向无告警时，显示绿字「左门安全 / 右门安全 / 通风口安全 / 上方安全」，默认 2.5 s（`safe_notice_seconds`，GUI 滑条 0–5 s），与其它方向的威胁提示**同时显示**。只对 high/attack 消失生效，避免低警抖动导致闪屏。
- **Ballora 实时位置（帧 31）**：`Ennard.alterable[3]` 路径状态 → 左路/通风口/右路；`current volume`（0..50，音乐/距离感）与 `Ballora count`（每 200 ms −1，10 s/段）→ ETA。danger 段 ETA = count/5 秒；到门段显示「已到门」。红色条件：danger 段或（door 段且门未关）。
- **Ballora 实时距离（帧 8）**：`distance`/600 + `progress`；`ballora 距离 d/600 · 还在靠近 · 约 Xs 到死亡线`（(600−d)/10 s，按 W 时），回退时显示「正在回退」。颜色：≥high 红、≥low 黄、否则白。
- **帧 31 危险区红警 + 防守倒计时**：state 2（danger 段）→ 红色「左路危险 · 约 Ns 到门」+「按 A 关门」；state 4/6/10 且对应门未关 → attack「左门/通风口/右门未关！」+「立即按 X 关门」；门已关时该通道静默（只留位置行）。
- **attack 文案**：帧 27 保留「攻击已判定 / 立即关门或躲避」；帧 31 用 `s31_ballora_door` 时显示「<方向>门未关！/立即按 X 关门」。

### 13.2 实机证据（warp 帧 31 自定义夜，AI 真实推进）

测试实例 pid 20584，日志 `Workspace/logs/framework/slmodf-20261027-001644-20584.log`；用 `p8_alert_watch.py` 监听日志并在告警行出现时立即 PIL 抓图（DWM 抓图太慢会错过窗口）。

- 低警（state 1，左路）：`00:20:58.637 ... Ennard[3]=1 ... ballora=3 balloraCount=49 vol=1 alert=low dir=left`；截图 `Workspace/logs/probe/p9-sc31-low-002058.png`（黄框「Ballora 在左路」+ 位置行「Ballora 在左路 · 音乐 1/50」）。
- 红警（state 2，danger 段）：`00:21:07.920 ... Ennard[3]=2 ... balloraCount=50 vol=5 alert=high dir=left reason=Ennard[3]=2 count=50 vol=5`；截图 `p9-sc31-high-002107.png`（左侧红边条 +「左路危险 · 约 10s 到门」+「按 A 关门」+ 红字位置行「音乐 5/50 · 约 10s 到门」）。
- 攻击（state 4，到门且门开）：`00:21:17.437 ... Ennard[3]=4 ... alert=attack dir=left reason=Ennard[3]=4 left closed=0`；截图 `p9-sc31-attack-002117.png`（全屏红框 +「左门路未关！」+「立即按 A 关门」+ 红字「已到门」；同屏还有中心黄字「屏住呼吸，别动」= Minireena 低警，证明多通道并存）。
- 安全提示：对 state 2 的 red 通道写 `left is closed=1` + 清 `Ennard[3]` 后，日志 `00:27:59.125 ... alert=none dir=-`；截图 `p9-sc31-safe-002759.png`（左下绿字「左门安全」）。`p9-sc31-safe2-002759.png` 为 1.2 s 后仍在显示。
- 字体：注入 `Minireena Diff=3`、`Minreena progress=1`、`Oxygen left=20` → 日志 `00:30:15.080 ... oxy=19 ... alert=high dir=center reason=Oxygen left=19 progress=1`；截图 `p9-sc31-oxygen_d350.png` 红色「氧气不多了」的「氧」字正常（U+6C27 已包含），同屏还有「Ballora 在左路」低警 → 多通道 + 全字库同时验证。
- 帧 8 距离：warp 帧 8 后注入 `distance=430/560`，截图 `Workspace/logs/probe/p9-sc8-low_d150.png`（黄字「Ballora 注意到你了」+「距离 400/600 · 正在回退」）与 `p9-sc8-high_d150.png`（红框「马上停下！」+「松开前进键，等噪音回落」+「距离 557/600 · 正在回退」）。

### 13.3 新配置项（ThreatTelegraphs CONFIG）

- `show_position = true` — 关闭后不显示 Ballora 位置行。
- `safe_notice_seconds = 2.5` — 安全提示保持秒数（0 = 关闭）。
- `scene8_position = 150` — 帧 8 显示距离行的最小 distance。
- `scene31_ballora = true` / `scene31_ballora_danger = 12` — 帧 31 Ballora 路径建模开关与危险 ETA 阈值（当前 danger 段按段固定 ETA，未用该阈值做二次过滤；保留给后续调参）。

### 13.4 限制与注意

- 帧 31 warped 自定义夜（F9→custom level）能唤醒真实 AI，但从菜单进入的「简单难度 Angry Ballet」与 warp 的难度/初始值可能不同；阈值以用户在自定义菜单正常进入的一局为准。
- Ballora count 每个路径状态被重写为 50（10 s/段），因此 ETA 精度约 ±1 s；`current volume` 每 100 ms ±1 平滑，只作辅助。
- 到门后游戏只检查对应 `* is closed`；门关时 state 4 会回 0 并重新开始，安全提示因此可自然出现。
- 多通道渲染意味着低警黄字可能同时出现最多 5 条；如觉得吵，可在 GUI 关闭 `show_approach`。

## 14. v0.4.1（2026-10-27 00:5x）— 帧31 Ballora 时序模型修正

> v0.4.0 的帧31 Ballora 规则基于错误假设（到门即攻击、ETA=count/5），task-4 静态研究 Workspace/research/p7-global/ballora-timing.md 完成实机日志复核后给出修正，本版按研究结论重写模型；§13 中「到门 → attack」「danger ETA=count/5」「通风口路径」三处已被本节取代。

### 14.1 修正后的模型

- **脉冲周期**：period = (50 − Ballora Diff) × 0.2 s。Diff=3（默认 Angry Ballet）→ 9.4 s；hour 4 升到 8 → 8.4 s；hour 5 升到 13 → 7.4 s。
- **到下个脉冲的剩余时间**：remain = clamp(Ballora count − Ballora Diff, 0, 50 − Ballora Diff) × 0.2 s。Ballora count 每 0.2 s −1，脉冲时重置 50。
- **状态分级**（Ennard[3]）：
  - 4/10（到门）+ 对应门**开** → **high**，文案带实时倒计时「左/右门没关！约 Ns 到门口，立刻按 X 关门」；remain ≤ scene31_ballora_urgent（默认 2 s）时升级为「马上关门！」。
  - 4/10 + 门**关** → 该通道静默，显示「门已关，她要走了」；0–5 s 内她会撤退。
  - 3/9（确定性再走一步到门）→ **high**「Ballora 到左/右路近门了，准备关门」。
  - 2/8 → low「中段」；1/7 → low「远处」。
  - 0、hour == 6、Ballora Diff == 0 → 静默。
- **attack 只保留给 got you ≥ 1**（99 与 got you=2 同帧，插件基本观测不到 99；保留 99 分支为防御代码）。这修正了 v0.4.0「到门即 attack」——实际门开时到 99 还有最长约 9.4 s，正是最有价值的关门窗口。
- **通风口 5/6 在帧31 是死代码**（没有任何事件组把 Ennard[3] 置 5），因此不再映射、也不显示通风口 Ballora 提示；保留 got you 锁存兜底。
- **入场伪值门控**：刚 warp/加载时 Ballora Diff 可能是 0 或垃圾值（实测 12464，持续约 47 s）。插件要求连续 0.5 s（约 30 帧）读到稳定的 1..20 才启用 Ballora 规则，否则静默、只保留 got you 兜底。

### 14.2 代码改动

- Workspace/plugins/ThreatTelegraphs/main.lua → **v0.4.1**：BALLORA_PATHS 改为 far/mid/near/door 四段；ballora_info() 增加 Ballora Diff 1..20 稳定门控（update_ballora_gate）、Ballora count 存在性检查、period/remain/eta 计算、hour==6 静默；ballora_alert() 重写（door=high+倒计时、near=high、mid/far=low、门关=静默）；compute_telemetry() 帧31 位置行改为「门口/近门/中段/远处 + 剩余秒」；compute_scene31() 移除死代码通风口 generic 分支；GUI 帧31 区新增「门口倒计时紧急阈值」「位置行转红阈值」两个滑条。
- 新增 CONFIG：scene31_ballora_urgent = 2（门口倒计时 ≤N 秒升级「马上关门」）；scene31_ballora_danger = 12 改为位置行转红阈值（近门段使用）。
- 离线回归 Workspace/tools/tt_offline_check.lua 重写为 30 项断言（含稳定门控预热、door 倒计时 8 s/1 s、3/9 high、5/6 静默、垃圾 Diff 静默后重新武装、Diff=13 周期、多通道、hour6 静默、got you 攻击、安全提示开关）→ **SUMMARY pass=30 fail=0**。

### 14.3 证据与不确定项

- 研究 Workspace/research/p7-global/ballora-timing.md §0/§2.2–2.6/§5.2–5.5：脉冲间隔实测 9.273–9.528 s（Diff3）、8.27–8.52（Diff8）、7.27–7.52（Diff13）；4/10 门开 → 下一脉冲 99 → got you=2 同帧 → 0.75–1.0 s 死亡；门关 → 1.25–5.01 s 撤退。
- 限制：帧31 仅实测 Angry Ballet 预设（Diff=3），其它自定义预设未实测；入场早期 Diff 伪值/个别 run 段 counter 缺失时插件会静默，只保留 got you 兜底；current volume 滞后 3.5 s，只作 UI 提示，不参与判定。
- 本轮未重启游戏（pid 11292 仍在标题，新 Lua 在下次启动/重载插件后生效）。

## 15. v0.4.2（2026-10-27 01:1x）— 门关不等于安全：Ballora 到门后的 hold 警示

> 起因：用户实测 Custom Night（Angry Ballet）一局，右侧红警后关门，因未看到安全提示而重新开门，被下一脉冲秒杀（m11004）。事故复盘见 Workspace/research/p7-global/incident-20261027-ballora-reopen.md。本节取代 §13.4 中「门关时 state 4 会回 0，安全提示因此可自然出现」与 §14.1 中「4/10 + 门关 → 该通道静默」两句。

### 15.1 修正后的语义

- 帧31 的 Ballora 攻击在**脉冲**结算：门关只会在下一个脉冲把 Ennard[3] 从 4/10 置 0；关门后到状态离开门口之间仍有最长约 9.4 s 窗口。
- **4/10 + 门关 + 状态仍为 4/10** → 持续 **high（hold）** 告警：琥珀色「左/右路已关 · 她还在门口」「先别开门！约 Ns 后她才离开」。倒计时 N 是到下个脉冲的剩余时间（remain），不是安全时间。
- **状态真正离开 4/10**（Ennard[3] 变成 0 或其它路径）后，才允许绿色「左门/右门安全」提示；hold 告警存续期间 update_safe 不会误报安全。
- **门重新打开且状态 4/10** → 立即回到红色 high「左/右路没关！约 Ns 到门口，立刻按 A/D 关门」；remain ≤ scene31_ballora_urgent（默认 2 s）升级「马上关门」。
- hold 与未关门高警通过颜色区分：hold=琥珀色边条/文字，危险=红色边条/文字；两者都算 high 严重度（不进安全提示）。

### 15.2 代码改动

- Workspace/plugins/ThreatTelegraphs/main.lua → **v0.4.2**（plugin.ini 同步 0.4.2）：ballora_alert() 的 door+closed 分支不再 return nil，state 4/10 时返回 rule=s31_ballora_hold 的 high；draw_alert() 对 hold 使用 COL.yellow 边条/文字与暗色底；compute_telemetry() 门关+state 4/10 显示「先别开门」黄字，state 离开后才显示「她要走了」。
- 离线回归 Workspace/tools/tt_offline_check.lua：39 项断言（新增 E hold、E2 离开后才 safe、E3 右路 hold、E4 hold 期间开门升级），**SUMMARY pass=39 fail=0**。

### 15.3 证据

- 事故日志 slmodf-20261027-005545-21832.log：01:04:43.232 high right（门开）→ 01:04:44.471 right is closed=1 → 01:04:44.482 alert=none（v0.4.1 误清警）→ 01:04:47.733 用户重开门 high right → 01:04:48.095 scream op5-6 → 01:04:48.233 got you=2 → 01:04:48.986 custom death。state JSONL 在 01:04:44–48 期间 Ennard[3]=10、right is closed=1、Ballora count 31–48、volume 升到 50。
- v0.4.2 实机日志（pid 5264）：01:14:26.461 `... Ennard[3]=10 doorsL/R/T=0/1/0 ... alert=high dir=right reason=Ennard[3]=10 remain=9.4 closed=1 hold=1`；01:15:53.694 同类 hold 行；随后 state→0 才清空。
- 限制：hold 的实际长度取决于关门时距下一个脉冲的时间（可短至 0.3 s、长至 9.4 s），插件只能按 remain 估计；其它门威胁（Foxy/Bidybab/Freddy）的门关语义未做 hold 建模，后续按实测定。
---

## 16. v0.4.3–v0.4.4（2026-10-27）：后段 Ballora 门控与 OI 计数器名截断

### 16.1 v0.4.3：Ballora Diff 门控上限 20 → 60

- 事故：用户用 v0.4.2 通关 Angry Ballet 最高难度时，前半段正常、后半段（4–5 AM）反复出现「关门说安全、开门又叫关门」。
- 日志（pid 30240，slmodf-20261027-011829-30240.log）：4–5 AM 时 Ballora Diff = 25/30（每小时 +5，12 AM=3），而 v0.4.2 的稳定采样门控写作 `d > 0 and d <= 20` → 门控失效 → ballora_info() 返回 nil → 退回 generic Ennard 兜底（只在门开时 high）。因此关门后告警立刻清空（假安全），重开门又 high。
- 修复：`local valid = d > 0 and d <= 60`（main.lua 约 620 行）。离线断言 E5/E6 覆盖 Diff=25/30 的 hold；`tt_offline_check.lua` pass=42。

### 16.2 v0.4.4：Fusion OI 名称截断导致 Foxy 规则静默

- 用新工具 `Workspace/tools/p9_attack_probe.py`（100 ms 计数器采样 + 时间线注入）在帧 31 把 Funtime Foxy progression 推到 4/5：游戏里 progression 正常递增、got you 正常触发，但插件**从未**输出 Foxy 告警。
- 根因：Fusion OI 表把长名截断为 `Funtime Foxy progressio`（23 字符），插件按 `Funtime Foxy progression` 精确查 `ctx.game.counters` 永远返回 0。
- 修复：`num()/has()` 增加唯一前缀匹配（双向、共享前缀 ≥8 字符、歧义则放弃）；v0.4.4 / plugin.ini 同步。修复后实机日志：01:40:14 `foxyProg=4 ... alert=high dir=right reason=progression=4`；01:40:23 `foxyProg=5 gotYou=2 ... alert=attack dir=center`（右门未关）。
- 离线断言新增 P/P2（截断名仍触发 Foxy 高警/攻击）、Q/Q2（Bidy 开/关通风口）、R（Freddy count 低值高警）；`tt_offline_check.lua` pass=47 fail=0。

### 16.3 其它敌人的门关语义（自动化实测）

- Foxy：progression==5 + 右门关 → G414 在下一 tick 重置为 random(2)（实测 progression 5→1），不需要 hold；右门开 → got you=1（实测 progression=5 同 tick got you=2、custom death）。progression==4 + 右门关每 5 s +1，所以「关门」仍是最优动作。
- Bidybab：Bidy prog==4 + 通风口关 → G271/G272 立即归零（实测 4→0，0.1 s 内），不需要 hold；通风口开 → prog=5 后约 3 s 触发 got you=3。
- Freddy/Bonnie：攻击条件读 Freddy[6]/[0]（不在当前 alterable 白名单），counter 只有 `Freddy count`；插件只能给双门低/高警，无法承诺具体方向。**（v0.5.0 已由 native `ctx.game.alterable("Freddy",i)` 解决，精确模型见 §17.1）**atch（需重建 DLL）。
- Bonnet：v0.4.4 时无前置 counter、同帧判定，无法提前预警；**v0.5.x 已通过动态对象读取 `Active 19` 实现行走倒计时/点鼻提示，见 §17.3。** Minireena/氧气只有低氧提示，没有 oxygen==0 致死组。
- 自动化方法与证据：`Workspace/research/p7-global/enemy-simulation-20261027.md`；JSONL `Workspace/logs/probe/p9-foxy-sim-24472.jsonl`、`p9-bidy-sim-24472.jsonl`。

### 16.4 限制

- 注入会把状态机推到指定分支，但游戏自身逻辑仍会继续运行/覆盖注入值；计时型参数（count/Diff）只能作为"近似自然"的样本，最终数值仍需真人自然局复核。
- 帧 31 的 F9 跳转不使用 Custom Night 菜单的难度设置，注入 *Diff 是让敌人"活跃"的必要步骤。
- Foxy/Bidy 的 counter 名称截断问题可能影响其它长名 counter；v0.4.4 的前缀匹配已统一兜底，但新规则仍应记录实际 OI 名。

## 17. v0.5.1 — 帧 31 精确敌人模型：Freddy 门/阶段、Bonnet 倒计时、状态字幕、动态对象读取

> 用户 m11423 要求：Freddy 攻击前有语音提示、最终由 Bon-Bon 触发 jumpscare（需要字幕系统）；Bonnet 在放下监控时出现并走到玩家，处理方式是点鼻子。本节记录 v0.5.0（2026-10-27 02:5x）实现与 v0.5.1 修正（03:0x）。

### 17.1 Funtime Freddy（帧 31）精确模型

静态依据：`recon/dumps/Sister Location/Events/frame_31_custom level_events.txt`（行号见下），运行时由 native `ctx.game.alterable("Freddy", i)` 读取。

| alterable | 语义 | 取值 |
|---|---|---|
| `[0]` | 侧别 | 1=左（相对 `cam 3`），2=右（相对 `cam 4`）；Group317/318 行 4170–4188；Group331 每 tick 随机重置 |
| `[1]` | 语音序号 | 1..4 轮流；Group326–329 行 4320–4354 → 样本 #351 `Freddy_DontKeepWaiting` / #352 `Freddy_readyornot` / #353 `Freddy_OhBirthdayBoy` / #354 `Freddy_KnockKnock`；>4 归 1（Group330） |
| `[2]` | 在场/排队 | 250（Group319 行 4190–4219 设置；Timer 10 s + `Freddy count`<=`Freddy Diff`(OI 665) + `flip it[0]==0`） |
| `[3]` | 攻击语音变体 | 1..3（Group333 行 4380–4398 随机；Group334–336 → #355 `Freddy_GoGetHim1` / #356 `_2` / #357 `Freddy_Surprise`） |
| `[6]` | 阶段 | 0 idle；1/2/4 = GoGetHim 语音播放中；3 = 到 alt0 侧门；5 = 跨到对侧门（Group341–343 行 4466–4494） |

**攻击方向映射**（Group344–347，行 4496–4578；Create x105 `Bonnie scare` + sample #303 `scream op5-6` + got you=3）：

- `alt6==3 && alt0==1` → 威胁在**左门**（`left is closed==0` 时触发）
- `alt6==3 && alt0==2` → **右门**
- `alt6==5 && alt0==1` → **右门**（跨过门到对侧）
- `alt6==5 && alt0==2` → **左门**
- 对应门已关 → Group348–351（行 4580–4650）播 #358 关门音 + `alt6=0`（取消，插件转为 hold/安全提示）

插件规则（`freddy_alert`）：phase 3/5 → 按上式算方向，门开 = `high`「Freddy 在X门！/立刻关X」；门关 = `low`「X已关 · Freddy 被挡住」；phase 1/2/4 = `low`「Freddy 要出手了」；`alt2>0` 且 phase 0 = `low center`「Freddy 在说话」。读不到 alterable 时回落 v0.4 的 `Freddy count` 阶梯。

**Bon-Bon = Freddy 的 jumpscare 载体**：死亡样本始终 #303 `scream op5-6`（`Create x105 "Bonnie scare"`），已由 SoftJumpscares 全游戏柔化覆盖（帧 27 同一路径，见 `docs/AUDIO_HOOK.md`）。

### 17.2 状态字幕（Phase 9 试点）

- 不依赖音频指纹：392 个音频样本中 214 个是 MP3 帧（`parse_wav` 失败），`sound_table.h` 只有 178 条且不含 Freddy 语音（`tools/gen_sound_table.py` `missing_wav=214`）。因此字幕改为**状态驱动**：`alt[1]` → 语音文本、`alt6` 1/2/4 → 「Go get him!」/「Surprise!」。
- 渲染：底部居中黑底白字（`BASE_FONT*scale*0.85`，y=`h-th-64*scale`），与预警/安全提示分层；`CONFIG.subtitles`、`subtitle_seconds=2.5`（S.subtitle/subtitle_until 帧数控制）；GUI 有开关与保持时长滑条。
- 证据：pid 23520 → `p9-v050-freddy-subtitle2.png`（中央黄框「Freddy 在说话」+ 底部「Freddy: Are you ready or not?」）、`p9-v050-freddy-door-high.png`（高警解除后「左门安全」与语音字幕同屏）。
- 框架级设计已写入计划 **Phase 9 — 字幕系统**（`ctx.ui.subtitle(text[,seconds])` / `subtitle_clear()`；提案未实现）。

### 17.3 Bonnet（帧 31）机制与倒计时

静态依据 `frame_31_custom level_events.txt`：

- 生成：Group388（行 5031–5038）`flip it[0]==2`（监控抬起）每 tick 脉冲 `make Bonnet[0]=1`；Group389（行 5040–5052）`make Bonnet==1` + `Active 19` 不在场 + got you==0 + **random(30) < `Bonnet Diff`** → Create x120 `Active 19`；Group390 清脉冲。
- 行走：Group404–408（行 5204–5267）`Active 19` 在场 + `alt[1]==0` + `alt[6]==1` + `Bonnet Diff >= 1/5/10/15` → 每 tick 移动 **(-3, 0)**，即速度 = **180 × N px/s**，N = 1+(Diff>=5)+(Diff>=10)+(Diff>=15)。Group409（行 5269–5277）`flip it[0]==0`（放下监控）→ `alt[6]=1` 开始走。
- 点鼻（正解）：Group392/393（行 5066–5087）玩家左键点 `Bonnet OFF` 与 `Active 3` 重叠 → `Active 19 alt[1]=1` + 播 #59 `SwitchPush_S08FO_2523`；Group394 强制动画 12、`alt2=2`；Group395（`alt2>70`）销毁。
- 死亡：Group401（行 5162–5173）`Active 19` 碰撞（cond #-17）+ hour!=6 → got you=1、销毁。
- 声音：Group396–400（行 5107–5160）`flip it==3` 时随机播 #371–374 `Giggle 2..5`。

**插件规则**（`bonnet_info/bonnet_alert`）：`Active 19` 存在时——`hit`（alt1==1 或 alt2>0）→ `low`「Bonnet 已被打中」；`flip==2` → `low`「Bonnet 在监控画面里/放下监控后点她的鼻子」；`flip` 0/3 → `high`「Bonnet 走过来了 · 约 Ns」+「点她的鼻子」，ETA ≤ `scene31_bonnet_urgent`(2 s) 升级「马上点 Bonnet 的鼻子！」。ETA = (x − target_x)/speed，`target_x=-230`（diff=5 实测死亡时 x≈−229）、速度 180×N。位置行在 `compute_telemetry` 同步显示（白/黄/红/绿）。

**实测（pid 15680 warp 帧 31，`tools/p9_bonnet_sim.py/probe.py`）**：diff=20 → 1.92 s 死亡；diff=1 → 9.08 s（比值 4.72 ≈ 4×，与 N 阈值一致）；diff=5、N=2 走行速度实测 **359.4 px/s ≈ 3 px/frame × 60 × N**；diff=30 spawn 稳定 ~2 s，diff=3/12 在 20 s 内可能不 spawn（随机性）。

### 17.4 动态对象读取（native rhObjectList 回退）

- 问题：`state.cpp` 的 `BuildMap()` 只在场景切换时全内存扫描 `dword==rh`，事件 Create 的对象（帧 31 `Active 19`、`Bonnie scare` 等）不在 Published 快照 → `ReadObjectRect("Active 19")`/`ObjectRval` 恒失败。
- 方案：读 `RunHeader.rhObjectList`（`rh+0x1278`）与 `rhNObjects`（`rh+0x3C`），每项 8 字节 `{objPtr, routine}`，用 `hoOi(@0x12)` 查 OI 句柄→名字，`_stricmp` 匹配；`STATE_MAX_LIVE_OBJS 4096`；先校验 `rh+0x1C == g_mapFrame`。
- 接入：`ObjectRval()`/`ReadObjectRect()`/`WriteObjectPosition()` 末尾回退；日志 `state| live rect fallback name=Active 19 obj=1D3E7838 x=1581 y=828 w=700 h=700`（预算 8 行）。
- 插件新 API：`ctx.game.object_rect(name)` → `{x,y,w,h}` 或 nil；`ctx.game.alterable(name,index)` → int 或 nil（native `state::ReadObjectRect` / `ReadAlterableLive`）。
- 偏移推导与验证：镜像 `Cncf.h` 结构体 `#pragma pack(2)` 打印 `offsetof`，用已知锚点 rhApp=0x18 / rhFrame=0x1C / rhQuit=0x30 / rhQuitParam=0x38 / rhNumberOi=0x54 / rhOiList=0x58 全部吻合；实机遍历 251 项与全内存扫描 251 项一致。

### 17.5 v0.5.1 修正：published alterable 快照滞后

- 现象：Bonnet 写入 `flip it[0]=0` 后 `Active 19` 已开始走，但插件一直显示「在监控画面里」（低警），无 high。
- 根因：`bonnet_info` 先读 `ctx.game.alterables["flip it"][0]`（Published 快照，随场景构建，滞后），实测 `flipPub=2` 而 `flipLive=0`。
- 修复：优先 `ctx.game.alterable("flip it",0)`（live 读），失败再回落 Published；新增 `bonnet_dbg` 调试日志（每 20 次 `bonnet_info` 一行，`CONFIG.debug` 控制）。
- 证据（pid 29848，`log slmodf-20261027-030647-29848.log`）：03:07:32.852 `bonnet_dbg ... flipPub=2 flipLive=0`；03:07:32.853 `... bonnet=x=1201 eta=2.0 hit=false ... alert=high dir=center reason=Bonnet x=1201 eta=2.0 speed=720`。

### 17.6 证据与回归

- 实机 Freddy 高警（pid 23520）：`02:52:09.098 ... freddy=1/3 ... alert=high dir=left reason=Freddy alt6=3 alt0=1 closed=0`（正是用户要求的「精确门」）；同 tick `got you=3 alert=attack`（游戏原生 Bon-Bon 判定）。
- 实机字幕（pid 23520）：`p9-v050-freddy-subtitle2.png`、`p9-v050-freddy-door-high.png`；标题 `p9-v050-ready.png`。
- 动态对象回退（pid 26784）：03:02:57.996–58.046 连续 8 行 `state| live rect fallback name=Active 19 ...`。
- 离线：`Workspace/tools/tt_offline_check.lua` **pass=64 fail=0**（新增 S/S2–S7 Freddy 阶段/字幕过期/无 API 回落；T/T2–T5 Bonnet ETA/紧急/监控/已打中/缺失静默）。
- 构建：`bin/SLModFramework32.dll` 1,611,776 B（含 object_rect/alterable + rhObjectList 回退）、`harness32.exe` 1,509,376 B；离线三模式 PASS。

### 17.7 配置与 GUI 新增

- CONFIG：`scene31_freddy`、`scene31_bonnet`、`scene31_bonnet_urgent=2`、`scene31_bonnet_target_x=-230`、`subtitles=true`、`subtitle_seconds=2.5`、`scene31_ballora_urgent`（v0.4.1 已有）。
- GUI：主界面「状态字幕 (Freddy 语音)」+「字幕保持 (秒)」；帧 31 区「Freddy 精确门/阶段」「Bonnet 行走倒计时」「Bonnet 紧急阈值」；状态区显示当前字幕与 Freddy/Bonnet 状态。

### 17.8 限制

- **Bonnet 生成是随机的**（random(30) < Diff），不能假设必然出现；插件仅在 `Active 19` 已存在时提示，不预报生成。
- 行走 window 约 1–6 s（依 Diff），`window_capture` 端到端约 1.2 s，**尚无行走 high 的视觉截图**；证据为日志行 + `p9_bonnet_sim.py` JSONL。
- Freddy 语音字幕文本由 dump 的样本名映射，若版本变化导致 alt 序号/样本错位，字幕可能错配（计划 §8 已登记风险）；不得依赖音频指纹。
- 上述 Freddy/Bonnet 验证多来自 F9 warp + 状态注入仿真（像素级时序可信，但非自然局）；仍需用户自然游玩复核（已列入下一轮）。
- 23/10/2026 03:0x 状态：插件 v0.5.1 默认关闭（测试态临时 enabled=true）；ThreatTelegraphs 转示例插件，剩余完善延后到框架完工后（用户 m11423）。


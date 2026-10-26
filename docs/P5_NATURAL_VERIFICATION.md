# P5 自然游玩复核报告（Night 2 / Night 5）

- 日期：2026-10-26（本地时区 Asia/Shanghai）
- 目的：用真人自然游玩，独立复核 P5 运行时场景/计数器读取，以及 P5.0 warp 取证得到的场景与死亡链结论（双轨证据规则）。
- 证据等级：【实机】= 自然游玩时的框架 JSONL/日志；【静态】= 事件组 dump；【推断】= 由实测+静态组合得出；【未解】= 尚无直接证据。
- 结论摘要：自然游玩完整走通 Night 2（Ballora Gallery、Breaker Room）与 Night 5（Private Room/Final Encounter）三条死亡链；计数器语义、场景编号、死亡后 8-bit 小游戏均取得实测数据；发现并修正了 3 处静态分析/文档偏差（帧 27 的自然入口、keycard 前置、死亡小游戏随机计数器）。Phase 5 自然复核验收项可判为完成。

## 1. 方法与环境

- 启动方式：`Workspace/framework/bin/launcher32.exe --test-exit 2400`（框架加载、SHA-256 指纹门禁通过后启动游戏）。自然游玩由用户操作，agent 只负责启动、配置录制、读取日志/状态并做时间线分析。
- 配置：`Workspace/config/framework.ini` 临时设 `record=1`、`record_interval_ms=500`；`audio_probe=0`、`audio_soften=0`（保证音频 Hook 不介入，排除框架对声音的影响）。
- 状态录制：`Workspace/logs/state/state-<ts>-<pid>.jsonl`，每 500 ms 一行，含 `scene`/`scene_name`/`scene_changed`/`counters`；另有 `Workspace/logs/framework/slmodf-*.log` 记录场景切换、帧 id、注入状态。
- 存档：原始存档 SHA-256 `6a67732756457b348a7d46b388b904183a58babc67553a2979427ac4eaa5e17b`（`[sl] intro=1 current=1`），备份 `Workspace/backups/save/sl-20261026-043019.bak`。为进入 Night 5，按前置进度把 `current=5`；为通过 keycard 闸门补 `keycard=1`（理由见 §3.1）。复核结束后已恢复原始存档与默认配置。
- 数据导出：
  - `Workspace/analysis/data/night5-private-room-natural-20261026.csv`（938 行，帧 27 全部 500 ms 采样）
  - `Workspace/analysis/data/night2-breaker-room-natural-20261026.csv`（102 行，帧 9 全部采样）
  - 原始 JSONL 与日志保留在 `Workspace/logs/` 下，未删改。

## 2. 会话与场景链

### Session 1（pid 24092，2026-10-26 04:30–04:48，Night 2）
- 场景链：title → Girl Voice(25) → Elevator(2) → Vent crawl(3) → Main Hub(5) → Vent crawl → Baby's Room(6) → Under Desk(13) → Vent crawl → Main Hub → **Ballora Gallery(8)** → death(14) → game over(15) → Main Hub → **Ballora Gallery(8)** → **Breaker Room(9)** → death(14) → game over(15) → Main Hub。
- 本次有两次自然死亡：Ballora Gallery 被 Ballora 抓、Breaker Room 被 Freddy 抓。

### Session 2（pid 26740，04:49–04:58，Night 5 第一次尝试）
- 场景链：title → Girl Voice → Elevator → Vent crawl → Main Hub → **Funtime Auditorium(10)** → **Parts and Service 2(19)** → Funtime Auditorium(10) → death(14) → game over(15) → Main Hub。
- 未到 Private Room：在 Funtime Auditorium 的 keycard 闸门处因存档无 keycard 播 "denied" 后死亡（见 §3.1）。这次死亡**不是** Private Room 的死亡链。

### Session 3（pid 28120，05:01–05:17，Night 5 补 keycard 后）
- 场景链：title(1) → Girl Voice(25) → Elevator(2) → Vent crawl(3) → Main Hub(5) → Funtime Auditorium(10) → Parts and Service 2(19) → Funtime Auditorium(10) → **Final Encounter(27)** 05:08:09.005 → death(14) 05:15:59.971 → **8-bit Baby Game(26)** 05:16:05.213 → game over(15) 05:17:07.854 → Main Hub(5) 05:17:33.074。
- 用户说明（m06482，paraphrase）：多年未玩、水平下降；电力到 5 点左右撑不住，索性 AFK 等 jumpscare；这次触发了死亡时随机出现的 8-bit 小游戏。

## 3. 关键发现

### 3.1 帧 27 的自然入口：帧 10 Funtime Auditorium 的 keycard 闸门（修正旧文档）

- 静态依据（`recon/dumps/Sister Location/Events/frame_10_Funtime Auditorium_events.txt`）：
  - Group 113：帧启动读 `Ini` 的 `keycard`；
  - Group 114：`keycard==0` 且正/右测量值 ≥5 → `have keycard=2`、Speaker 播 sample #50 `"denied"`、`Script Event=678`；
  - Group 116：`keycard==2` 且 `got you==0`，2 s 后 → `got you=1` 进入死亡链。
  - `frame_26_8-bit Baby Game_events.txt`：小游戏完成（`died==2`）时执行 `DO "Ini" act #87 keycard = 1`——keycard 是完成前置小游戏后写入的进度奖励。
- Session 2 实测：04:57:42.519 `have keycard=0 Script Event=621` → 04:57:43.523 `have keycard=2 Script Event=678` → 04:57:45.529 `have keycard=2 got you=2 death timer=8` → death 04:57:47.264。与静态链一致。
- Session 3 补 `keycard=1` 后：05:07:59.115 进 Funtime Auditorium，约 10 s 后（05:08:09.005）直接进 Final Encounter，不再触发 denied 分支。
- **修正**：旧 `analysis/night5-private-room.md` 写「进入来自帧 14 death 或帧 18 Extras」。实测自然路径是 **帧 10 Funtime Auditorium（keycard 闸门）→ 帧 27**；帧 14 death 只会进帧 15 game over，不进帧 27。Extras 路径未在本次复核中验证；v1.7 曾观察到 Extras 点 little Baby 实际进帧 26（handle→index 映射按名称反查会出错），不能作为帧 27 入口证据。
- 处置：把 `keycard=1` 视为「跳过前置小游戏后的进度补齐」，不是改游戏逻辑；仅为取证环境准备，复核后恢复原始存档。

### 3.2 Night 2 Ballora Gallery（帧 8）自然死亡

- 死亡前计数器：04:41:46.047 `jumpscare=0 distance=582 progress=102 side=1 moving toward=2 crawl=14`；
- 04:41:46.548 `jumpscare=1 distance=601 crawl=0`（0.5 s 内 `jumpscare 0→1`）；
- 04:41:48.046 切帧 14 `death`（death 计数器 `planned death=0 random=5`）；
- 04:41:53.347 帧 15 game over。
- 结论：Ballora 的抓取在 `jumpscare` 计数器上先置 1，约 1.5 s 后切 death；death 帧 `random=5` 该次未进入 8-bit 小游戏。

### 3.3 Night 2 Breaker Room（帧 9）自然死亡

- 04:47:04.115 进入帧 9（109 instances）。
- 时间线（关键行）：
  | 时间 | 事件 |
  | --- | --- |
  | 04:47:47.229 | `Freddy 1→2`，`vol level 14→30` |
  | 04:47:49.234 | `Freddy=4` |
  | 04:47:51.238 | `Freddy=6 got you=1` |
  | 04:47:52.740 | `interface=100 percentage 2=100` |
  | 04:47:54.243 | `got you=2` |
  | 04:47:56.062 | 切帧 14 death（`planned death=0 random=3`） |
  | 04:48:01.113 | 帧 15 game over |
- 与静态 G58–G61 完全吻合：`Freddy>=6` → `got you=1` → scream/pop → `got you=2` → Jump 帧 15；从 `got you=2` 到 death 约 1.8 s。`random=3` 该次未进入 8-bit 小游戏。
- 这说明 P5 的计数器读取（`Freddy`、`got you`、`interface`、`beat` 等）在真实游玩中可信，可直接作为 P7 ThreatTelegraphs 的输入。

### 3.4 Night 5 Private Room / Final Encounter（帧 27）自然死亡

- 进入：05:08:09.005，帧 27，77 instances，1900×800，4 层。
- 入场计数器：`hour=12 power left=99 final stage=0 side=2 viewing cam=1 got you=0 near=0`，`power count down=-31207`。
- 主时间线（全部来自 500 ms JSONL）：

  | 时间 | 事件 | 备注 |
  | --- | --- | --- |
  | 05:08:09.005 | 进入帧 27 | hour=12, power=99, final stage=0 |
  | 05:08:12.515 | `final stage 0→1` | +3.51 s |
  | 05:08:38.585 | `final stage 1→2` | +29.58 s，此后整局保持 2 |
  | 05:09:38.244 | `hour 12→1` | +89.24 s |
  | 05:11:08.500 | `hour 1→2` | +90.26 s |
  | 05:12:38.239 | `hour 2→3` | +89.74 s |
  | 05:14:08.489 | `hour 3→4` | +90.25 s |
  | 05:15:38.235 | `hour 4→5` | +89.75 s；hour=6 为本局胜利条件（G203/G207，未实测） |
  | 05:15:58.285 | `got you 0→2`，同一行 `side 3→2` | 500 ms 采样合并了 1→2，见下 |
  | 05:15:59.971 | 切帧 14 death | 最后一条 FE 行：got you=2 final stage=2 hour=5 power left=7 side=2 |
  | 05:16:05.213 | 帧 26 8-bit Baby Game | death 后约 5.2 s |
  | 05:17:07.854 | 帧 15 game over | 小游戏约 62.6 s 后 |
  | 05:17:33.074 | 帧 5 Main Hub | — |

- `power left`：99 起，约每 5.1 s 降 1 点，死亡时 7（不是 0）；`power count down` 从 -31207 变到 7503 后触发了某种归零/换算（G211/G212），死亡时该值不是 0，说明本局死因不是断电。
- `side`：取值 1/2/3，对应 Ennard 与 `left side`/`middle`/`right side` 三个 Active 的重叠（Group 215–217），并驱动环境声 pan（Group 234–236：`side` 1/2/3 → pan -50/0/50）。本局 AFK 时 `side` 反复 2→3→2→1→2…，死亡瞬间为 2（middle）。
- `play sound Baby` 累计 14 次、`play sound Girl` 从 05:14:17 起累计到 7 次；它们是“触发次数的计数器”，不是音量/可听度。
- `viewing cam` 取 1/2/5（切换监控视角的状态值）；`trigger ready` 周期性 0↔1（约每 1 s，点/闸门判定脉冲）。
- `left/right/top is closed` 在 AFK 时也会短暂 0→1→0（约 1 s）；玩家未操作，语义未完全确定（可能是 Ennard 对门的“检查/经过”状态，而非玩家关门状态）；不能直接当“玩家关门”用。
- `near`、`was here`、`going here` 整局保持 0：`near` 在帧 27 事件 dump 中没有任何引用（可能是未使用/占位计数器），`was here`/`going here` 则由监控视角相关组驱动（本局未用监控）。
- 死亡链归因（【推断】）：`got you` 从 0 直接到 2，与静态 Group 196（`Ennard.alterable[3]==99` 且 `hour!=6` → `got you=1`）+ Group 200（`flip it.alterable[0]==0` 且 `got you==1` → `got you=2`、创建 `scare 2`、立即 game over）一致；500 ms 采样未捕捉到 1 的中间态。分支 A（`flip it.alterable[0]==2` + `got you==1` + 5 s Timer → `got you=3` → `scare 1` → 1.5 s 后帧 15）本局未触发。未直接读 `Ennard.alterable[3]` 与 `flip it.alterable[0]`，故标【推断】。
- **P7 预警含义**：`got you` 在到达门后 0–0.5 s 内从 1 变 2，死亡约 1.7 s 后发生，作为预警信号太晚；真正的前置信号是 `side`（Ennard 在左/中/右）与 `Ennard.alterable[3]==99`（到达门口）。P7 若要读 Active 的 alterable 字段，需要在 state 模块补充对象 alterable 读取能力（当前只暴露计数器）。
- 死亡小游戏：death 帧 `planned death=0 random=1`，5.2 s 后进帧 26 8-bit Baby Game，小游戏 62.6 s 后 game over。对比 Session 1 两次死亡 `random=5`、`random=3` 均未进帧 26；Session 2 `random=3` 也未进帧 26。**观察**：`random` 与死亡小游戏选择相关，本样本中仅 `random=1` 触发 8-bit 小游戏；选择规则未解。

## 4. 计数器语义（自然复核后）

| 场景 | 计数器/对象 | 实测语义 | 证据等级 |
| --- | --- | --- | --- |
| Ballora Gallery(8) | `jumpscare` | 0=安全，1=已被抓；0→1 后约 1.5 s 切 death(14) | 【实机】 |
| Ballora Gallery(8) | `distance`/`progress`/`side`/`moving toward`/`crawl` | 距离/进度/方位/移动方向/爬行状态；本局 death 前 distance 582→601 | 【实机】 |
| Breaker Room(9) | `Freddy` | 危险累积 0→6；≥6 触发 `got you=1` | 【实机】 |
| Breaker Room(9) | `got you` | 0=安全，1=被抓，2=已播 jumpscare；2 后约 1.8 s 切 death | 【实机】 |
| Breaker Room(9) | `interface`/`percentage 2` | 操作进度；实测可到 100（但该局仍被抓，说明被抓与 interface 无直接互斥，需结合 beat/Hand Unit） | 【实机】 |
| Final Encounter(27) | `hour` | 12→1→2→…→5，约 90 s/小时；hour=6 为胜利条件（静态 G203/G207） | 【实机】+【静态】 |
| Final Encounter(27) | `power left` | 99 起每 ~5.1 s 减 1；本局死亡时 7，非断电死 | 【实机】 |
| Final Encounter(27) | `final stage` | 0→1 约 +3.5 s，1→2 约 +29.6 s，之后保持 2 | 【实机】 |
| Final Encounter(27) | `side` | 1=左/2=中/3=右，Ennard 与三个 side Active 重叠时更新，并决定环境声 pan（-50/0/50） | 【实机】+【静态】 |
| Final Encounter(27) | `got you` | 0→2（经 Group196/200 瞬间完成）→死亡；作为预警太晚 | 【实机】+【推断】 |
| Final Encounter(27) | `near`/`was here`/`going here` | 本局全 0；`near` 无事件引用（疑似未使用），后两者由监控视角路径驱动 | 【实机】 |
| death(14) | `planned death`/`random` | `planned death` 本次全 0；`random` 1/3/5，仅 1 观测到进入 8-bit 小游戏(26) | 【实机】观察 |

## 5. 对静态分析与文档的修正

1. **帧 27 入口**：旧文档「来自帧 14 death 或帧 18 Extras」→ 自然主路径是帧 10 Funtime Auditorium 的 keycard 闸门。帧 14 death 只进帧 15 game over。Extras 路径未复核。
2. **keycard 前置**：帧 10 的 denied 分支不是 bug，是缺 `keycard=1`；`keycard` 由帧 26 8-bit Baby Game 完成时写回存档。此前把 `current` 直接设 5 会跳过该前置，制造“Access Denied → jumpscare”的误判。
3. **死亡小游戏**：death 帧的 `random` 计数器与是否进入帧 26 8-bit Baby Game 相关（实测 random=1 进入，3/5 未进入）；旧文档未记录该字段。
4. **near 等计数器**：帧对象列表中的 Counter 不一定被事件引用（`near` 在帧 27 dump 中零引用）；P7 选预警信号时必须先确认它确实被事件组更新，不能只凭名字。
5. **自然路径不会经过帧 14 death 进入帧 27**：帧 27 的死亡链只会出到帧 14/15/26，不会从 death 回帧 27。

## 6. 未解项 / 后续

- hour=6 的胜利路径（G203/G207 → fade out → 帧 6 Baby's Room）未自然走通；需要一次“撑到 6 点”的录像/日志才能确认胜利序列与奖励写回。
- `flip it.alterable[0]` 的 0/2/3 语义（门/闸门状态）与分支 A（5 s Timer → got you=3 → scare1 → 1.5 s 后帧 15）未实测；只观测到分支 B。
- `Ennard.alterable[3]` 从多少开始、以多快速度趋近 99（到门）未读；这决定 P7 预警窗口。P5 state 目前不读 Active alterable，需要扩展。
- `left/right/top is closed` 在 AFK 下自动 toggle 的机制未解；若它表示 Ennard 对门的检查，可做更早预警；若是玩家门状态，则说明 AFK 时门状态也在被系统改写。
- 死亡小游戏 `random` 的取值表（1/3/5 对应什么）未解，样本只有 4 次死亡。
- Ballora 音乐“偏小”的用户主观感受未做 A/B 客观测量；本局框架 audio hook 未安装（日志 `audio| probe disabled`、`audio=0/0/0/0`），可排除框架介入，但不能排除游戏本身声像/距离衰减。后续可在同一位置做原版直跑 + 录制系统输出对比。

## 7. 证据文件

| 文件 | 内容 |
| --- | --- |
| `Workspace/logs/framework/slmodf-20261026-043052-24092.log` | Session 1 启动/场景/注入日志 |
| `Workspace/logs/state/state-20261026-043055-24092.jsonl` | Session 1 500 ms 状态（Night 2 两次死亡） |
| `Workspace/logs/framework/slmodf-20261026-044904-26740.log` | Session 2（Night 5 denied 分支） |
| `Workspace/logs/state/state-20261026-044907-26740.jsonl` | Session 2 状态 |
| `Workspace/logs/framework/slmodf-20261026-050105-28120.log` | Session 3（Private Room 自然死亡） |
| `Workspace/logs/state/state-20261026-050107-28120.jsonl` | Session 3 状态（2040 行 / 14 次场景切换） |
| `Workspace/analysis/data/night5-private-room-natural-20261026.csv` | 帧 27 全部计数器 500 ms 采样 |
| `Workspace/analysis/data/night2-breaker-room-natural-20261026.csv` | 帧 9 全部计数器 500 ms 采样 |
| `recon/dumps/Sister Location/Events/frame_10_Funtime Auditorium_events.txt` | keycard 闸门静态证据 |
| `recon/dumps/Sister Location/Events/frame_27_Final Encounter_events.txt` | 死亡链 Group 196/199/200 静态证据 |
| `recon/dumps/Sister Location/Events/frame_26_8-bit Baby Game_events.txt` | keycard 写回静态证据 |

## 8. 复现步骤

1. 备份存档：`python Workspace/tools/save_night.py --backup`；设 `--night 5`（如需）与 `--set keycard=1`。
2. 备份 `Workspace/config/framework.ini`，设 `record=1`、`record_interval_ms=500`、`audio_probe=0`、`audio_soften=0`。
3. `Workspace/framework/bin/launcher32.exe --test-exit 2400` 启动；确认日志 `scene| fast idx=1 name="title screen"`。
4. 自然游玩到 Private Room；期间不要退出游戏。结束后从 `logs/state/state-*.jsonl` 用 Python 过滤 `scene_name=="Final Encounter"` 即可还原本报告表格。
5. 结束：`taskkill` 游戏与 launcher；`save_night.py --restore <备份>`；恢复 `framework.ini`。

## 9. 对 Phase 7（ThreatTelegraphs）的直接输入

- 可用、已验证的预警信号（按提前量排序）：
  1. `side`（Private Room）：Ennard 左/中/右方位，随移动实时更新；配合环境声 pan 可做左右提示。
  2. `Freddy`（Breaker Room）：0–6 累积，≥3/≥5 分档预警；`beat==1` 时关闭。
  3. `final stage`/`hour`/`power left`（Private Room）：阶段/时间/资源三条基线，可用于 HUD 而非惊吓预警。
  4. `jumpscare`（Ballora Gallery）：0→1 后 1.5 s 死亡，只能做“已触发”提示。
- 不建议作为预警：`got you`（Private Room 0→2 太快）、`near`（未验证/疑似未使用）、`left/right/top is closed`（语义未定）。
- 需要补的能力：P5 state 读取 Active 的 `alterable`（至少 `Ennard.alterable[3]`、`flip it.alterable[0]`），否则 Private Room 的早期预警只能依赖 `side`。

---

【实机复核完成】2026-10-26；环境已恢复（原始存档 SHA-256 `6a67732756457b348a7d46b388b904183a58babc67553a2979427ac4eaa5e17b`，config 恢复默认 `record=0`）。

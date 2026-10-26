# QoL（Phase 8）— 失焦解锁鼠标 / 小键盘密码盘 / Private Room 快捷键

> 状态：**已实现并通过首轮实机验证**（2026-10-26）。框架版本 0.5.0-p8。
> 所有功能默认开启，可在 `Workspace/config/framework.ini` 中关闭。
> 本文只覆盖 Phase 8 QoL；攻击预警见 `docs/THREAT_RULES.md`，柔化 jumpscare 见 `docs/AUDIO_HOOK.md`。

## 0. 一句话结论

| 功能 | 配置键（默认） | 实机状态 |
| --- | --- | --- |
| 失焦 / 打开 GUI 时释放鼠标 | `qol_release_mouse=1` | 已验证（GUI 开关、焦点丢失两条路径） |
| 小键盘 1–9 代替鼠标点 Baby 密码盘 | `qol_keypad_numpad=1` | 已验证（VK_NUMPAD5 → chain+1，pid 19316） |
| Private Room（帧 27）A/D/W/S 开关门 + S 翻监控面板 | `qol_private_hotkeys=1` | 已验证（门动画+Doorslam 音效 pid 22180；监控面板原生翻起/放下 pid 9908） |

## 1. 失焦解锁鼠标（qol_release_mouse）

### 1.1 问题

Fusion 版游戏通过 `SetCursorPos` 把光标强制拉回窗口中心，并用 `ShowCursor(FALSE)` 隐藏系统光标；即使游戏窗口不在前台，这个循环也可能继续，导致用户无法用鼠标点击其他窗口（例如本 DSH 会话）。

### 1.2 实现

在 `framework/native/d3d9hook.cpp` 中 hook 三个 user32 函数：

- `ClipCursor`：如果处于“应释放”状态，保存游戏传来的矩形并转发 `NULL`（解除裁剪）；恢复时重新 `ClipCursor(saved)`。
- `SetCursorPos`：释放状态下直接返回 TRUE（吞掉游戏的重定位）。
- `ShowCursor`：释放状态下只累计、不真正隐藏，并强制 `ShowCursor(TRUE)` 让系统光标可见；恢复后把计数还原并重新隐藏。

“应释放”判据：

```
qolReleaseMouse && (guiVisible || GetForegroundWindow() != 游戏顶层窗口)
```

状态切换时写日志（`logs/framework/slmodf-*.log`）：

```
qol| mouse released gui=%ld foreground=%d
qol| mouse captured again
```

安装日志：`qol| cursor hooks installed=3/3 release_mouse=1 ...`。

### 1.3 实测证据

- 打开 Insert GUI（`slmodf-20261026-160022-30136.log` 起）：`qol| mouse released gui=1 foreground=1`；关闭 GUI：`qol| mouse captured again`。
- Alt-Tab 到 DSH 窗口：`qol| mouse released gui=0 foreground=0`。
- 注意：Windows 前台窗口抢占保护可能阻止脚本把游戏重新切到前台（测试工具侧限制），但“打开/关闭 GUI”已覆盖释放与恢复两条对称路径，游戏内恢复抓取由同一 `QolUpdate()` 处理。

### 1.4 限制

- 只在本框架安装 hook 成功后生效；`hooks installed=0/3` 时功能静默失效（日志会写）。
- 若游戏在框架之外重新隐藏光标（例如切换分辨率/设备重建），下次状态切换会重新纠正，但可能有一帧闪烁。
- 该功能不改变游戏自身的鼠标灵敏度/锁定逻辑，只在“失焦或 GUI 打开”时让出鼠标。

## 2. 小键盘密码盘（qol_keypad_numpad）

### 2.1 背景

Night 5 的 Parts and Service 2（帧 19）要求连续输入 **10 位**随机数字：Baby 报数（`prompt` 1..9），玩家点击 3D 数字键。正确点击 `chain+1`、`death timer` 清零；错误点击或空按会加速死亡计时；每秒 `death timer+1`，到 3 触发 jumpscare。

### 2.2 实现

物理小键盘按键 **Numpad 1–9** 在游戏窗口内、GUI 未打开时被框架拦截：

1. 校验当前场景 == 19；
2. 实时读取 `Script Event == 607`；
3. `prompt` 在 1..9、`answer == 0`、`chain < 10`、`got you == 0`；
4. 把数字写入 `answer` 计数器（直接写对象 `+0x20A` 的 int16，取反存储），游戏自己的判定逻辑随即执行：正确 → beep+chain+1，错误 → buzz+death timer+1。

命中时写日志：

```
qol| keypad numpad digit=5 prompt=5 chain=0 vk=0x65
```

### 2.3 实测证据（pid 19316）

- 日志 `Workspace/logs/framework/slmodf-20261026-163321-19316.log` 第 223 行：`qol| keypad numpad digit=5 prompt=5 chain=0 vk=0x65`。
- 同 tick 读回：`chain=1`、`prompt=0`、`answer=0`，证明游戏接受了输入并推进链条。
- 测试脚本 `Workspace/tools/p8_keypad_test.py`（dev）用 `PostMessage(WM_KEYDOWN, VK_NUMPAD5)` 到游戏子窗口即可触发，不依赖真实键盘。

### 2.4 限制

- 仅帧 19 密码阶段生效；其他场景数字键原样传给游戏。
- 需要 **NumLock 开启**：NumLock 关闭时 Windows 把小键盘映射为方向键（VK_LEFT 等），框架收不到 VK_NUMPADx。
- 若同一 tick 玩家也点击了鼠标，可能出现 answer 竞争；守卫要求 `answer==0`，框架写入失败时不会破坏游戏状态（最多该键无效）。
- 只在 `Script Event==607` 时工作；直接内存改 Script Event 跳过音频/计时器链的测试方式会导致 prompt 不生成（这是测试方法问题，不是功能缺陷）。

## 3. Private Room 快捷键（qol_private_hotkeys）

### 3.1 背景

Night 5 Final Encounter（帧 27）原版只支持鼠标点击 3D 按钮，没有键盘快捷键。Custom Night（帧 31）有 A/D/W 开关左右门/通风口。QoL 在帧 27 注入同款键盘操作：

| 键 | 动作 |
| --- | --- |
| A | 左门开/关 |
| D | 右门开/关 |
| W | 通风口开/关 |
| S | 翻起/放下监控面板（原生 trigger 链：Active 8 动画 + Speaker #299/#300） |

### 3.2 实现

不发送合成鼠标点击（实测游戏不认 `SendInput`/`PostMessage`，疑似 DirectInput/RAW 输入），而是直接写游戏状态：

- 关门：`left/right/vent door` alterable[0]=2，`button/button 2/button 3` alterable[0]=1，`* is closed`=1，`cooldown`=20，`state::WriteAnimation(door, 12)` 强制关门动画，播放 Doorslam；
- 开门：对应 door alterable[0]=0，button=0，`* is closed`=0，`cooldown`=10，`state::WriteAnimation(door, 14)`，播放 Doorslam；
- S：走**原生 trigger 链**——把隐藏的 `trigger cursor` Active 移进 `flip trigger` 矩形并置 `trigger ready=1`，游戏自己的 Group63/64 会 Create `Active 8`、强制翻起/放下动画并播放 Speaker #299 `flipup2` / #300 `putdown`；不再直接写 `flip it[0]`，因此动画和音效都是原版的。

门动画的偏移来自 `RunObject.rAni.raAnimForced`：`headerObject` 之后是 `rCom`(120 B) + `rMvt`(158 B) + `rAni`，即对象基址 **+0x1CE**（已用 `hoOffsetValue=0x242` 交叉验证）；写入 `anim+1` 后游戏每 tick 的 `animIn` 会消费它并在动画结束时触发 cond #-2，关门/开门最终由原版事件补完 alterable 状态。

日志示例（门动画+音效）：

```
qol| anim force name=left door anim=12 on=0 frame=0 frames=1 prevForced=0
qol| door sfx path=...\Workspace\assets\sfx\Doorslam.wav found=1
winmm| PlaySoundA snd=... flags=0x00020003
qol| private left door -> closed
```

日志示例（监控面板）：

```
qol| private flip request state=0 target=2
audio| play ... match=150 handle=299 name="flipup2" how=3 scream=0
qol| private flip request state=2 target=0
audio| play ... match=151 handle=300 name="putdown" how=3 scream=0
```

### 3.3 实测证据（门：pid 22180，log slmodf-20261026-223202-22180.log）

- 按 A：日志 `qol| anim force name=left door anim=12 ...` + `winmm| PlaySoundA ... flags=0x00020003` + `qol| private left door -> closed`；内存采样 `forced=13, on=12, frames=10`，frame 1→9 播完（0.295 s）后 `forced=14, alt0=2`，即关门动画真实播放。
- 截图对比 `logs/probe/p8-fr27-door-lamp-compare.png`：左“开门：指示灯红”/右“关门：指示灯绿”。
- 再按 A：`anim=14` → `forced=15, on=14, frames=10` → 结束 `forced=1, on=0, alt0=0`，开门动画同样成立。

### 3.4 实测证据（监控面板：pid 9908，log slmodf-20261026-231055-9908.log）

- 23:11:57.402 `qol| private flip request state=0 target=2`，同毫秒 `audio| play ... handle=299 name="flipup2"`；截图 `logs/probe/p8-flip-native-9908.png`：画面已是 CAM01–CAM07 监控面板（面板升起），ThreatTelegraphs 同步显示「Ennard 在正面」。
- 23:12:29.398 `qol| private flip request state=2 target=0`，同毫秒 `audio| play ... handle=300 name="putdown"`；`flip it[0]` 回到 0。
- 说明：面板不是瞬间出现的静态切换，而是游戏自身 Group63/64 链（Create Active 8 + 强制动画 + Speaker）执行的结果；DWM 截图 1.5 s 端到端延迟大于动画时长，因此动画证据以内存帧序列 + 音效句柄为准。

### 3.4 限制

- 仅帧 27 生效；`got you != 0` 或 `hour == 6`（通关时刻）时不响应。
- 直接写 alterable 会跳过原版“门动画完成 → alterable=2”的过渡；实测游戏每 tick 的逻辑/指示灯会同步到新状态，但门体动画可能瞬移或短暂不连续，属于外观差异。
- 与攻击判定存在竞态：如果 Ennard 已在同一 tick 判定 `alterable[3]==99`，按键无法挽回；预警插件会在攻击前提示。
- GUI 打开时不响应（按键被 GUI 吞掉），避免误触。

## 4. 配置与安装

`Workspace/config/framework.ini`：

```ini
# --- Phase 7/8 QoL (v1.15+) ---
qol_release_mouse=1
qol_keypad_numpad=1
qol_private_hotkeys=1
qol_door_sfx=1
```

- 所有键默认开启，缺失时按默认值处理；损坏配置会备份并重建模板。
- 需要框架 DLL 已注入（用 `framework/bin/launcher32.exe` 启动游戏）。
- 关闭某项后立即生效，无需重启游戏（配置在启动时读取；QoL 状态每次事件检查 `g_fw` 字段）。

## 5. 已知问题 / 待办

1. 失焦恢复依赖 Windows 前台切换；若用户用“始终置顶”工具或远程桌面，判据可能抖动。
2. 密码盘输入目前只认物理小键盘；笔记本无小键盘时暂无替代键（可后续加 `ui` 按钮或数字行映射）。
3. Private Room 门动画/Doorslam 与监控面板翻起/放下均已走原版链路（raAnimForced / trigger cursor）；摄像头切换、屏幕上下选择仍需鼠标，后续可继续映射。
4. Doorslam 素材为部署时从本机游戏解包复制到 `Workspace/assets/sfx/`（见 assets/README），发布包不包含该素材，缺失时静音但功能不变。
5. 现有 dev/测试脚本已整理：`p8_keypad_test.py`、`p8_private_test.py` 保留在 `Workspace/tools/`；其余历史 `tmp_*` 探索脚本统一归档在 `Workspace/tools/dev-scratch/`（仅取证，不参与构建/发布），说明见 `logs/probe/README.md`。

## 6. 复现步骤（开发/验证）

1. `launcher32.exe --test-exit 600` 启动游戏；
2. 帧 19：`python Workspace/tools/p5_jump.py --pid <pid> 20`，进入密码阶段后按小键盘 1–9，观察 `qol| keypad` 日志；
3. 帧 27：`python Workspace/tools/p5_jump.py --pid <pid> 28`，按 A/D/W/S，检查 `left/right/top is closed`、`qol| anim force`、`qol| private flip` 与 `audio| play`（#298/#299/#300）；动画面可用 `python Workspace/tools/p8_door_anim_test.py --pid <pid> --key A` 采样 `raAnimForced/raAnimFrame`。
4. 失焦：让游戏在前台，Alt-Tab 到其他窗口，观察 `qol| mouse released` 与系统光标是否可用。

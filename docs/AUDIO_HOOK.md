# P6 音频 Hook 调查报告（SoftJumpscares 预研：识别 + 柔化）

- 状态：**P6a 识别、P6b 音量柔化与回滚、P6c 帧 27 识别、P6d 插件化 全部完成并实测**（2026-10-26 04:0x）；**P8 全局扩展到全部 scream 样本并三场景实测**（2026-10-26 16:2x）
- 适用构建：SL PC v1.121，EXE SHA-256 `c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73`，Fusion build 286
- 代码：`Workspace/framework/native/audio_hook.cpp`、`audio_hook.h`、`sound_table.h`（自动生成）
- 生成器：`Workspace/tools/gen_sound_table.py`；离线校验：`Workspace/tools/p6a_dump_match.py`、`p6a_dump_verify.py`

## 1. 结论摘要

1. 游戏音效**全部经由 DirectSound**（`mmfs2.dll` 导入 `DSOUND.dll!DirectSoundCreate`）；WINMM/MCI/waveOut 探针在整局运行中没有捕获到任何调用。
2. Fusion 运行时**从不把整段样本放进一个 DirectSound buffer**：它按固定的 **1.2 秒 chunk** 流式播放。chunk 字节数由格式决定：
   - 44100 Hz / mono / 16 bit → **105840** B
   - 48000 Hz / mono / 16 bit → **115200** B
   - 44100 Hz / stereo / 16 bit → **211680** B
   chunk #0 的 PCM 与样本文件 data 区**逐字节相同**（本报告 §5 用 dump 验证）。
3. 因此"整段样本哈希"必然匹配失败；改为**样本前缀指纹**（FNV-1a 64 over `min(bytes, chunkBytes)`）后，在真实 jumpscare 时刻成功识别：
   `match=89 handle=120 name="scream op5-7" how=3 chunk=0 scream=1`。
4. P6b 只对**被识别的 scream buffer** 调用真实 `IDirectSoundBuffer::SetVolume`，把音量衰减 `2000*log10(gain)` 厘贝（25% → **-1204**，约 -12 dB）；普通音效（如 `pop1`）不受影响；`audio_soften=0` 时 0 条 soften 记录（回滚验证）。
5. 该方案**不修改任何游戏文件**：hook 全在进程内运行时完成，框架/游戏退出即恢复；原版 EXE 与 Steam 校验不受影响。
6. **全局范围（v0.2.0）**：`scream=1` 的 7 个样本（86 `scream op5-8`、112 `op5-9`、120 `op5-7`、148 `op5-2`、257 `jump2`、265 `op3-2`、303 `op5-6`）全部在表内；插件使用**空白名单**时 `WhitelistAllowsLocked()` 放行所有 scream，因此“柔化整个游戏的 jumpscare”在代码路径上已覆盖，无需逐场景配置。帧 8 / 10 / 19 已实机验证，见 §5.7。

## 2. 关键发现：1.2 秒流式 chunk

- 早期探针把「整段样本 FNV」作为指纹，结果全部 `match=-1`。观察 buffer 长度只有 4 种（32768 primary、105840、115200、211680），且没有任何样本长度与它们相同 → 推翻"buffer = 完整样本"假设。
- 打开 probe dump 后确认：`dump-<pid>-012-105840-44100-1-16.bin` 的 105840 字节与 `Sounds/scream op5-7.wav` 的 data 前 105840 字节**完全相等**（`p6a_dump_verify.py` 输出 `eq_prefix True firstdiff None`）；`pop1.wav`（93092 B）也逐字节匹配其 chunk 前缀。
- 结论：chunk #k 对应样本 `[k*chunkBytes, +chunkLen)`；短样本的 chunk 尾部是填充/后续数据，但**前缀部分与样本一致**，所以用 `min(bytes, chunkBytes)` 长度做哈希即可匹配。
- 1.2s 公式：`chunkBytes = rate * channels * (bits/8) * 6 / 5`（44100·1·2·6/5 = 105840 ✓，48000·1·2·6/5 = 115200 ✓，44100·2·2·6/5 = 211680 ✓）。

## 3. Hook 链实现（`audio_hook.cpp`）

1. `audio_install_early()`（在 `d3d9hook::InstallEarly()` 之后、等 mmfs2 之前调用）：`LoadLibraryA("dsound.dll")` → MinHook `DirectSoundCreate`。
2. `DirectSoundCreate` detour 成功后：hook `IDirectSound::CreateSoundBuffer`（vtable[3]）；若对象 QI 到 `IDirectSound8` 且 vtable[3] 地址不同，再单独 hook。
3. 每次 `CreateSoundBuffer` 记录 buffer 元数据（bytes/rate/ch/bits/flags），拿到 buffer 后 hook 其 **Play（vt[12]）** 与 **SetVolume（vt[15]）**。
4. **多 vtable 变体**：primary buffer 与 secondary buffer 的 Play/SetVolume 实现地址不同（实测 `0x69293890` vs `0x69293990`），MinHook 一个 detour 无法区分 trampoline，因此每发现一个新目标地址就安装一个新的薄 detour 变体（Play 8 个、SetVolume 6 个，各自保存 original 指针）。日志：
   `audio| play hook #0 target=69293890 in_dsound=1 enable=MH_OK`
   `audio| play hook #1 target=69293990 in_dsound=1 enable=MH_OK`
5. WINMM 探针（`mciSendCommandA/W`、`PlaySoundA/W`、`waveOutWrite/Open/SetVolume`）全部安装成功（7/7 `MH_OK`），但**整局 0 次调用** → 音效不走系统 MCI/waveOut。

## 4. 样本指纹与匹配算法

表：`sound_table.h`（178 条有 PCM 的样本；214 条 ctfak 导出为裸 MP3，未纳入指纹）。

| 字段 | 含义 |
|---|---|
| `fp` | 整段 `min(bytes, 1 MiB)` 的 FNV-1a 64（旧方案，仅作兜底/日志） |
| `prefixFp` | 前 `min(bytes, chunkBytes)` 字节的 FNV-1a 64（chunk #0 指纹，主匹配键） |
| `chunkBytes` | 该格式的运行时 chunk 字节数（1.2 s） |
| `chunkFp[0..5]` / `chunkLen[0..5]` | **scream 专用**：样本各 chunk 的哈希与长度（chunk #k） |
| `scream` / `helper` | 是否 jumpscare scream / jumpscare 链辅助音 |

运行时匹配（`MatchStreamLocked`，每次 Play 重算，因为流式 buffer 会被复用/重填）：

1. 先算整段 `fp`（日志用）。
2. 收集 checkpoint：所有**同格式**且 `chunkBytes == buffer.bytes` 的样本前缀（长度 `min(bytes, bufferBytes)`）；scream 样本额外加入 chunk #1..#n 的哈希与长度。
3. Lock buffer 一次，按长度升序单遍增量 FNV，在 checkpoint 长度处取快照。
4. 比较：scream chunk 命中优先（`how=3`，`chunk=k`）；普通样本前缀唯一命中 → `how=3`；同数据多条目（如两个 `control room power down` 完全相同）→ `how=5` 标 ambiguous。
5. 无流式命中时回退旧的整段哈希 / 唯一 size+format 逻辑。

样本白名单（来自 `analysis/jumpscare-audio.md`）：scream = 86 `scream op5-8`、112 `op5-9`、120 `op5-7`、148 `op5-2`、257 `jump2`、265 `op3-2`、303 `op5-6`；helper = pop1、Doorslam、flipup2、putdown、Powerdownv2、good_job、Hand_Unit_Final_Encounter_VOb、Freddy02-10。

## 5. 实机证据（2026-10-26 02:42–04:0x）

流程：`launcher32.exe --test-exit 300` 注入 → 等标题页 → `python Workspace/tools/p5_jump.py --pid P 8 --wait 20` 跳到 Breaker Room（handle 8 → index 9）→ `python Workspace/tools/p6_trigger_scare.py --pid P --expect-frame "Breaker Room" --name Freddy --value 6` 写 `Freddy` 计数器触发**游戏原生**死亡链（G58→G59）→ 观察 `audio|` 日志。

### 5.1 P6a 识别（软化关闭，pid 19712，日志 `logs/framework/slmodf-20261026-024231-19712.log`）

```
02:42:42.479 audio| play #12 buf=064650B0 variant=1 bytes=105840 rate=44100 ch=1 bits=16
  fp=264C51E97B037792 match=89 handle=120 name="scream op5-7" how=3 chunk=0 scream=1 plays=1 zero=5
02:42:42.480 audio| play #13 ... match=12 handle=20 name="pop1" how=3 chunk=0 scream=0 ...
```

- `p6a_scream_test.ps1` 退出码 0（`found_scream=True`），`logs/probe/p6a-scream-report.txt` 记录了 trigger 前后全部 `audio|` 行。
- 触发前 0.1–1.5 s 内先出现 `pop1`（handle 20）等链内音效，符合帧 9 G59（scream #120 + pop1 #20）顺序。
- dump 交叉验证：`logs/audio/dump-19712-012-...bin` 与 `scream op5-7.wav` 前 105840 字节完全一致（`p6a_dump_verify.py`）。

### 5.2 P6b 柔化（`audio_soften=1`，`audio_gain_percent=25`，pid 24028，日志 `slmodf-20261026-024451-24028.log`）

```
02:45:02.644 audio| play #12 ... match=89 handle=120 name="scream op5-7" how=3 chunk=0 scream=1 ...
02:45:02.644 audio| soften buf=031CD310 handle=120 chunk=0 gain=25% off_cb=-1204 volume=0 -> -1204 (readback=-1204)
```

- 同一局中 `pop1`（handle 20）与 `Static2`（handle 223）等普通音效**没有** soften 行 → 只影响目标 scream。
- readback 用 `GetVolume` 复核，确认音量确实生效（不是只调用 API）。

### 5.3 回滚验证（`audio_soften=0`，pid 15276，日志 `slmodf-20261026-024656-15276.log`）

```
audio| probe installed dsound=69260000 ... table=178 screams=7 soften=0 gain=25%
02:47:07.352 audio| play #12 ... match=89 handle=120 name="scream op5-7" ... scream=1
（整局 audio| soften 行数 = 0）
```

识别仍工作、但**完全不改音量** → 关闭即回滚，等价原版音效。

### 5.4 离线回归

`cd Workspace/framework/bin && ./harness32.exe --wndproc-only|--imgui-only|--plugins` 三种模式全部 PASS（2026-10-26 02:49，`logs/framework/harness-20261026-024926.log`），音频改动不影响 overlay/插件/状态逻辑；P6b 插件化后 harness 重新链接 `audio_hook.o`，三模式再次 PASS（2026-10-26 03:55，`harness-20261026-035518.log`，`SUMMARY4 … vtx=2630`）。

### 5.5 P6c 帧 27（Night 5 Final Encounter）识别与柔化

流程：`tools/p6c_final_test.ps1 -Mode forced`（启动 → 等标题 → `p5_jump.py --pid P 28` 跳帧 27 → `p6_trigger_scare.py --expect-frame "Final Encounter" --name "got you" --value 1` → 统计 scream/soften）。pid 23104，日志 `slmodf-20261026-034828-23104.log`：

```
03:48:36.967 scene| fast idx=27 name="Final Encounter" ...
03:48:40.011 trigger ... obj=0x106BC1F0 num=31 got you: raw 65535 (value 0) -> raw 65534 (value 1)
03:48:40.020 audio| play #7 buf=116B6B90 ... match=154 handle=303 name="scream op5-6" how=3 chunk=0 scream=1
03:48:40.020 audio| soften buf=116B6B90 handle=303 chunk=0 gain=25% off_cb=-1204 volume=0 -> -1204 (readback=-1204)
```

- dump 交叉验证：`logs/audio/dump-23104-007-105840-44100-1-16.bin`（105840 B，scream op5-6 chunk #0）。
- 写 `got you=1` 后 1 秒内游戏走分支 B 切到帧 14（death），因此只记录到 chunk #0；帧 27 识别/柔化本身已实机验证。

### 5.6 P6d 插件化（SoftJumpscares 通过 ctx.audio 控制）

流程：`tools/p6d_plugin_test.ps1` 临时把 config 设为 `audio_probe=1, audio_soften=0, audio_gain_percent=100`（只允许插件开启柔化），再走帧 27 forced 用例。pid 20980，日志 `slmodf-20261026-035632-20980.log`、报告 `logs/probe/p6d-plugin-report.txt`：

```
plugin| loaded id=sl.softjumpscares ... callbacks load=1 update=1 draw=1 shutdown=1
audio| plugin whitelist count=0
audio| plugin set_soften=1 gain=25%
audio| play #7 ... name="scream op5-6" ... scream=1
audio| soften ... gain=25% off_cb=-1204 ... (readback=-1204)
```

- 证明 config `audio_soften=0` 时插件仍可通过 `ctx.audio.set_soften` 让帧 27 scream 柔化。
- GUI 截图 `logs/probe/p6e-plugin-ui.png`：插件列表出现 SoftJumpscares，自绘区显示开关/滑条（25）/Night5 白名单/统计 `探针: true | 柔化: true | 目标: 25%`（pid 28380）。

### 5.7 P8 全局验证（SoftJumpscares v0.2.0，插件空白名单）

前置：`config/framework.ini` 临时 `audio_probe=1`（识别探针），`audio_soften=0`（由插件开启）；`plugins/SoftJumpscares/main.lua` 的 `CONFIG.enabled=true`、`night5_only=false` → on_load 调 `ctx.audio.clear_names()`（空白名单 = 全部 scream）。

| 场景 | 触发方式 | 证据 |
|---|---|---|
| 帧 8 Ballora Gallery | warp 到帧 8，写 `jumpscare=1` 触发原生死亡 | `logs/framework/slmodf-20261026-161906-29632.log`：`match=83 handle=86 name="scream op5-8" scream=1` → `soften ... off_cb=-1204 ... readback=-1204` |
| 帧 10 Funtime Auditorium | warp 到帧 10，写 `got you=1` | 同日志：`match=94 handle=148 name="scream op5-2" scream=1` → soften -1204；随后 `Static2` 无 soften（scream=0，不受影响） |
| 帧 19 Parts and Service 2 | warp 到帧 19，制造死亡计时到 3（测试态） | `logs/framework/slmodf-20261026-162935-30284.log` 第 347–348 行：`name="scream op5-8" scream=1` → `soften ... volume=0 -> -1204 (readback=-1204)`；第 372 行 `setvol softened` 复核 |

结论：同一份 `SoftJumpscares` 代码在三个不同场景的 jumpscare 上均识别并衰减，证明“全局扩展”不是仅改文案；未覆盖的 `scream` 名称集合与 `sound_table.h` 完全一致（7/7）。插件默认仍为 `enabled=true`（柔化默认开启）但 **依赖 config `audio_probe=1`**；若用户不想要柔化，在 GUI 插件页取消勾选或在 `plugin.ini` 禁用插件。

## 6. 配置项（`Workspace/config/framework.ini`）

```ini
audio_probe=0          # 只读探针：记录 buffer 创建/播放/样本匹配（0/1，默认 0）
audio_soften=0         # 对已识别的 jumpscare scream 衰减音量（0/1，默认 0；依赖 audio_probe=1）
audio_gain_percent=25  # 目标增益百分比（1..100，默认 25 ≈ -12 dB）
```

- 配置模板与 `config/framework.ini.example` 已包含这三项；默认全部关闭，安装框架本身不改变任何音效。
- 采集期间可临时设 `audio_probe=1` 看日志；`audio_soften=1` 才真正衰减。
- 插件（P6d 起）：`audio_probe=1` 时 `ctx.audio` 可用，插件可调用 `set_soften/reset_soften/set_names/clear_names` 并读取 `last/stats`；详见 `docs/PLUGIN_API_V0.md` §4.1。

## 7. 已知限制与风险

1. **只柔化已识别的 scream**：识别依赖 FNV 指纹表，构建更新（EXE 指纹变化）后需重新生成 `sound_table.h`，否则不回落到猜测。
2. **只有 178/392 样本有指纹**：ctfak 把 214 个样本导出为裸 MP3（`ff fb` 帧头），未纳入匹配；这些音效会在 `audio|` 日志里显示 `match=-1`，不会被柔化。
3. **chunk #1+ 无独立日志**：Fusion 续流时不重新 `Play`，Play 钩子只能看到 chunk #0，因此现有日志没有 `chunk=1/2/3` 行；若引擎复用同一 buffer 续写，chunk #0 上设置的音量对该 buffer 后续内容同样生效（音量是 buffer 级属性），实际听感覆盖整段 scream；若未来引擎新建 buffer 再 Play，`chunkFp` 仍可识别（实现已就绪，尚无长样本实机证据）。帧 27 强制死亡链在 1 s 内切场景，只能验证到 chunk #0。
4. **音量语义**：-1204 厘贝 ≈ -12 dB，实际响度还受游戏内混音/系统音量影响；`audio_gain_percent` 可调 1..100，默认 25。
5. **多 vtable 上限**：Play 8 / SetVolume 6 个变体，若未来引擎新增第 9 种实现会漏 hook（日志有 `hook #n` 可诊断，超出会打印失败）。
6. **Dev 工具**：`p6_trigger_scare.py` 是开发者写内存工具（写 `Freddy` 计数器 int16 于 obj+0x20A），**不属于框架功能**、不随正常游玩使用；使用后游戏应重启。
7. 本方案不提供"资源替换"路径（每次启动都会把资源解包到 %TEMP%，改包不可行，见 `docs/RUNTIME_PROBE.md`）；替换音效需要运行时混音/注入替代样本，属于 P6 后续可选项。

## 8. 文件与产物清单

| 文件 | 说明 |
|---|---|
| `framework/native/audio_hook.cpp/.h` | DirectSound + WINMM 探针、流式 chunk 匹配、scream 柔化 |
| `framework/native/sound_table.h` | 自动生成的 178 条样本指纹表（无音频数据） |
| `tools/gen_sound_table.py` | 从本地 ctfak WAV dump 生成指纹表 |
| `tools/p6a_dump_match.py` | 把 probe dump 与样本 PCM 做子串匹配（离线取证） |
| `tools/p6a_dump_verify.py` | 校验 dump 前缀与样本 data 逐字节相等 |
| `tools/p6a_scream_test.ps1` | 一键：启动→跳 Breaker Room→触发 Freddy=6→统计 scream 匹配 |
| `tools/p6c_final_test.ps1` | 一键：启动→跳帧 27→强制 got you=1→统计 scream/soften（P6c） |
| `tools/p6d_plugin_test.ps1` | 一键：config 只开 probe→插件 SoftJumpscares 自行开启柔化→帧 27 验证（P6d） |
| `plugins/SoftJumpscares/` | P6d 插件样板：开关/增益/白名单/统计 UI，使用 `ctx.audio` |
| `tools/p6e_launch_gui.ps1`、`p6e_post_key.ps1` | GUI 取证辅助：启动并等标题页、PostMessage 按键/滚轮 |
| `tools/p6a_probe_test.ps1` | 只读 probe 测试（不写游戏内存） |
| `tools/p6_trigger_scare.py` | DEV ONLY：写计数器触发死亡链 |
| `logs/audio/dump-*.bin` | probe 抓到的原始 chunk（每条 ≤256 KiB，含 scream chunk 证据） |
| `logs/probe/p6a-scream-report.txt` | 一局完整 audio 日志摘录 |

## 9. 复现步骤

```powershell
# 1) 配置：audio_probe=1（识别），audio_soften=1 可选
notepad Workspace\config\framework.ini

# 2) 一键测试（自动启停游戏、跳帧、写 Freddy=6、统计 scream 匹配）
pwsh -NoProfile -ExecutionPolicy Bypass -File Workspace\tools\p6a_scream_test.ps1 -TestExit 300 -WatchSec 60

# 2b) 帧 27（Night 5）与插件化用例
pwsh -NoProfile -ExecutionPolicy Bypass -File Workspace\tools\p6c_final_test.ps1 -Mode forced -TestExit 240 -WatchSec 90
pwsh -NoProfile -ExecutionPolicy Bypass -File Workspace\tools\p6d_plugin_test.ps1 -TestExit 300 -WatchSec 75

# 3) 查看框架日志中的 audio 行
Select-String -Path Workspace\logs\framework\slmodf-*.log -Pattern 'audio\| (play|soften)' | Select-Object -Last 20

# 4) 离线复核 dump 与样本 PCM 一致
python Workspace\tools\p6a_dump_match.py
python Workspace\tools\p6a_dump_verify.py
```

## 10. 后续（P6 已完成项与未做项）

- ✅ 已完成：`plugins/SoftJumpscares/` 插件形态（框架 `ctx.audio` = 只读 match + 设置增益 + 白名单，插件负责开关/增益/策略/UI）；帧 27 `scream op5-6`（handle 303）实机识别与柔化；P6d 插件用例；P6e GUI 截图；P8 全局验证（帧 8/10/19 + `night5_only=false` + 空白名单）。
- 未做（评估后暂缓）：
  1. chunk #1+ 长样本实机证据：需要找一条不切场景的 scream 播放（当前死亡链都会 1 s 内切帧），或 Hook Lock/Unlock 验证续流；优先级低于 P5/P7。
  2. 整体音量倍率、高频衰减（低通）——需要自定义混音，风险高，先不做。
  3. 资源替换（P6 原定可选项）在"每次启动解包到 %TEMP%"前提下不可行；若未来要做，需拦截样本加载/替换 buffer 内容，属于独立课题。

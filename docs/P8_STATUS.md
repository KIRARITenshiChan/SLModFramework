# P8.0 — 现状盘点与发布范围清单（【v1.20】2026-10-27）

> 本文件是 Phase 8 / P8.0 的交付物：记录首发（v1.0.0）包含什么、每项功能的验证状态与限制、版本标识一致性检查。
> 结论：**首发范围已冻结，P8.1–P8.5 完成**；稳定性四项、发行包、许可/文档、干净安装→卸载→原版对照均有证据（见 `docs/P8_STABILITY.md`、`docs/P8_ACCEPTANCE_DRILL.md`）。

## 1. 版本与构建标识（一致性检查）

| 项目 | 值 | 位置/证据 |
|---|---|---|

| 框架版本 | **1.0.0** | `framework/native/common.h:9` `#define SLMODF_VERSION "1.0.0"`（P8.6 冻结） |
| 插件 API | **1** | `common.h:14` `#define SLMODF_PLUGIN_API_VERSION 1` |
| init 标记 | `version=1.0.0` | 干净安装 `SLModFramework/logs/framework/init-30792.done` |
| DLL 日志 | `version=1.0.0`（启动行） | `slmodf-20261027-065308-30792.log` |
| GUI 标题 | `SLModFramework 1.0.0` | 截图 `logs/probe/p8-v1.0.0-gui.png`、`p85-final-gui-top.png` |
| launcher 版本 | **1.0.0** | `loader_version=` in `launch-30792.json` |
| 构建产物 | DLL 1,617,920 B / launcher 31,744 B / harness 1,517,056 B | 2026-10-27 06:55 `build.sh` + `tests/build_harness.sh`（P8.5 后，连续两次一致） |
| 发行包 | ZIP `f8932e2513efbb70ae662d67ae51e21c62f84afc75bcfcb5a27b90980acc7859`（805,520 B）；DLL `fd83f195f220e584712dd89be2b7b177d2c596c737cba40b9b43f59f4e420b1e`；launcher `22daac7fe3a197e7b0bd29395274361eefa02f96def7dd42df05ac3fc02b85ea` | `tools/make_release.sh 1.0.0`；见 `docs/P8_ACCEPTANCE_DRILL.md` |
| 离线回归 | `--wndproc-only/--imgui-only/--plugins/--p8-only` 全部 rc=0 | 2026-10-27 06:56（`harness-20261027-065639/065640.log`） |
| 安装布局 | **布局 B** | `<game>/SLModFramework/{bin,config,plugins,logs,assets,docs}`，详见 `docs/INSTALL_LAYOUT.md` |
| 真机冒烟 | 通过 | 2026-10-27 06:53 干净安装（pid 30792）：launch/init/plugin/日志路径正确；另见 `docs/P8_ACCEPTANCE_DRILL.md` |

结论：版本标识一致（源码、init、DLL 日志、GUI、launcher 全为 1.0.0），无遗留 `0.7.0-p8` 硬编码。

## 2. 功能 × 证据状态表

| # | 功能 | 阶段 | 状态 | 证据 | 限制/备注 |
|---|---|---|---|---|---|
| 1 | EXE 指纹门禁（SHA-256 + 尺寸） | P2 | 已实现/已实机 | 正例 c60e1c79…；负例 notepad exit 3 不启动 | 游戏更新后必然拒绝，见 KI-04 |
| 2 | 挂起启动 + 远程 LoadLibraryW 注入 | P2 | 已实现/已实机 | HMODULE、init 标记、心跳 | 仅 32 位 |
| 3 | 等 mmfs2.dll + PAMU/build 286 校验 | P2/P3 | 已实现/已实机 | `pamu| scan ok`、init `detail=build286+pamu` | 扫描期间启动延迟（可接受） |
| 4 | D3D9 Hook（Create9→CreateDevice→Present/Reset） | P3 | 已实现/已实机 | device vtable present/reset、Present 计数、Reset 恢复 | 锁屏不可用 KI-02 |
| 5 | ImGui GUI + CJK 全字库 + DPI 缩放 | P3/P7 | 已实现/已实机 | 截图 `p8-layoutB-gui.png`、glyph_oxygen=1 | 字体依赖系统 CJK 字体 |
| 6 | 输入屏蔽/放行 + 热键自动重复防护 | P3 | 已实现/已实机 | swallow_k/swallow_m、hotkey_repeat=1/1/0 | – |
| 7 | 插件宿主 + Lua 5.4 沙箱 + API v1 生命周期 | P4 | 已实现/已实机 | HelloPlugin loaded；6 夹具 SUMMARY4 | 同名计数器只留最后一个（KI-18） |
| 8 | 插件 GUI 启停写回 plugin.ini + 重载 | P4 | 已实现/已实机 | pid 3588/23104/20904/19188 | – |
| 9 | 配置系统（config_version/默认模板/损坏恢复） | P4 | 已实现/已实机 | `config| …version=1 …known=16`；.bad-* 恢复 | 配置重置入口未做（P8.2） |
| 10 | 场景识别（帧号/名字/尺寸/快速路径） | P5 | 已实现/已实机 | 帧 1/8/9/10/11/13/19/27/31；延迟 1.7–3.2 s | 帧内存复用靠 rh 比较 |
| 11 | 对象/计数器读取 + DebugInspector + 实时值 | P5 | 已实现/已实机 | 标题 option 1→2→3、帧 27/31 计数器 | 全表构建 230–360 ms/350–780 MB 仅切帧 |
| 12 | 状态录制 JSONL（record） | P5 | 已实现/已实机 | `logs/state/state-*.jsonl` | 默认关；样本可增长（P8.2 轮转） |
| 13 | ctx.game 只读状态（含 object_rect/alterable/live 回退） | P5/P7 | 已实现/已实机 | HelloPlugin 读到 scene/counters；Bonnet `Active 19` 回退 | 插件拿不到原始地址 |
| 14 | 存档辅助 + 场景跳转（dev） | P5 | 已实现/已实机 | `tools/save_night.py`、F9 菜单、warp 到帧 9/27 | 仅开发用途，不进发行功能 |
| 15 | DirectSound 音频探针 + 样本指纹（178 条） | P6 | 已实现/已实机 | scream 86/112/120/148/257/265/303 识别 | 只认 chunk#0；MP3 样本无法指纹（KI-11） |
| 16 | SoftJumpscares 实例（插件） | P6 | 已实现/已实机 | `audio| soften … off_cb=-1204` | 示例插件，不随 1.0.0（KI-14） |
| 17 | ThreatTelegraphs 示例（8 场景矩阵 + 帧 31 精确模型） | P7 | 已实现/部分实机 | 帧 8/9/10/11/19/27/31 实机；离线 64 断言 | 多局误报率未测（KI-15）；不随 1.0.0 |
| 18 | QoL：失焦/开 GUI 释放鼠标 | P8-QoL | 已实现/已实机 | `qol| mouse released gui=1`；Alt-Tab 回归 | hook 未装成会静默失效（KI-09） |
| 19 | QoL：密码盘小键盘 | P8-QoL | 已实现/已实机 | `chain 0→1`，pid 19316 | 需 NumLock + 特定状态（KI-08） |
| 20 | QoL：Private Room A/D/W/S + 门动画/音效 | P8-QoL | 已实现/已实机 | 动画 forced 12→13→14；flip 原生链 #299/#300 | 只覆盖帧 27；帧 31 无快捷键（KI-06） |
| 21 | QoL：Doorslam 本机音效 | P8-QoL | 已实现/已实机 | `winmm| PlaySoundA …` | 素材不随包，缺失静音（KI-10） |
| 22 | 安装布局 B + deploy.sh + 路径推导 | P8 | 已实现/已实机 | 2026-10-27 05:35/05:49 冒烟；`docs/INSTALL_LAYOUT.md` | 日志路径显示 ..\..（KI-05） |
| 23 | 离线 harness 四模式 | P3–P8 | 已实现/已离线 | wndproc/imgui/plugins/p8-only 全 PASS | 不覆盖真实设备（锁屏时 SKIP） |
| 24 | 日志轮转（默认 4 MiB×5，env 覆盖阈值） | P8.2 | 已实现/已离线+实机 | `SUMMARY5 rotated=1`；实机 `[log] rotated (threshold 4096 bytes, 5 backups)` | 见 `docs/P8_STABILITY.md` |
| 25 | 诊断包（summary+日志+配置+sidecar → zip） | P8.2 | 已实现/已离线 | `--p8-only` diag >200 B；`diagnostics-*.zip` 约 75 KB | 依赖系统 `tar.exe`，无则回退目录 |
| 26 | 配置重置（备份+默认模板+重载） | P8.2 | 已实现/已实机 | 「恢复默认配置」→ `config| reset requested ... ok=1` + `framework.ini.bak-<ts>`；pid 9492 实测 | 见 `docs/P8_ACCEPTANCE_DRILL.md` §4 |
| 27 | 插件连续错误自动禁用（30 次） | P8.2 | 已实现/已实机 | `AUTO-DISABLED ... after 30 consecutive errors; enabled=0 written to manifest` | 仅运行时错误；加载期错误不自动写 disabled |
| 28 | GUI 主窗口高度自适应 + 滚动 | P8.5 | 已实现/已实机 | 1440@150% 下窗口限高可滚动，底部「框架/插件」与诊断按钮可达；截图 `logs/probe/p85-final-gui-top.png` / `p85-final-gui-bottom.png` | 最小 520×260；小屏仍可滚 |

## 3. 内置 QoL 配置（默认值与生效范围）

| 配置 | 默认 | 生效场景 | 说明 |
|---|---|---|---|
| `qol_release_mouse=1` | 开 | 失焦或 GUI 打开时 | ClipCursor/SetCursorPos/ShowCursor 三个 hook 全装成才生效 |
| `qol_keypad_numpad=1` | 开 | 帧 19 密码盘 | 需 Script Event=607、prompt 1..9、answer=0、chain<10、got you=0、NumLock 开 |
| `qol_private_hotkeys=1` | 开 | 帧 27 Private Room | A/D/W 门 + S 面板；跳过动画（由原生 trigger/动画强制补上）；hour==6 静默 |
| `qol_door_sfx=1` | 开 | 帧 27 注入开关门时 | 播放 `assets/sfx/Doorslam.wav`；缺失则静音，不影响功能 |

## 4. 示例插件（冻结，不随 v1.0.0 发布）

| 插件 | 版本 | 默认 | 状态 | 说明 |
|---|---|---|---|---|
| HelloPlugin | 0.1.0 | enabled=1 | 随包示例 | 最小生命周期 + 中文 UI + ctx.game 读取 |
| SoftJumpscares | 0.2.0 | enabled=1（本机） | 示例，不随包 | 空白名单=全部 7 个 scream；实机柔化证据 P6 |
| ThreatTelegraphs | 0.5.1 | enabled=0 | 示例，不随包 | 8 场景矩阵；帧 31 Freddy/Bonnet/字幕试点；离线 64 断言 |

## 5. 首发范围冻结确认（v1.0.0）

**包含**：launcher + DLL、GUI、插件宿主与 API v1、Lua 沙箱、配置系统、日志、场景/对象/计数器只读状态、内置 QoL 四项、HelloPlugin、LICENSE/THIRD_PARTY_NOTICES、中文为主+英文摘要的 README/INSTALL/UNINSTALL/TROUBLESHOOTING/CHANGELOG/PLUGIN_API。

**不包含**：SoftJumpscares、ThreatTelegraphs、Phase 9 字幕系统、外部音频替换、多游戏/未验证版本、非注入 HUD-only 模式、内存 dump/游戏派生素材。

## 6. P8.1–P8.6 状态

1. **P8.1**：✅ 干净构建/可复现/objdump 证据/发行 ZIP 已完成（`docs/P8_BUILD_EVIDENCE.md`；ZIP SHA-256 `f8932e25...`）。
2. **P8.2**：✅ 轮转/诊断包/配置重置/自动禁用（`docs/P8_STABILITY.md`）；GUI 点击验证在 P8.5 完成。
3. **P8.3**：✅ `LICENSE`（MIT）、`THIRD_PARTY_NOTICES`、素材审计 `docs/P8_ASSET_AUDIT.md`。
4. **P8.4**：✅ 中文为主+英文摘要文档 6 份（`docs/release/`）。
5. **P8.5**：✅ 干净安装→使用→卸载→原版对照→复装，证据 `docs/P8_ACCEPTANCE_DRILL.md`。
6. **P8.6**：进行中——剩余 tag v1.0.0 与 GitHub Release（身份 KIRARITenshiChan）；版本号已冻结 1.0.0。

## 7. 证据索引

- P8.5 演练：`docs/P8_ACCEPTANCE_DRILL.md`；截图 `logs/probe/p85-final-gui-top.png`、`p85-final-gui-bottom.png`、`p85-original-uninstalled.png`
- 发行包：`release/SLModFramework-1.0.0-win-x86.zip`（805,520 B / `f8932e25...`）、`release/BUILD_MANIFEST.sha256`（18 files）、`release/SHA256SUMS`
- 干净安装冒烟：`SLModFramework/logs/framework/launch-30792.json`、`init-30792.done`、`slmodf-20261027-065308-30792.log`
- 布局 B 冒烟：`logs/probe/p8-layoutB-smoke.md`、截图 `logs/probe/p8-layoutB-gui.png`
- P8.2 稳定性证据：`docs/P8_STABILITY.md`（轮转/诊断包/自动禁用日志与截图索引）
- 计划：`SL-Mod-Framework-Plan.md` Phase 8 节；安装布局说明：`docs/INSTALL_LAYOUT.md`；已知问题：`docs/P8_KNOWN_ISSUES.md`

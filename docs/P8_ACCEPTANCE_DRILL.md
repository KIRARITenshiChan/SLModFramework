# P8.5 — 干净安装、使用与卸载演练记录（【v1.21】2026-10-27 06:45–06:57）

> Phase 8 / P8.5 交付物。目标：只用最终发行 ZIP 在“接近玩家”的干净环境完成安装→使用→配置重置→诊断导出→卸载→原版对照→复装，并记录证据。
> 执行人：lead；环境：本机 Windows 10.0.26100 x64 / 游戏 v1.121 / 2560×1440 @150% DPI。

## 1. 被测发行包

| 项目 | 值 |
|---|---|
| 文件 | Workspace/release/SLModFramework-1.0.0-win-x86.zip |
| 大小 / SHA-256 | 805,520 B / f8932e2513efbb70ae662d67ae51e21c62f84afc75bcfcb5a27b90980acc7859 |
| 包内文件数 | 18（release/BUILD_MANIFEST.sha256） |
| 包内二进制 | launcher32.exe 22daac7fe3a197e7b0bd29395274361eefa02f96def7dd42df05ac3fc02b85ea；SLModFramework32.dll fd83f195f220e584712dd89be2b7b177d2c596c737cba40b9b43f59f4e420b1e |
| 布局 | 布局 B：ZIP 顶层目录 SLModFramework/，直接解压到游戏根即得 <game>/SLModFramework/{bin,config,docs,plugins,...} |

测试前把开发期安装目录整体移到 Workspace/backups/install-pre-P8.5-20261027/，确保游戏根不存在旧框架文件；游戏存档为原版 [sl] intro=1 current=1。

## 2. 干净安装与首次启动

1. 把发行 ZIP 解压到游戏根，得到：bin/{launcher32.exe,SLModFramework32.dll}、config/framework.ini.example、plugins/HelloPlugin/{plugin.ini,main.lua}、docs/、LICENSE、README.md、THIRD_PARTY_NOTICES.md；无 logs/、无 assets/。
2. 启动：<game>/SLModFramework/bin/launcher32.exe --toggle-gui 5
   实测 launcher pid 33524 / 游戏 pid 30792（另一次 pid 24504 为卸载后的原版对照）。
3. 首次运行自动创建：
   - config/framework.ini（config_version=1）；
   - logs/framework/launch-30792.json、init-30792.done、loader-20261027-065304.log、slmodf-20261027-065308-30792.log。
4. launcher 日志关键行：
   - framework=E:/.../SLModFramework
   - game_dir=E:/.../Five Nights at Freddy's Sister Location
5. DLL 日志关键行：
   - framework=E:/.../SLModFramework
   - config| not found (.../config/framework.ini), using defaults: hotkey=Insert hud=0
   - config| wrote default config ... (config_version=1)
   - plugin| manifest dir=.../plugins/HelloPlugin id=example.hello name=Hello Plugin version=0.1.0 api=1 enabled=1
   - plugin| loaded id=example.hello ... callbacks load=1 update=1 draw=1 overlay=0 shutdown=1
   - plugin| [info] HelloPlugin loaded; framework=1.0.0 api=1
6. GUI 实测（截图 logs/probe/p85-final-gui-top.png）：标题 SLModFramework 1.0.0、场景 [1] title screen、Present 725 / FPS 60.0 / DPI 1.500、HelloPlugin 已加载 upd=1606 err=0。
   结论：发行包在无开发文件、无预生成配置的目录中可独立安装并正确加载。

## 3. GUI 高度/滚动修复（P8.5 发现并修复）

- **问题**：1440@150% 下主窗口内容高于屏幕，底部「框架/插件」与诊断按钮不可见，且当时无滚动条，玩家无法触达。
- **修复**（framework/native/overlay.cpp DrawMainWindow()）：按显示高度限制窗口最大高度，并允许滚动——
  maxH = (io.DisplaySize.y > 200*s) ? io.DisplaySize.y - 60*s : 720*s；
  SetNextWindowSizeConstraints(ImVec2(520*s,260*s), ImVec2(760*s,maxH))；
  SetNextWindowSize(ImVec2(640*s,maxH), ImGuiCond_FirstUseEver)。
  版本号不变（仍 1.0.0），修复后重建 DLL fd83f195...。
- **验证**：真实滚轮（mouse_event WHEEL）在面板内滚动 12 格后，底部「框架/插件」段、导出诊断包/恢复默认配置按钮、配置/日志路径、关闭提示全部可见（截图 logs/probe/p85-final-gui-bottom.png）；面板高度不再超出屏幕。
- 备注：本机 pwsh 进程 DPI-unaware，自动化点击/移动需用逻辑坐标（物理 = 逻辑 × 1.5）；这解释了早期合成点击落在 y=1439 的现象。

## 4. 配置重置（GUI 按钮）

在上一轮干净安装（pid 9492）实测点击「恢复默认配置」（物理坐标 204,1293）：

config| reset requested: backup=.../config/framework.ini.bak-20261027-065129 ok=1
config| wrote default config .../config/framework.ini (config_version=1)
config| reset applied: hotkey=VK 0x2D hud=0 qol(mouse=1 keypad=1 private=1 sfx=1) audio(probe=0 soften=0 gain=25)

config/ 目录出现 framework.ini.bak-20261027-065129，活动配置重写为默认模板。

## 5. 诊断包导出（GUI 按钮）

同一轮安装实测点击「导出诊断包」（物理坐标 111,1293）：

input| imgui click at 111.0,1293.0 display=2560.0x1440.0 gui=1
diag| saved .../SLModFramework/logs/framework/diagnostics-20261027-065052.zip (tar wait=0)
ui| diagnostics export -> .../diagnostics-20261027-065052.zip

离线 --p8-only 也验证同一 API（诊断包 >200 B，见 docs/P8_STABILITY.md）。

## 6. 卸载与原版对照

1. 停止游戏/launcher 进程，把 <game>/SLModFramework 整体移出游戏根（保留到 Workspace/backups/install-P8.5-clean-20261027/ 作为日志证据）。
2. 直接运行原版 SisterLocation.exe（pid 24504）：
   - 窗口标题 Sister Location，标题菜单正常；
   - 进程模块列表中**没有** SLModFramework32.dll；
   - 游戏根未重新生成 SLModFramework/，无框架日志；
   - 截图 logs/probe/p85-original-uninstalled.png（纯原版画面，无 HUD/GUI）。
3. 结论：卸载 = 删除 SLModFramework 目录即可；游戏根无框架写入文件；Steam 文件验证不会改动原版。

## 7. 用最终修复后 ZIP 复装

1. bash tools/make_release.sh 1.0.0 重新生成发行包（包含 GUI 修复后的 DLL）：ZIP 805,520 B，SHA-256 f8932e2513...；release/SHA256SUMS 与 BUILD_MANIFEST.sha256（18 files）同步更新。
2. 解压到游戏根 → 安装目录 bin 哈希与清单一致（launcher 22daac7f...、DLL fd83f195...）。
3. 再次启动冒烟：GUI 标题 1.0.0、config/logs 自动生成、HelloPlugin loaded、滚轮可到面板底部（截图 p85-final-gui-top.png / p85-final-gui-bottom.png）；退出后无残留进程。
4. 结论：最终发行 ZIP 与证据一致，安装/使用/卸载闭环可用。

## 8. 构建与离线回归（本阶段）

- 2026-10-27 06:55 重建（含 P8.5 GUI 修复）：
  - bin/SLModFramework32.dll 1,617,920 B / fd83f195f220e584712dd89be2b7b177d2c596c737cba40b9b43f59f4e420b1e
  - bin/launcher32.exe 31,744 B / 22daac7fe3a197e7b0bd29395274361eefa02f96def7dd42df05ac3fc02b85ea
  - bin/harness32.exe 1,517,056 B / 248ab25b34fde79a047d85718a6fe89b3c57eb66aa8fe4ca8f8627ec558cb24d
- 连续两次构建 + harness 重链接哈希完全一致（REPRODUCIBLE）。
- 离线四模式全 PASS：--wndproc-only、--imgui-only、--plugins、--p8-only（日志 logs/framework/harness-20261027-065639/065640.log；诊断包 diagnostics-20261027-065640.zip）。
- 备注：P8.2 时 harness 哈希为 dccdec7b...；P8.5 overlay 修复后重新链接变为 248ab25b...（发行包不含 harness，不影响包哈希）。

## 9. 验收结论

| # | P8.5 流程 | 结果 |
|---|---|---|
| 1 | 只从最终发行 ZIP 解压安装 | ✅ |
| 2 | 启动器识别游戏版本并注入 | ✅ loader fingerprint OK、init ok |
| 3 | init/版本/路径日志正确 | ✅ framework/game_dir/exe 全部正确 |
| 4 | GUI 呼出/关闭、鼠标释放 | ✅ （GUI 截图与 qol 日志；输入焦点见 P3/P7 证据） |
| 5 | HelloPlugin 加载与重启保持 | ✅ loaded=1；启停持久化见 P4 证据 |
| 6 | 内置 QoL 开关与素材缺失行为 | ⚠ 本轮未触发 scene27；代码路径见 P7/P8-QoL，缺失素材静音不报错 |
| 7 | 错误插件/损坏配置恢复 | ✅ P8.2 实测（自动禁用 + .bad-* 恢复） |
| 8 | 日志足以说明初始化结果 | ✅ |
| 9 | 按文档卸载、游戏根无残留 | ✅ |
| 10 | 无框架重启作为原版对照 | ✅ 无 SLMod 模块、无 HUD |

未覆盖/后续：Alt+Tab、全屏/分辨率切换、反复启退的长回归（KI-20，标记未验证）；QoL Doorslam 缺失静音未在本轮重复触发；用户侧杀软提示仅本机 Defender 结论。

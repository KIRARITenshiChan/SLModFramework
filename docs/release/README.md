# SLModFramework

> 《Five Nights at Freddy's: Sister Location》(Steam PC, v1.121) 的本地运行时 Mod 框架：把 ImGui 控制面板、插件宿主和一组体验改进（QoL）注入游戏进程，不修改游戏文件、不替换资源。

**当前版本：v1.0.0** ｜ 插件 API v1 ｜ Windows 10/11 x64（游戏本体为 32 位）
**English summary**: A local runtime mod framework for the Steam PC build of *FNAF: Sister Location*. It injects a DLL into the game process (no file patching), provides an ImGui overlay (Insert), a Lua 5.4 plugin host (API v1) and optional quality-of-life features. MIT-licensed source; game assets are not included.

## 功能

- **游戏内 GUI**：按 `Insert` 打开/关闭；显示 EXE 指纹、D3D9 设备、Present/FPS、场景、对象与计数器；支持鼠标操作、中文界面。
- **插件系统**：`plugins/<目录>/{plugin.ini, main.lua}`，Lua 5.4 沙箱 + 受限 API；GUI 内可启用/禁用（写回清单并热重载）。
- **只读游戏状态**：场景 ID/名称/尺寸、帧号/FPS、对象表、计数器、部分 alterable 字段；失败不会写入游戏内存。
- **QoL（默认开启，可配置关闭）**：失焦或打开 GUI 时释放鼠标；密码盘支持小键盘；Private Room（帧 27）A/D/W 门与 S 面板快捷键（含门动画/音效）；可自定义门音效。
- **稳定与排障**：日志轮转（4 MiB × 5）、一键导出诊断包、恢复默认配置、插件连续错误自动禁用。
- **不修改游戏文件**：资源全程在内存中 Hook/读取；卸载=删除安装目录。

## 快速开始（3 步）

1. 把发行 ZIP 解压到游戏根目录下，得到：
   `<Steam>\steamapps\common\Five Nights at Freddy's Sister Location\SLModFramework\`
2. 双击 `SLModFramework\bin\launcher32.exe` 启动游戏（不要直接双击游戏 exe，否则不会加载框架）。
3. 进入游戏后按 `Insert` 打开面板；配置与日志在 `SLModFramework\config`、`SLModFramework\logs`。

详细安装/卸载/排障见 `docs/INSTALL.md`、`docs/UNINSTALL.md`、`docs/TROUBLESHOOTING.md`；插件开发见 `docs/PLUGIN_API.md`。

## 系统要求

- Windows 10 1803+ / Windows 11（需要系统自带 `tar.exe` 用于诊断包；缺失时回退为文件夹）。
- Steam 版《FNAF: Sister Location》**v1.121**，`SisterLocation.exe` SHA-256 为
  `c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73`（950,556,728 字节）。
- 游戏为 32 位（x86）程序；框架同为 32 位，无额外运行库依赖（无需 VC++ Redist）。

## 安全与边界

- 仅本地单机使用；不含联网、遥测、自动更新。
- 只对**已知构建**生效：EXE 指纹不匹配时启动器会拒绝启动（退出码 3），不会静默降级。
- 游戏更新后需等待框架适配；请不要用旧版强行加载新 EXE。
- 不含任何游戏素材；日志/诊断包可能含本机路径与用户名，分享前请自行检查。

## 已知限制

- 无「非注入 / HUD-only」模式（必须由启动器注入）。
- 锁屏/安全桌面下无法创建 D3D9 设备，GUI 不显示（解锁后正常）。
- 音频识别只覆盖部分样本；字幕/音频系统不在 v1.0.0 范围。
- 示例插件 `SoftJumpscares`、`ThreatTelegraphs` 不在 v1.0.0 发行包内，后续独立发布。

## 许可证

项目本体 MIT，见 `LICENSE`；第三方组件（Dear ImGui、MinHook、Lua、MinGW 运行库）声明见 `docs/THIRD_PARTY_NOTICES.md`。游戏及其派生素材版权归原权利人，本项目不分发。

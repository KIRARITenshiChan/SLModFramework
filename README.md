# SLModFramework

> 《Five Nights at Freddy's: Sister Location》(Steam PC, v1.121) 的本地运行时 Mod 框架：把 ImGui 控制面板、插件宿主和一组体验改进（QoL）注入游戏进程，不修改游戏文件、不替换资源。

**当前版本：v1.0.0** ｜ 插件 API v1 ｜ 项目许可证：MIT
**English summary**: A local runtime mod framework for the Steam PC build of *FNAF: Sister Location*. It injects a DLL into the game process (no file patching), provides an ImGui overlay (Insert), a Lua 5.4 plugin host (API v1) and optional quality-of-life features. MIT-licensed source; game assets are not included.

- 发行包下载：本仓库的 [Releases](../../releases)（`SLModFramework-1.0.0-win-x86.zip`）
- 安装/卸载/排障：`docs/release/INSTALL.md`、`docs/release/UNINSTALL.md`、`docs/release/TROUBLESHOOTING.md`
- 插件开发：`docs/release/PLUGIN_API.md`
- 项目计划与路线图：`SL-Mod-Framework-Plan.md`

## 功能

- **游戏内 GUI**：按 `Insert` 打开/关闭；显示 EXE 指纹、D3D9 设备、Present/FPS、场景、对象与计数器；支持鼠标操作与中文界面。
- **插件系统**：`plugins/<目录>/{plugin.ini, main.lua}`，Lua 5.4 沙箱 + 受限 API；GUI 内可启用/禁用（写回清单并热重载）；连续 30 次运行时错误自动禁用。
- **只读游戏状态**：场景 ID/名称/尺寸、帧号/FPS、对象表、计数器、部分 alterable；默认只读，失败不会写入游戏内存。
- **QoL（默认开启，可配置关闭）**：失焦或打开 GUI 时释放鼠标；密码盘支持小键盘；Private Room（帧 27）A/D/W 门与 S 面板快捷键（含门动画/音效）；可自定义门音效。
- **稳定与排障**：日志轮转（4 MiB × 5）、一键导出诊断包、恢复默认配置。
- **不修改游戏文件**：资源全程在内存中 Hook/读取；卸载 = 删除安装目录。

## 快速开始（3 步）

1. 把发行 ZIP 解压到游戏根目录，得到：
   `<Steam>\\steamapps\\common\\Five Nights at Freddy's Sister Location\\SLModFramework\\`
2. 双击 `SLModFramework\\bin\\launcher32.exe` 启动游戏（不要直接双击游戏 exe，否则不会加载框架）。
3. 进入游戏后按 `Insert` 打开面板；配置与日志在 `SLModFramework\\config`、`SLModFramework\\logs`。

## 从源码构建

依赖：Windows x64、[winlibs i686 MinGW-w64](https://winlibs.com/)（32 位工具链，本仓库以 gcc 16.2.0 验证）、Git Bash 或 MSYS2 bash。第三方源码（Dear ImGui / MinHook / Lua 5.4.7）已放在 `third_party/`，无需另行下载。

```bash
# 1) 构建 32 位 DLL + 启动器（输出到 framework/bin）
cd framework
bash build.sh
# 2) 构建离线回归 harness（可选）
bash tests/build_harness.sh
# 3) 部署到游戏安装目录（<game>\SLModFramework），可选带文档/音效
bash deploy.sh --with-docs
```

构建为确定性（byte-reproducible）：链接参数含 `-Wl,--no-insert-timestamp`，源码不含 `__DATE__/__TIME__`。

## 目录结构

| 路径 | 说明 |
|---|---|
| `framework/` | 32 位注入启动器 + 框架 DLL 源码、构建/部署脚本、离线 harness |
| `third_party/` | 构建所需第三方源码（ImGui MIT、MinHook BSD-2、Lua 5.4.7 MIT） |
| `plugins/HelloPlugin/` | 最小示例插件（随发行包） |
| `config/framework.ini.example` | 配置模板 |
| `docs/release/` | 玩家文档与插件 API 参考（中文为主 + English summary） |
| `docs/` | 开发/验收文档（构建证据、稳定性、素材审计、安装布局等） |
| `tools/make_release.sh` | 白名单发行打包脚本（生成 ZIP + SHA-256 清单） |
| `SL-Mod-Framework-Plan.md` | 项目计划与分阶段路线图 |

## 系统要求

- Windows 10 1803+ / Windows 11（诊断包依赖系统 `tar.exe`，缺失时回退为文件夹）。
- Steam 版《FNAF: Sister Location》**v1.121**，`SisterLocation.exe` SHA-256 `c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73`（950,556,728 字节）。
- 游戏为 32 位（x86）；框架同为 32 位，无额外运行库依赖（无需 VC++ Redist）。

## 安全与边界

- 仅本地单机使用；不含联网、遥测、自动更新。
- 只对**已知构建**生效：EXE 指纹不匹配时启动器拒绝启动（退出码 3），不会静默降级。
- 游戏更新后需等待适配；不要用旧版强行加载新 EXE。
- 不含任何游戏素材；日志/诊断包可能含本机路径与用户名，分享前请自行检查。

## 已知限制

- 无「非注入 / HUD-only」模式（必须由启动器注入）。
- 锁屏/安全桌面下无法创建 D3D9 设备，GUI 不显示（解锁后正常）。
- 音频识别只覆盖部分样本；字幕/音频系统不在 v1.0.0 范围。
- 示例插件 `SoftJumpscares`、`ThreatTelegraphs` 不随 v1.0.0 发行包，后续独立发布。

## 许可证

项目本体 MIT，见 `LICENSE`；第三方组件声明见 `THIRD_PARTY_NOTICES.md`（发行包内为 `docs/THIRD_PARTY_NOTICES.md`）。游戏及其派生素材版权归原权利人，本项目不分发。

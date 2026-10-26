# SLModFramework v1.0.0

《Five Nights at Freddy's: Sister Location》(Steam PC, v1.121) 的本地运行时 Mod 框架首个公开版本。

**English summary**: First public release of SLModFramework — a local runtime mod framework for the Steam PC build of *FNAF: Sister Location*: ImGui in-game panel (Insert), Lua 5.4 plugin host (API v1), read-only game state, and optional QoL features. MIT-licensed source; no game assets are included.

## 下载与校验

- `SLModFramework-1.0.0-win-x86.zip` — 805,520 B，SHA-256 `f8932e2513efbb70ae662d67ae51e21c62f84afc75bcfcb5a27b90980acc7859`
- `SHA256SUMS` — 启动器 / 框架 DLL / 发行 ZIP 的校验值

## 安装（3 步）

1. 把 ZIP 解压到游戏根目录，得到 `<Steam>\steamapps\common\Five Nights at Freddy's Sister Location\SLModFramework\`。
2. 运行 `SLModFramework\bin\launcher32.exe`（不要直接双击游戏 exe）。
3. 进入游戏后按 `Insert` 打开面板；配置与日志在 `SLModFramework\config`、`SLModFramework\logs`。

详见仓库 `docs/release/INSTALL.md`（安装）、`UNINSTALL.md`（卸载=删除目录）、`TROUBLESHOOTING.md`（排障）、`PLUGIN_API.md`（插件开发）。

## 内容

- 游戏内 ImGui 面板：EXE 指纹、D3D9 设备、Present/FPS、场景、对象/计数器、插件管理。
- Lua 5.4 插件宿主（API v1）+ HelloPlugin 示例；启用/禁用写回清单并热重载；连续 30 次运行时错误自动禁用。
- 只读游戏状态 API（场景/计数器/alterable/对象矩形）+ ctx.audio。
- QoL：失焦/GUI 打开时释放鼠标、密码盘小键盘、Private Room 键位（含门动画/音效）、可自定义门音效。
- 稳定与排障：日志轮转 4 MiB × 5、一键导出诊断包、恢复默认配置。

## 系统要求

- Windows 10 1803+ / Windows 11（诊断包依赖系统 `tar.exe`）。
- Steam 版 FNAF: Sister Location **v1.121**，`SisterLocation.exe` SHA-256 `c60e1c7994316426d824b82004aa5c76e3aab06d4e78e0c9391628e0d52b2c73`；指纹不匹配时启动器拒绝启动（退出码 3）。
- 32 位框架，无额外运行库依赖（无需 VC++ Redist）。

## 已知限制

- 无 HUD-only（非注入）模式；锁屏下无法创建 D3D9 设备（解锁后正常）。
- 音频识别只覆盖部分样本；字幕系统不在本版范围。
- 示例插件 SoftJumpscares / ThreatTelegraphs 不随本包，后续独立发布。
- 不含任何游戏素材；日志/诊断包可能含本机路径，分享前请自行检查。

## 许可证

项目本体 MIT（见 `LICENSE`）；第三方声明见 `THIRD_PARTY_NOTICES.md`。游戏及派生素材版权归原权利人。

# Changelog

## v1.0.0 — 2026-10-27（首个公开版本）

### 新增
- 运行时框架：`launcher32.exe` 指纹校验 + 挂起注入；`SLModFramework32.dll`（D3D9 Present/Reset Hook、ImGui 面板、HUD）。
- 游戏内 GUI（Insert）：游戏/设备信息、场景与对象状态、DebugInspector、帧跳转（F9，开发用）、插件管理、诊断与配置入口。
- 插件系统 API v1：Lua 5.4 沙箱、`plugin.ini` + `main.lua`、生命周期 `on_load/on_update/on_draw_ui/on_draw_overlay/on_shutdown`、GUI 启停与热重载、错误计数与自动禁用。
- 只读游戏状态 API：`ctx.game`（场景/帧/FPS/对象/计数器/alterable）、`ctx.audio`、`ctx.ui`。
- QoL：鼠标释放（失焦/开 GUI）、密码盘小键盘、Private Room（帧 27）A/D/W/S 快捷键（含门动画/音效）、可自定义门音效。
- 稳定性：日志轮转 4 MiB × 5、诊断包导出、恢复默认配置、配置损坏自动备份重建。
- 发行：安装布局 B（`<game>\SLModFramework`）、MIT 许可证、第三方声明、可复现构建。

### 已知限制
- 仅支持 Steam PC v1.121（EXE 指纹门禁）；游戏更新后需适配。
- 无 HUD-only（非注入）模式。
- 锁屏/安全桌面下无法创建 D3D9 设备，GUI 不显示。
- 音频识别只覆盖部分样本；字幕系统不在本版范围。
- 示例插件 `ThreatTelegraphs`、`SoftJumpscares` 不随包（后续独立发布）。
- 帧 31（Custom Night 场景）QoL 仅覆盖帧 27 的 A/D/W 快捷键；其他场景用原生操作。
- 未做代码签名；杀软评估仅本机 Defender。

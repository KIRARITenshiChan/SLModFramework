# 故障排查 / Troubleshooting

## 1. 启动器没反应或闪退（看退出码）

启动器退出码定义：

| 退出码 | 含义 | 处理 |
|---|---|---|
| 0 | 正常（游戏已退出、启动器收尾完成） | — |
| 1 | 参数错误、找不到 EXE/DLL、读取失败 | 检查 `SLModFramework` 是否完整、是否改名/移动；看 `logs\framework\loader-*.log` |
| 3 | **游戏 EXE 指纹不匹配**（不启动游戏） | 游戏不是 v1.121 或文件被修改/更新过；校验 `SisterLocation.exe` SHA-256 是否为 `c60e1c7994…2b2c73`；不要用 `--no-verify` 强行玩 |
| 4 | 注入失败（远程 LoadLibraryW 返回 0） | 常被杀软拦截或 DLL 缺依赖；看日志、加白名单；确认 DLL 与 launcher 同版本 |

> 启动器是控制台程序但无窗口；请用「日志文件」而不是「双击后没反应」判断。日志：`SLModFramework\logs\framework\loader-*.log`。

## 2. 游戏启动了，但按 Insert 没面板

1. 确认是从 `SLModFramework\bin\launcher32.exe` 启动（不是 Steam 直接启动游戏）。
2. 检查 `slmodf-*.log` 是否有 `init status=ok`；若为 `status=deferred`，表示没等到 `mmfs2.dll`（见下一条）。
3. 热键可能被其他软件占用（RTSS、Discord、输入法等）：改 `config\framework.ini` 的 `hotkey`（十六进制 VK，例如 `0x2D`=Insert、`0x71`=F2），重启游戏。
4. 若日志中有 `CreateDevice failed 0x8876086A`：发生在**锁屏/安全桌面**下，解锁后重试即可（游戏本身同样无法创建 D3D9 设备）。
5. 全屏独占/特殊覆盖层冲突：尝试窗口化或关闭其他注入类工具（RTSS/Afterburner 等）。

## 3. 日志里 `wait mmfs2` 超时 / 加载很慢

- 游戏启动早期 `mmfs2.dll` 尚未加载，框架会轮询等待（默认 `wait_mmfs2_ms`）；过短会 deferred。把该值调大（如 20000）后重启。
- 首次启动需要解包资源到 `%TEMP%\mrt*.tmp`，机械硬盘上会更慢。

## 4. 插件不加载 / 自动禁用

- GUI「框架/插件」段会显示每个插件的状态与错误计数；日志搜索 `plugin|`。
- 常见错误：缺少 `plugin_info()` 全局函数、`api_version` 不匹配、Lua 语法错误、manifest 字段缺失。
- **连续 30 次运行时错误会自动禁用**并写回 `plugin.ini` 的 `enabled=0`（日志 `AUTO-DISABLED`）。修复后手动改回 `enabled=1` 或 GUI 重新勾选。
- 沙箱限制：仅 `base/table/string/math/coroutine/utf8` 可用；无 `io/os/package`。

## 5. 帧率下降 / 卡顿

- GUI 关闭时开销极小；打开 GUI 时每帧渲染面板（含对象表刷新）会略高。可关闭「场景状态」自动刷新。
- `record=1` 会按 `record_interval_ms` 写 JSONL，大型场景可能造成 IO 抖动；非排查时建议 `record=0`。
- 全表对象扫描只在换场景时发生（约 230–360 ms），属一次性开销。

## 6. 配置损坏 / 想重置

- 启动时若 `framework.ini` 解析失败，会自动备份为 `framework.ini.bad-<时间戳>` 并写回默认值。
- 手动重置：关闭游戏，删除 `config\framework.ini`，下次启动自动重建；或 GUI 中「恢复默认配置」（会先备份当前文件）。
- 配置模板见 `config\framework.ini.example`。

## 7. 杀毒软件 / SmartScreen

- 未做代码签名，SmartScreen 会提示「未知发布者」；核对下载来源与 SHA-256 后选择「仍要运行」。
- 注入行为可能被启发式引擎误报。v1.0.0 仅在本机 Windows Defender 实测通过；其他引擎未测试。可把 `SLModFramework` 目录加入白名单，或直接用原版游戏。
- 发行包 SHA-256 见发布页；请勿使用来源不明的 DLL。

## 8. 导出诊断包（反馈问题用）

- GUI「框架/插件」段底部 →「导出诊断包」；生成 `SLModFramework\logs\diagnostics-<时间戳>.zip`（失败则生成同名文件夹）。
- 内容：summary.txt、framework.ini、最近日志（`slmodf-*`/`loader-*`/harness）、launch/init 记录、插件清单。
- **注意**：诊断包包含本机路径与用户名，分享前请自行检查/删改。

## 9. 反馈问题时请附

1. `summary.txt` + 最近一份 `slmodf-*.log`、`loader-*.log`；2. 游戏版本/EXE SHA-256；3. 复现步骤；4. 是否锁屏、是否其它注入工具。

## English summary

Check `SLModFramework\logs\framework\` first. Exit code 3 = game EXE fingerprint mismatch (wrong build/updated game); 4 = injection blocked (antivirus). If the panel does not open, launch via `bin\launcher32.exe`, verify the hotkey, and make sure the session is unlocked (D3D9 device creation fails on the lock screen). Use the in-GUI diagnostics export (zip includes local paths — review before sharing) and reset the config by deleting `config\framework.ini`.

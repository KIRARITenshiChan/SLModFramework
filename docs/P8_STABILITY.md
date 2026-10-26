# P8.2 — 稳定性与失败处理证据（【v1.20】2026-10-27）

> Phase 8 / P8.2 交付物：日志轮转、诊断包、配置重置、插件错误自动禁用四项稳定性能力的实现与验证。
> 构建：`SLModFramework32.dll` 1,617,920 B / `launcher32.exe` 31,744 B / `harness32.exe` 1,517,056 B（2026-10-27 06:22，源码见 `release/BUILD_MANIFEST.sha256`）。
> 结论：四项均已实现；轮转、诊断包、自动禁用有离线/实机证据；配置重置入口代码已接入 GUI，点击验证并入 P8.5 干净环境演练。

## 1. 实现位置

| 能力 | 入口/函数 | 文件 | 默认/阈值 |
|---|---|---|---|
| 日志轮转 | `LogSetPath()` + `RotateLogLocked()` | `framework/native/util.cpp` | 4 MiB（`SLMODF_LOG_ROTATE_BYTES_DEFAULT`）× 5 份备份（`.1`..`.5`），可用环境变量 `SLMODF_LOG_ROTATE_BYTES`（≥4096）覆盖 |
| 诊断包 | `ExportDiagnostics()` | `framework/native/util.cpp` | 写 `<framework>\\logs\\diagnostics-<ts>\\`（summary.txt、config、最近日志、loader/launch/init sidecar、插件清单），再调系统 `tar.exe` 打包为 `diagnostics-<ts>.zip`；tar 失败时保留目录并返回目录路径 |
| 配置重置 | `ResetConfigAndReload()` | `framework/native/util.cpp` | 先把当前配置备份为 `framework.ini.bak-YYYYmmdd-HHMMSS`，再写默认模板并重新 `LoadConfig()`，日志记录新值 |
| 插件自动禁用 | `HandleLuaError()` 连续错误计数 | `framework/native/plugin_host.cpp` | 连续 ≥30 次运行时错误（`SLMODF_PLUGIN_AUTO_DISABLE_ERRORS`）→ 置 `enabled=0` 写回 plugin.ini + 日志 `AUTO-DISABLED`；任何一次成功调用清零计数 |

GUI 入口（`framework/native/overlay.cpp`）：「框架/插件」段底部新增两个按钮 **导出诊断包**、**恢复默认配置**（各带 tooltip）；被自动禁用的插件行显示红色「已自动禁用」。

## 2. 离线证据（无 D3D9 设备）

新增 harness 模式 `--p8-only`（`framework/tests/d3d9_harness.cpp`）：

1. 设置 `SLMODF_LOG_ROTATE_BYTES=8192` 并 `LogSetPath()`；
2. 写入 400 行填充日志，触发多次轮转；
3. 检查 `<log>.1` 存在；
4. 调用 `ExportDiagnostics()`，检查返回的 zip 文件 >200 B。

| 项目 | 结果 | 证据 |
|---|---|---|
| `harness32.exe --p8-only` | rc=0 **PASS**（`SUMMARY5 pass=1 rotated=1 diag_ok=1`） | `logs/framework/harness-20261027-063133.log`；诊断包 `logs/framework/diagnostics-20261027-063133.zip`（约 75 KB） |
| 备份份数上限 | 轮转后恰好 `.1`..`.5`，旧文件被覆盖 | 同目录 `harness-20261027-062222.log.1..5` |
| 轮转标记 | 每次轮转写 `[log] rotated (threshold N bytes, 5 backups)` | 同上日志 |

离线回归（P8.2 改动后重跑，四模式全 PASS）：

| 模式 | 结果 |
|---|---|
| `--wndproc-only` | rc=0 PASS |
| `--imgui-only` | rc=0 PASS（`harness-20261027-063132.log`） |
| `--plugins` | rc=0 PASS（`harness-20261027-063132.log`，6 夹具 SUMMARY4 pass=1） |
| `--p8-only` | rc=0 PASS（见上） |

字节级可复现（P8.2 代码合入后重新验证）：连续两次 `build.sh` DLL `8faf8e66d4a971268566737b30e68b92c9798c14202a276c96000dae2c5a495c`、launcher `803c54cb312541dc220e41cbe8f2a5579626af100f174426c3930f05ce80738c` 不变；`build_harness.sh` 两次 harness `dccdec7bbfa6445dc4251558824fcbde3b304c45b6ed8699f040c75b722e881c` 不变。

## 3. 实机证据（布局 B 安装目录）

| 项目 | 过程 | 结果 |
|---|---|---|
| 日志轮转 | 环境 `SLMODF_LOG_ROTATE_BYTES=4096` 启动 launcher（pid 13680 / 24796 / 33888） | 日志出现 `[log] rotated (threshold 4096 bytes, 5 backups)`，生成 `.1`..`.4`；单会话即可触发 |
| 插件自动禁用 | 临时插件 `plugins/AutoDisableProbe`（`on_update` 每帧 `error()`） | 精确 30 次后：`plugin| AUTO-DISABLED id=test.autodisable after 30 consecutive errors (last=...main.lua:5: intentional failure ...); enabled=0 written to manifest`；随后 `plugin.ini` 中 `enabled=0` 持久化（见 `slmodf-20261027-062640-33888.log.1`） |
| 诊断包/GUI | GUI 打开、面板滚动/折叠经 PostMessage 尝试截图 | 总体 GUI 截图 `logs/probe/p8-p82-gui.png`；两个新按钮位于「框架/插件」段底部，点击路径并入 P8.5 手动演练（自动化点击坐标不可靠，未作为证据） |

测试后已删除临时插件，安装目录仅保留 `HelloPlugin`；游戏/launcher 无残留进程。

## 4. 残余限制（写入 P8_KNOWN_ISSUES）

- 诊断包含本机日志与路径（含用户名），分享前应自行检查；`summary.txt` 不含密钥类信息（本框架无凭据）。
- 诊断包依赖 Windows 10 1803+ 自带 `tar.exe`；缺失时回退为未压缩目录（功能不丢，只是不便分享）。
- 自动禁用只统计**运行时**连续错误；加载期（`plugin_info`/语法/API 不匹配）错误仍以红色错误行展示，但不会自动写 disabled（用户可在 GUI 关闭）。
- 轮转阈值可用环境变量压到 4096 B 用于测试；稳定性验证中故意使用小阈值，默认 4 MiB。

## 5. 参考

- 代码：`framework/native/util.cpp`、`framework/native/plugin_host.cpp`、`framework/native/overlay.cpp`、`framework/tests/d3d9_harness.cpp`
- 日志：`Workspace/logs/framework/`（离线）、`<game>/SLModFramework/logs/framework/`（实机，junction `Workspace/logs/runtime`）
- 文档：`docs/P8_STATUS.md`、`docs/P8_KNOWN_ISSUES.md`、`docs/P8_BUILD_EVIDENCE.md`

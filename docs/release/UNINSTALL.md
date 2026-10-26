# 卸载与回滚 / Uninstall

框架**不修改游戏文件**（`SisterLocation.exe`、资源、Steam 文件均保持原样），也不写入游戏存档。卸载是纯删除操作。

## 1. 标准卸载

1. 退出游戏与启动器（任务管理器中确认没有 `SisterLocation.exe` / `launcher32.exe`）。
2. 删除整个 `<game>\SLModFramework\` 文件夹（含 bin/config/plugins/logs/docs）。
3. 完成。之后再启动游戏即恢复原版行为。

> 建议先导出诊断包或保留 `logs\`（如需反馈问题）；导出方式见 `TROUBLESHOOTING.md`。

## 2. 只停用不删除

- 配置：编辑 `SLModFramework\config\framework.ini`，把 `hook_d3d9=0` 可关闭 overlay/Hook（仅保留注入与状态读取）；或把 `hotkey` 改成不冲突的键。
- 插件：在 GUI「框架/插件」段取消勾选，或把插件目录下 `plugin.ini` 的 `enabled` 改为 `0`。
- 完全禁用：直接用游戏原 exe 启动（不经过 launcher），框架不会被加载。

## 3. 存档与配置

- 游戏存档：`%APPDATA%\MMFApplications\sl`，框架从不写入；如曾手动用测试工具改过，请从备份恢复。
- 框架配置：只存在于 `SLModFramework\config`，随目录删除即消失。
- 日志/诊断包：`SLModFramework\logs`，可安全删除。

## 4. 验证已回滚

1. 直接双击 `SisterLocation.exe`（或从 Steam 启动），游戏正常进入标题画面。
2. 无 Insert 面板、无框架日志新增，即已回滚。
3. Windows「应用和功能」中本来就没有安装项——框架是绿色解压式部署。

## English summary

Delete the `SLModFramework` folder (game files and saves are untouched). To disable without removing, launch the game directly, disable plugins in `plugin.ini`, or set `hook_d3d9=0` in `config\framework.ini`.

# 安装与启动 / Installation

> SLModFramework v1.0.0，适用于 Steam 版《FNAF: Sister Location》v1.121（`SisterLocation.exe` SHA-256 `c60e1c79…2b2c73`）。

## 1. 目录布局

框架**必须**安装在游戏根目录下、名为 `SLModFramework` 的文件夹中。启动器按固定相对路径推导框架根与游戏根：

```
<Steam>\steamapps\common\Five Nights at Freddy's Sister Location\
├─ SisterLocation.exe              ← 游戏本体（不要修改、不要替换）
└─ SLModFramework\
   ├─ bin\launcher32.exe            ← 启动器：注入 + 启动游戏
   ├─ bin\SLModFramework32.dll      ← 框架本体
   ├─ config\framework.ini.example  ← 配置模板（首次运行自动生成 framework.ini）
   ├─ plugins\HelloPlugin\          ← 示例插件（可保留或删除）
   ├─ logs\framework\               ← 运行日志
   ├─ logs\state\                   ← 场景状态记录（默认关闭）
   ├─ docs\                         ← 本文档
   └─ LICENSE
```

- 框架根 = `launcher32.exe` 所在目录的上一级（即 `SLModFramework`）。
- 游戏根 = 框架根的上一级，必须能找到 `SisterLocation.exe`。
- 因此：**不要把 launcher32.exe 单独拖到桌面或其它目录运行**，也不要把 `SLModFramework` 改名。

## 2. 安装步骤

1. 完全退出 Steam 中的游戏（任务管理器里不应有 `SisterLocation.exe`）。
2. 备份存档（可选但推荐）：`%APPDATA%\MMFApplications\sl`（文本 INI）。框架不会修改存档，但备份总是安全的。
3. 把发行 ZIP 解压到游戏根目录，确认得到 `...\Five Nights at Freddy's Sister Location\SLModFramework\`。
4. 双击 `SLModFramework\bin\launcher32.exe`。
   - 启动器会先校验游戏 EXE 的 SHA-256（约 4–6 秒，无界面，属正常）。
   - 校验通过后以挂起方式启动游戏、注入 DLL，再恢复运行。
   - 若 Windows SmartScreen 提示「未知发布者」，选择「更多信息 → 仍要运行」；本项目未做代码签名。
5. 进入游戏后按 `Insert` 打开/关闭面板。首次运行会在 `config` 生成默认配置，在 `logs\framework` 生成日志。

## 3. 启动器参数（排查/自动化用，一般无需关心）

```text
launcher32.exe [--exe PATH] [--dll PATH] [--test-exit SECONDS]
               [--toggle-gui 8,20] [--test-input 12] [--toggle-key 0x71]
               [--wait-ms N] [--no-verify] [--help]
```

- `--test-exit N`：N 秒后自动关闭游戏（当前实现固定等待 20 秒退出）。
- `--toggle-gui N`：第 N 秒模拟按下热键（默认 Insert），用于无人值守截图/验证 GUI。
- `--no-verify`：跳过 EXE 指纹校验，**仅诊断用**；正常游玩不要用。
- `SL_GAME_EXE` 环境变量可覆盖游戏 EXE 路径（仅限本机测试）。

## 4. 第一次运行检查

1. `SLModFramework\logs\framework\loader-*.log` 应包含：`fingerprint OK`、`spawned suspended`、`remote LoadLibraryW`、`init status=ok version=1.0.0`。
2. `SLModFramework\logs\framework\slmodf-*.log` 应包含：`framework=...`、`config ... version=1`、`plugin scan root=...`、`HelloPlugin ... loaded`。
3. 游戏内 Insert 面板顶部应显示 `SLModFramework 1.0.0` 与版本/构建信息。

## 5. 配置

- 路径：`SLModFramework\config\framework.ini`（首次运行自动生成；损坏时自动备份 `.bad-<时间戳>` 并重建）。
- 常用键：`hotkey`（默认 0x2D = Insert）、`hud`、`record`/`record_interval_ms`、`qol_release_mouse`、`qol_keypad_numpad`、`qol_private_hotkeys`、`qol_door_sfx`、`audio_probe`/`audio_soften`/`audio_gain_percent`、`font_path`/`font_size`。
- 修改后重启游戏生效；GUI 内「恢复默认配置」按钮会备份当前文件并写回默认值。

## 6. 从源码构建（可选）

需要 MinGW-w64 i686 工具链（本机开发用 winlibs）。在 `Workspace\framework` 下执行 `bash build.sh`，产物在 `bin\`；`bash deploy.sh` 可复制到 `<game>\SLModFramework`。构建是可复现的（已用 `-Wl,--no-insert-timestamp`）。

## English summary

Unzip the release into `<game>\SLModFramework` (next to `SisterLocation.exe`) and run `SLModFramework\bin\launcher32.exe`. The launcher verifies the game EXE fingerprint, injects the framework DLL and starts the game; press `Insert` in-game to toggle the panel. Config, logs and plugins live under the framework folder. Do not move the launcher out of `SLModFramework\bin`.

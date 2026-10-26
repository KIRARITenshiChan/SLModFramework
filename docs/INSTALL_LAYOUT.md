# 安装布局（方案 B，v1.20 起生效）

> 用户 m12195 拍板：新建独立 `SLModFramework` 安装目录，按固定相对路径定位框架根与游戏根。
> 本文档描述安装目录树、路径推导规则、部署脚本与开发期约定；面向开发/排障，玩家安装说明见 P8.4 的 `INSTALL.md`。

## 1. 目录树

```text
<游戏根>\                                  例：E:\SteamLibrary\steamapps\common\Five Nights at Freddy's Sister Location
  SisterLocation.exe                       原版游戏，不改动、不修改哈希
  SLModFramework\                          框架根 = bin 的父目录
    bin\launcher32.exe                     启动器（32 位）
    bin\SLModFramework32.dll               框架 DLL（32 位，注入游戏进程）
    config\framework.ini                   首次运行自动生成（缺失即写默认模板）
    config\framework.ini.example           配置模板/文档
    plugins\HelloPlugin\plugin.ini+main.lua 插件（首发只带 HelloPlugin）
    logs\framework\                         loader-*.log / slmodf-*.log / init-<pid>.done / launch-<pid>.json / harness-*.log
    logs\state\                             record=1 时的 state-<ts>-<pid>.jsonl
    assets\sfx\                             可选，用户自备素材（Doorslam.wav）；发布包不带游戏派生素材
    docs\                                   用户文档（安装/卸载/排障）
```

运行时 config / plugins / logs / assets 全部以**框架根**为基准，不在游戏根写文件。

## 2. 路径推导（实现）

- **launcher**：`GetLoaderDir()` 得到 `...\SLModFramework\bin`；`StripOneComponent` 一次得框架根，再一次得游戏根；再拼 `SisterLocation.exe` 并校验存在。目标 EXE 可用 `--exe` 或环境变量 `SL_GAME_EXE` 覆盖（诊断用）。
- **DLL**：`DllMain` 保存 `g_selfInst`，`GetFrameworkDir()` 用 `GetModuleFileNameA(self)` 得 `...\bin\SLModFramework32.dll`，剥两级得框架根；`g_fw.logDir = <framework>\logs\framework`。
- 其余模块沿用 `logDir\..\..` 相对推导（功能正确，日志里会显示 `logs\framework\..\..\config` 形式；P8.2 规范化）。
- 版本：`SLMODF_VERSION = 0.7.0-p8`；插件 API `SLMODF_PLUGIN_API_VERSION = 1`。

日志证据（2026-10-27 05:35 冒烟）：

```text
loader: this=...\SLModFramework\bin
        framework=...\SLModFramework
        game_dir=...\Five Nights at Freddy's Sister Location
        exe=...\SisterLocation.exe  sha256=c60e1c79...  fingerprint OK
dll:    framework=...\SLModFramework
        config missing -> wrote ...\config\framework.ini
        plugin scan root=...\plugins
        HelloPlugin loaded (total=1 loaded=1 ...)
```

## 3. 部署（开发期）

```bash
cd Workspace/framework
bash build.sh                                   # 生成 bin/launcher32.exe + bin/SLModFramework32.dll
"/d/Scoop/apps/git/current/bin/bash.exe" build.sh
bash tests/build_harness.sh                     # 可选：离线 harness
bash deploy.sh                                  # 部署 bin + config 模板 + HelloPlugin
bash deploy.sh --with-docs --with-sfx           # 额外复制 ../docs/*.md 与 ../assets/sfx/Doorslam.wav（仅本机测试）
```

`deploy.sh` 会校验 `<游戏根>\SisterLocation.exe` 存在，创建 `SLModFramework\{bin,config,plugins,logs\framework,logs\state,assets\sfx,docs}` 并复制文件；不删除安装目录中的其他内容。

## 4. 开发期约定

- `Workspace\` 是开发/记录/取证目录；**运行测试一律启动 `<游戏根>\SLModFramework\bin\launcher32.exe`**，不要再用 `Workspace\framework\bin` 里的副本（否则日志/配置会指向开发目录，无法验证发行布局）。
- `Workspace\logs\runtime\` 是指向 `<游戏根>\SLModFramework\logs` 的目录 junction（`cmd /c mklink /J`），便于直接在 Workspace 下看运行日志；它不是发行内容，也别把它当真源。
- 改代码后：杀干净 `SisterLocation.exe` / `launcher32.exe` → 构建 → harness 三模式 → 部署 → 冒烟。
- 历史脚本（p2a–p9 取证）里写死 `%TEMP%\mrt*.tmp` 或旧 Workspace 路径的部分仅作归档参考；活跃工具 `p9_launch_test.ps1` / `p8_safe_capture.py` / `p8_alert_watch.py` / `p7_keypad_helper.py` 已改布局 B 路径。

## 5. 冒烟检查清单（布局 B）

1. 启动 `SLModFramework\bin\launcher32.exe --toggle-gui 14`。
2. `logs\framework\loader-*.log` 含 `framework=` / `game_dir=` / `exe=`，指纹 OK，`init status=ok version=0.7.0-p8`。
3. `SLModFramework\config\framework.ini` 自动生成（或保留用户配置）。
4. `slmodf-*.log`：`plugin scan root=<framework>\plugins`、`HelloPlugin loaded`、`summary total=1 loaded=1`。
5. GUI 标题含版本、场景识别正确、Present/FPS 正常（截图存档）。
6. 退出后无 `SisterLocation.exe` / `launcher32.exe` 残留；游戏根除安装目录外无新增文件。
7. 卸载 = 删除 `SLModFramework\`；Steam 校验文件不应报告原版 EXE 被修改。

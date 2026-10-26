# P8.3 — 第三方许可证与素材审计（【v1.20】2026-10-27）

> Phase 8 / P8.3 交付物：确认 v1.0.0 发行包与公开源码仓库中**不含游戏派生素材**，第三方组件许可证齐备。
> 结论：**通过**；无版权阻断项。

## 1. 许可证文件落位

| 文件 | 作用 | 位置 |
|---|---|---|
| `LICENSE` | 项目本体 MIT（Copyright (c) 2026 KIRARITenshiChan） | Workspace 根；发行包复制到顶层与 `docs/LICENSE` |
| `THIRD_PARTY_NOTICES.md` | ImGui/MinHook/Lua/MinGW 运行库声明 | Workspace 根；发行包 `docs/THIRD_PARTY_NOTICES.md` |
| `third_party/imgui/LICENSE.txt` | ImGui MIT 原文 | vendor 源码目录（构建需要，随源码仓库） |
| `third_party/minhook/LICENSE.txt` | MinHook BSD-2 原文 | 同上 |
| Lua 5.4.7 MIT 文本 | vendored 目录缺 `COPYRIGHT`，已在 `THIRD_PARTY_NOTICES.md` 内全文收录 | — |

发行包规划路径（`release/make_release.sh` 生成）：
```
SLModFramework/
  LICENSE
  README.md
  docs/{README.md, INSTALL.md, UNINSTALL.md, TROUBLESHOOTING.md, CHANGELOG.md, PLUGIN_API.md,
        THIRD_PARTY_NOTICES.md, third_party/{imgui-LICENSE.txt, minhook-LICENSE.txt, lua-LICENSE.txt}}
  bin/{launcher32.exe, SLModFramework32.dll}
  config/framework.ini.example
  plugins/HelloPlugin/{plugin.ini, main.lua}
```

## 2. 素材来源分类与处置

| 类别 | 例子 | 权利人/来源 | 处置 |
|---|---|---|---|
| 游戏本体与提取资源 | `SisterLocation.exe`、`%TEMP%\mrt*.tmp` 解包资源、ctfak-cpp 全量 dump（`recon/dumps/`，约 1.1 GB） | Scott Cawthon / Steel Wool Studios | **仅本地开发**；不进源码仓库、不进发行包；不随任何分发包 |
| 游戏派生音频 | `assets/sfx/Doorslam.wav`、`logs/audio/dump-*.bin` | 游戏音频 | **仅本机测试**；发行包不包含；Doorslam 由用户自备（缺失静音） |
| 游戏派生截图/录像 | `logs/probe/*.png`（含游戏画面）、截图索引 | 游戏画面 | **仅本地取证**；发行文档只用文字/UI 自身截图；不把含游戏画面的截图放入 release |
| 逆向分析数据 | `analysis/data/*.json`、`recon/out/` 偏移表 | 由游戏二进制分析得出的事实数据 | 事实/偏移可用于开发；**不随发行包**；公开仓库不收录 dump 与提取资源 |
| 自研代码/文档 | `framework/`、`plugins/HelloPlugin`、`docs/` 新写文档 | 本项目 | MIT；随源码仓库与发行包（按 P8.4 文档清单） |
| 第三方源码 | ImGui、MinHook、Lua | 各自作者 | 按各自许可证随源码仓库；发行包附声明与许可证文本 |
| 工具链运行时 | libstdc++/libgcc 静态链接 | GCC/MinGW-w64 | GPLv3 + Runtime Library Exception；见 `THIRD_PARTY_NOTICES.md` |

## 3. 自动/人工检查

- 对 `framework/`、`plugins/`、`docs/` 执行媒体与二进制扫描（`.wav/.mp3/.bin/.png/.jpg/.ogg`）：**未发现**媒体文件（2026-10-27 06:36 检查）。
- 发行脚本只从白名单路径复制（`framework/bin`、`config/framework.ini.example`、`plugins/HelloPlugin`、`docs` 允许清单、`LICENSE`、`THIRD_PARTY_NOTICES.md`），并显式拒绝 `assets/`、`recon/`、`logs/`、`analysis/`、`backups/`。
- 源码仓库（公开 GitHub）以 `framework/` + `third_party/` + `plugins/HelloPlugin/` + 本组发布文档 + 根许可证文件为主体；不含 `recon/`、`logs/`、`assets/`、`analysis/`、`research/`、`plan-backups/`。

## 4. 残余风险与声明

- 游戏更新/不同地区版本可能改变 EXE；指纹门禁会拒绝（KI-04），文档需说明。
- 发行包不含游戏素材；若用户自行把游戏派生文件放入 `assets/`，由其自行承担许可风险（文档声明）。
- 静态链接 GCC 运行库的分发合规性依赖 GCC Runtime Library Exception；本项目以目标码 + 对应源码公开方式满足（源码仓库即对应源码）。

## 5. 参考

- `LICENSE`、`THIRD_PARTY_NOTICES.md`、`docs/P8_STATUS.md`、主计划 Phase 8 §P8.3

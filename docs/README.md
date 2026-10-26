# 文档索引（Workspace/docs）

本目录是计划要求的正式文档（从 `recon/` 与 `research/` 提炼，不复制 dump）。中文为主，证据分级见根目录 `README.md`。

## 基础与运行时

- `PREFLIGHT.md` — 可行性预检与总体结论
- `BUILD_FINGERPRINT.md` — EXE 指纹、Fusion 版本、Steam 构建
- `LOADING_AND_LIFECYCLE.md` — 注入/加载/退出生命周期、PAMU 扫描修复
- `RUNTIME_PROBE.md` — P2a 只读注入探针实测（模块/窗口/DPI/TEMP 解包）
- `RUNTIME_SCENE.md` — 场景识别、CRunApp/RunHeader 偏移、warp 协议
- `RUNTIME_STATE.md` — 对象/计数器/alterable 读取与 recorder

## Overlay / 输入 / 配置

- `OVERLAY_INPUT.md` — D3D9 Hook、ImGui、输入分流、字体/DPI、验收表
- `PLUGIN_API_V0.md` — 插件 API v1（历史文件名保留）生命周期与 UI 原语
- `QOL.md` — 内置 QoL（鼠标释放/小键盘/Private Room 快捷键/门音效）

## 音频 / 威胁 / 复核

- `AUDIO_HOOK.md` — DirectSound Hook、chunk 指纹、scream 识别与柔化
- `THREAT_RULES.md` — ThreatTelegraphs 全场景规则、证据与限制（示例插件）
- `P5_NATURAL_VERIFICATION.md` — 自然游玩复核（双轨证据人工轨道）

## Phase 8（发布）

- `INSTALL_LAYOUT.md` — 布局 B 安装目录/路径推导/deploy/冒烟清单
- `P8_STATUS.md` — P8.0 现状盘点：版本一致性、功能×证据状态表、QoL 默认值、首发范围
- `P8_BUILD_EVIDENCE.md` — P8.1 干净构建、字节级可复现、objdump 依赖审计、离线回归
- `P8_STABILITY.md` — P8.2 日志轮转/诊断包/配置重置/插件自动禁用实现与证据
- `P8_ASSET_AUDIT.md` — P8.3 第三方许可证落位与素材审计（发行白名单）
- `P8_ACCEPTANCE_DRILL.md` — P8.5 干净安装→使用→卸载→原版对照演练记录
- `P8_KNOWN_ISSUES.md` — P8 已知问题与发行前关闭项
- `release/` — 玩家向中文为主文档（README/INSTALL/UNINSTALL/TROUBLESHOOTING/CHANGELOG/PLUGIN_API），由 `tools/make_release.sh` 打进发行 ZIP

## 计划与工作痕迹

- 主计划：`../SL-Mod-Framework-Plan.md`（Phase 8 节）
- 状态/证据：`../logs/`（probe/state/audio/framework）、`../analysis/`、`../research/`
- 构建与部署：`../framework/build.sh`、`deploy.sh`、`tests/build_harness.sh`

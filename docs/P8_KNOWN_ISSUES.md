# P8 已知问题清单（【v1.20】2026-10-27，P8.0 交付物）

> 记录 1.0.0 发行前必须公开或规避的已知问题。严重度：**阻断**（发行前必须修）/ **高**（有规避或明确限制声明）/ **中**（记录并文档化）/ **低**（观察项）。

| ID | 严重度 | 问题 | 影响 | 规避/处理 | 关联 |
|---|---|---|---|---|---|
| KI-01 | 中 | 退出时游戏可能以 `0xC0000005` 结束（WM_CLOSE 触发） | 退出码难看；不影响运行 | 原版无框架同样如此（已对照），文档声明；测试不把退出码当回归 | docs/LOADING_AND_LIFECYCLE.md |
| KI-02 | 中 | 锁屏/安全桌面下无法创建 D3D9 设备（CreateDevice 0x8876086A） | GUI/HUD 不出、离线 harness 默认模式 SKIP | 解锁后重测；环境限制，非框架缺陷 | docs/OVERLAY_INPUT.md |
| KI-03 | 中 | 无 HUD-only（非注入）模式 | 无法在纯外部进程绘制 | 1.0 列为已知限制；需要另一套 Overlay 架构，不在 P8 实现 | 计划 Phase 8 §8.0 |
| KI-04 | 高 | 游戏更新后 EXE SHA 变化 → 启动器 exit 3 拒绝启动 | 新版本游戏无法直接用框架 | 预期安全行为；文档写期望/实际哈希与适配流程；不静默降级 | plan §P8.4 |
| KI-05 | 低 | 日志/配置路径显示 `logs\framework\..\..\config` | 排障时可读性差 | 功能正确；P8.2 规范化为 `<framework>\config` 绝对路径 | plan §8 风险表 |
| KI-06 | 中 | 帧 31（Custom Night）没有 A/D/W 门快捷键 | 仅帧 27 Private Room 有；帧 31 仍靠鼠标 | 文档说明；后续可作为插件（需内存写门状态）或框架扩展 | docs/QOL.md |
| KI-07 | 低 | Bonnet 生成随机（`random(30)<Diff`） | 预警插件无法保证每局都捕获 | 示例插件内部按出现后倒计时处理；不阻塞首发 | docs/THREAT_RULES.md §17 |
| KI-08 | 中 | 密码盘小键盘依赖前置状态与 NumLock | 不满足时按键交给游戏（可能判错） | 文档写明条件；qol 可关闭 | docs/QOL.md |
| KI-09 | 中 | 鼠标 hook 未装成（AV/其他 overlay 占用）时静默失效 | 失焦仍锁鼠标 | 日志 `qol| cursor hooks installed=x/3`；可 `qol_release_mouse=0` 关闭 | framework/README.md |
| KI-10 | 中 | Doorslam 素材不随发行包 | 缺失时关门静音 | 用户自备放入 `assets/sfx/Doorslam.wav`；功能不受影响 | plan §P8.3 |
| KI-11 | 低 | DirectSound 只认 chunk#0；204/392 个 MP3 样本无法指纹 | 音频识别不完整（字幕/音效分类受限） | 状态驱动为主；Phase 9 字幕提案记录此限制 | docs/AUDIO_HOOK.md |
| KI-12 | 中 | 攻击档提示窗口极短（0.75–1.0 s，含轮询延迟） | “攻击”提示价值有限 | 真正有效的是到门高警（ThreatTelegraphs 已优先高警） | docs/THREAT_RULES.md |
| KI-13 | 低 | `on_draw_overlay` 只在 D3D9 Present 内绘制 | 无设备时预警不可见 | 与 KI-02 同源；文档说明 | framework/README.md |
| KI-14 | 低 | 示例插件不随 v1.0.0 发布 | 首发功能面更小 | 用户 m12195 同意的裁剪；后续独立发布 | 计划 §1.3 / §8.0 |
| KI-15 | 中 | ThreatTelegraphs 多局误报率/漏报率未测 | 预警可信度未知 | 示例插件定位；发布后收集反馈；不阻塞框架 1.0 | plan §8 风险表 |
| KI-16 | 低 | Private Room 注入与 `Ennard[3]==99` 存在竞态 | 极短窗口内操作可能无效 | 文档说明；不影响正常游玩 | docs/QOL.md |
| KI-17 | 低 | 同名计数器（如 `compute`）在 Lua 表只留最后一个 | 插件读到的计数器可能不完整 | Phase 5 已知限制；需要时用 alterable/专题 API | docs/PLUGIN_API_V0.md |
| KI-18 | 低 | ~~日志轮转/诊断包/配置重置/插件错误自动禁用尚未实现~~ | ~~长会话日志增长、排障缺工具~~ | **已解决（P8.2）**：日志轮转 4 MiB×5、诊断包 zip、配置重置入口、连续 30 次运行时错误自动禁用；证据 `docs/P8_STABILITY.md` | plan §P8.2 |
| KI-21 | 中 | 诊断包含本机日志/路径（可能含用户名） | 用户直接分享可能泄露环境信息 | 诊断包生成后由用户自行检查；文档提醒可只发 `summary.txt`；框架本身无凭据数据 | docs/P8_STABILITY.md |
| KI-22 | 低 | 插件自动禁用只统计运行时连续错误 | 加载期错误（`plugin_info`/语法/API 不匹配）不会自动写 disabled | GUI 仍显示红色错误行，用户可手动关闭；日志含 `consec=` 便于定位 | docs/P8_STABILITY.md |
| KI-23 | 低 | 诊断包依赖 Windows 自带 `tar.exe` | 极老系统（<10 1803）无法直接打包 | 回退为未压缩目录（功能不丢）；文档说明 | docs/P8_STABILITY.md |
| KI-19 | 中 | 音频 dump、recon dump、游戏派生素材仅本地 | 误入发行包会造成版权/合规问题 | P8.3 素材审计清单；发行 ZIP 明确排除 | plan §P8.3 |
| KI-20 | 中 | 未验证兼容性（全屏/分辨率切换、其他 Windows 版本、其他杀软） | 可能有未覆盖问题 | 文档标记“未验证/不支持”；P8.6 只声明本机 Defender 实测 | plan §P8.6 |
| KI-24 | 低 | ~~高 DPI/小屏下主窗口内容高于屏幕、底部按钮不可达~~ | ~~玩家无法触达诊断/重置按钮~~ | **已解决（P8.5）**：主窗口按显示高度限高并允许滚动，1440@150% 滚轮实测可达底部；证据 `docs/P8_ACCEPTANCE_DRILL.md` §3 | plan §P8.5 |

## 发行前必须关闭（阻断）

- 无。原 KI-18（P8.2 稳定性缺口）已关闭并验证（`docs/P8_STABILITY.md`）；当前无阻断项。

## 参考

- `docs/P8_STATUS.md`（功能×证据状态表）、`docs/INSTALL_LAYOUT.md`、主计划 Phase 8 节。

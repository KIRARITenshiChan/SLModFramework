# SLModFramework 插件 API v1 / Plugin API v1

适用框架 **v1.0.0**。插件接口版本为整数 **1**：`plugin.ini` 与 `plugin_info()` 都写 `api_version=1`。破坏性变更会升到 2（旧插件显示 API 不匹配、不加载）。

> 本文面向插件作者；只读 API，不开放任意内存读写、Hook 注册、文件写。

## 1. 目录与清单

插件放在框架根的 `plugins\` 下，每个插件一个子目录：

```
<game>\SLModFramework\plugins\<插件目录>\
  plugin.ini      # 清单（必需）
  main.lua        # 入口（必需；可用 entry= 改名）
```

`plugin.ini`（`key=value`；`#`/`;` 开头为注释，`[节]` 行忽略）：

| 字段 | 必需 | 说明 |
|---|---|---|
| `id` | 是 | 稳定 ID（建议 `作者.名字`）；日志与去重依据 |
| `name` | 否 | 显示名，缺省回退 `id` |
| `version` | 否 | 插件版本，缺省 `0.0.0` |
| `api_version` | 是 | 必须为 **1** |
| `enabled` | 否 | `0`=禁用，其他=启用（缺省启用） |
| `entry` | 否 | 入口 .lua 文件名，缺省 `main.lua` |

示例：

```ini
id=local.MyPlugin
name=My Plugin
version=0.1.0
api_version=1
enabled=1
entry=main.lua
```

扫描根：`<framework>\plugins`；最多 **32** 个插件。GUI「框架/插件」段可勾选启停（写回清单并热重载）；目录缺失/为空不是错误。

## 2. 沙箱与入口

- 每个插件运行在共享 Lua 5.4 状态中，但有独立只读 `_ENV`（全局只读，写全局不影响其他插件）。
- 可用库：`base`、`table`、`string`、`math`、`coroutine`、`utf8`；**不可用**：`io`、`os`、`package`、`debug`、`dofile`、`loadfile`、`load`、`require`。
- 宿主注入全局 `sl`：`sl.api`（`"0.1"`）、`sl.api_version`（1）、`sl.framework_version`、`sl.log(msg[,level])`、`sl.warn(msg)`。

### 必需：`plugin_info()`

必须是**全局函数**，返回表；缺函数/不返回表 → 插件加载失败：

```lua
function plugin_info()
    return { id = "local.MyPlugin", name = "My Plugin", version = "0.1.0", api_version = 1 }
end
```

## 3. 生命周期

| 回调 | 时机 | 说明 |
|---|---|---|
| `on_load(ctx)` | 加载成功后一次 | 抛错 → 插件置错误（不影响其他插件） |
| `on_update(ctx)` | 每帧（ImGui NewFrame 前） | 逻辑更新；不要长时间阻塞 |
| `on_draw_ui(ui)` | 每帧，仅 GUI 打开时 | 在框架面板内绘制；宿主已 `PushID` |
| `on_draw_overlay(ui)` | 每帧（GUI 可见与否都调用） | 屏幕前景绘制，用 §6 的图元 |
| `on_shutdown()` | 禁用/重载/卸载时一次 | 清理 |

回调都在渲染线程执行（D3D9 设备线程）；Lua 单线程使用，无需加锁。插件错误被 `pcall` 捕获，**连续 30 次运行时错误会自动禁用**并写回 `plugin.ini enabled=0`（日志 `AUTO-DISABLED`）；修复后重新勾选/改清单即可。

## 4. `ctx` 游戏状态

| 字段 | 类型 | 说明 |
|---|---|---|
| `ctx.framework_version` | string | 如 `1.0.0` |
| `ctx.game.supported` | bool | 版本门禁通过且当前帧已识别；**false 时不得做肯定判断** |
| `ctx.game.frame_id` | int | Present 帧号 |
| `ctx.game.scene_id` | int | 帧索引 0–35；未知 -1 |
| `ctx.game.scene_name` | string | 如 `Breaker Room`、`Final Encounter` |
| `ctx.game.scene_width/height` | int | 逻辑尺寸；未知 0 |
| `ctx.game.counters_ready` | bool | 计数器快照就绪 |
| `ctx.game.counters` | table | 计数器名→整数（≤64 项，重名后者覆盖） |
| `ctx.game.fps` / `presents` | number/int | 帧率 / Present 累计 |
| `ctx.game.objects_ready` | bool | 对象表 + alterables 就绪 |
| `ctx.game.alterables` | table | 对象名→0-based alterable 表；当前仅监视 `Ennard`、`flip it` |
| `ctx.game.alterable(name, i)` | int/nil | 实时读取任意 Active 对象第 i 个 alterable |
| `ctx.game.object_rect(name)` | `{x,y,w,h}`/nil | 对象场景坐标与尺寸（支持事件动态创建对象） |

```lua
function on_update(ctx)
    if not ctx.game.supported or not ctx.game.objects_ready then return end
    local st = ctx.game.alterables.Ennard and ctx.game.alterables.Ennard[3]
    local phase = ctx.game.alterable("Freddy", 6)   -- 帧 31 Freddy 阶段
    local r = ctx.game.object_rect("Active 19")      -- Bonnet 行走体
end
```

限制：读不到返回 `nil`，**不得当 0**；`object_rect` 是场景坐标（非屏幕像素），按场景宽高换算；动态对象回退依赖 Fusion build 286 的布局，版本不符返回 nil。

## 5. `ctx.audio`（音频探针）

| 字段/函数 | 说明 |
|---|---|
| `supported` | 探针已安装；false 时不得做音频判断 |
| `soften_enabled` / `gain_percent` | 框架级柔化开关 / 目标音量百分比（1..100） |
| `last.has/name/handle/chunk/scream` | 最近匹配样本信息 |
| `stats.creates/plays/matched/screams` | 累计统计 |
| `set_soften(enabled, gain_percent)` | 设置柔化；只影响已识别的 scream |
| `reset_soften()` | 恢复 config 中的初始值（不是关闭） |
| `set_names({...})` / `clear_names()` | 设置/清除柔化白名单（最多 16 个，大小写不敏感） |

## 6. `ui`：面板控件与前景绘制

`on_draw_ui(ui)` 的 `ui` 是控件表：

| 函数 | 对应 |
|---|---|
| `ui.text(s)` / `ui.text_disabled(s)` | 文本 / 灰文本 |
| `ui.separator()` / `ui.same_line()` | 分隔线 / 同行 |
| `ui.checkbox(label, value)` → bool | 复选框 |
| `ui.slider_int(label, value, min, max)` → int | 整数滑条 |
| `ui.button(label)` → bool | 按钮（本帧点击为 true） |

`on_draw_overlay(ui)` 的 `ui` 是前景 DrawList 原语（坐标=物理客户区像素，原点左上；颜色 ImU32/ABGR，缺省白）：

| 函数 | 说明 |
|---|---|
| `ui.viewport()` → w,h | 客户区像素尺寸 |
| `ui.text_size(text[, size])` → w,h | 测量文本 |
| `ui.text_at(x,y,text[, color[, size]])` | 画文本 |
| `ui.rect_filled(x,y,w,h[, color[, rounding]])` | 填充矩形 |
| `ui.rect(x,y,w,h[, color[, thickness[, rounding]]])` | 描边矩形 |
| `ui.line(x1,y1,x2,y2[, color[, thickness]])` | 画线 |

每帧图元数量控制在几十个以内；文本不自动换行。

## 7. 状态与日志

GUI 中每个插件显示：名称/版本、API、状态、update/draw 计数、错误数；自动禁用时显示「已自动禁用」。

日志：`<framework>\logs\framework\slmodf-*.log`，关键行：

```
plugin| scan root=...\SLModFramework\plugins
plugin| manifest dir=... id=... name=... version=... api=1 enabled=1 entry=...\main.lua
plugin| loaded id=... api=1 ... callbacks load=1 update=1 draw=1 overlay=1 shutdown=1
plugin| summary total=1 loaded=1 disabled=0 api_mismatch=0 error=0 invalid=0
plugin| ERROR id=... on_update: ... (errors=N) consec=...
plugin| AUTO-DISABLED id=... after 30 consecutive errors ...
```

## 8. 完整示例（HelloPlugin）

```lua
local updates = 0

function plugin_info()
    return { id = "demo.hello", name = "Hello Plugin", version = "0.1.0", api_version = 1 }
end

function on_load(ctx)
    sl.log("loaded with framework " .. ctx.framework_version)
    updates = 0
end

function on_update(ctx)
    updates = updates + 1
end

function on_draw_ui(ui)
    ui.text("Hello Plugin v0.1.0")
    ui.text("updates: " .. updates)
    if ctx and ctx.game and ctx.game.supported then
        ui.text(string.format("scene %d: %s", ctx.game.scene_id, ctx.game.scene_name))
    end
end
```

> 注意：`on_draw_ui` 只收到 `ui`，需要 `ctx` 时在 `on_update` 里存到插件自己的变量（上例为简化省略）。

## English summary

Plugins live in `<game>\SLModFramework\plugins\<dir>\` with `plugin.ini` and a Lua entry script. API version is 1. `plugin_info()` is mandatory. Available callbacks: `on_load(ctx)`, `on_update(ctx)`, `on_draw_ui(ui)`, `on_draw_overlay(ui)`, `on_shutdown()`. The sandbox exposes read-only game state (`ctx.game`), audio probe helpers (`ctx.audio`) and ImGui wrappers (`ui`). No file/OS/memory-write access. Runtime errors are caught; 30 consecutive failures auto-disable the plugin and write `enabled=0`.

# SLModFramework 插件 API v0（Phase 4 起；Phase 7 增补 overlay 绘制）

状态：**已完成并实测**（2026-10-25 21:14；2026-10-26 01:48 起 `ctx.game` 接入 Phase 5 的 GameAdapter 只读状态；2026-10-26 04:0x 起 `ctx.audio` 接入 Phase 6 音频探针）。适用框架版本 `0.4.0-p5`；插件 API 版本 `SLMODF_PLUGIN_API_VERSION = 1`。
对应计划：v1.4 Phase 4（配置/插件宿主/API v0）。证据等级标注沿用 Workspace 约定：[实测] / [推断] / [待实机]。

命名说明：计划里叫 "API v0"，指的是 Phase 4 这一版插件接口；实际对外版本号是整数 **1**（清单与 `plugin_info()` 都写 `api_version=1`）。未来破坏性变更会升到 2，并同时保留旧插件诊断（状态=API 不匹配，不加载）。

## 1. 插件目录与清单

每个插件一个子目录，放在：

```
<game>\Workspace\plugins\<插件目录>\
  plugin.ini     # 清单（必需）
  main.lua       # 入口脚本（默认；可由 entry= 改）
```

`plugin.ini` 字段（`key=value`，`#`/`;` 开头为注释，`[节]` 行被忽略）：

| 字段 | 必需 | 说明 |
| --- | --- | --- |
| `id` | 是 | 稳定 ID（建议 `作者.名字`），日志与去重依据 |
| `name` | 否 | 显示名；缺省回退 `id` |
| `version` | 否 | 插件版本；缺省 `0.0.0` |
| `api_version` | 是 | 必须等于框架支持的整数（当前 **1**）；缺失=清单无效 |
| `enabled` | 否 | `1`/其它非零=启用；`0`=禁用（缺省启用） |
| `entry` | 否 | 入口 .lua 文件名，缺省 `main.lua`；相对插件目录 |

清单解析 [实测]：`plugin_host.cpp::ParseManifest`；缺 `id`/`api_version` → 状态 `kInvalid`，错误文本 `manifest: missing id/api_version`。

框架扫描根目录：`<game>\Workspace\plugins`（由 `g_fw.pluginsDir` 推导：`<logDir>\..\..\plugins`）。最多 **32** 个插件（`MAX_PLUGINS`），超出打 `plugin| WARN plugin cap 32 reached`。

## 2. 沙箱

每个插件在**同一个 Lua 5.4 状态**中运行，但各自有独立的 `_ENV` 表（全局只读视图，写全局不会污染别的插件）。[实测]

可用标准库：`base`、`table`、`string`、`math`、`coroutine`、`utf8`。
**不可用**：`io`、`os`、`package`、`debug`，以及被显式置 nil 的 `dofile`、`loadfile`、`load`、`require`。插件不能读写文件、不能加载外部模块、不能 `os.execute`。

宿主注入的全局 `sl`：

| 字段/函数 | 说明 |
| --- | --- |
| `sl.api` | 接口代号字符串 `"0.1"` |
| `sl.api_version` | 整数 1 |
| `sl.framework_version` | 框架版本字符串（`0.3.0-p4`） |
| `sl.log(msg[, level])` | 写框架日志 `plugin| [level] msg`，level 缺省 `info` |
| `sl.warn(msg)` | 同上（当前实现与 log 等价，level 固定 info） |

设计边界（计划 §0/§1）[实测]：v0 **不开放**任意内存读写、不开放 Hook 注册、不开放文件写。后续阶段的 GameAdapter 也只通过受控字段暴露游戏状态。

## 3. 生命周期

1. **扫描/加载**（懒加载：第一个 Present 帧，渲染线程）：`plugins::EnsureLoaded()` 扫根目录 → 读清单 → 版本检查 → 建沙箱环境 → 加载 chunk → 调 `plugin_info()`。
2. **`plugin_info()`（必需）**：返回表，字段 `id/name/version/api_version`；会覆盖清单里的同名字段。缺函数或不返回表 → `kError`。
3. **`on_load(ctx)`（可选）**：加载成功、API 版本校验通过后调用一次；抛错 → 插件置 `kError`（`disabled after on_load error`），不影响其它插件。
4. **`on_update(ctx)`（可选）**：每帧一次（ImGui::NewFrame 之前），参数同上文的 `ctx`。
5. **`on_draw_ui(ui)`（可选）**：每帧一次，**在框架状态面板内部**绘制；宿主已 `PushID(插件序号)`，可用 `ui.*` 画控件。该回调只在 GUI 可见时执行（面板打开时）。
6. **`on_draw_overlay(ui)`（可选，Phase 7 新增）**：每帧一次，**在屏幕前景**绘制，不要求 GUI 打开；参数 `ui` 提供 §5.1 的前景原语。宿主在 `DrawHud()` 之后、`ImGui::Render()` 之前调用，因此预警会盖在 GUI 之上；同样 `PushID(插件序号)`。
7. **`on_shutdown()`（可选）**：插件被禁用/重载/框架卸载时调用一次。

回调全部在**渲染线程**执行（D3D9 设备所在线程），插件里不要做长时间阻塞操作（会掉帧）。Lua 状态单线程使用，无需加锁。

状态枚举与含义：

| status | 名称 | 触发条件 |
| --- | --- | --- |
| 0 | 已禁用 `kDisabled` | 清单 `enabled=0`，不加载脚本 |
| 1 | 已加载 `kLoaded` | 加载成功并 `plugin_info` 校验通过 |
| 2 | API 不匹配 `kApiMismatch` | 清单或 `plugin_info` 的 `api_version` ≠ 1 |
| 3 | 错误 `kError` | 语法错、入口缺失、`plugin_info` 缺/异常、`on_load` 异常 |
| 4 | 清单无效 `kInvalid` | `plugin.ini` 缺失/缺必需字段 |

## 4. `ctx`（on_load / on_update 参数）

| 字段 | 类型 | 当前值/说明 |
| --- | --- | --- |
| `ctx.framework_version` | string | `0.4.0-p5` |
| `ctx.game.supported` | bool | `true` = 版本门禁通过且当前帧已被 scene 模块识别；否则 `false`，插件**不得**生成肯定性判断 |
| `ctx.game.frame_id` | int | 框架自增帧号（Present 帧） |
| `ctx.game.scene_id` | int | 当前帧索引（0–35，见 `analysis/frame-index.md`）；未知 `-1` |
| `ctx.game.scene_name` | string | 帧名（如 `Breaker Room`、`Final Encounter`）；未知空串 |
| `ctx.game.scene_width` / `scene_height` | int | 帧逻辑尺寸（像素）；未知 0 |
| `ctx.game.counters_ready` | bool | `true` = state 模块本帧的 OI/对象映射 + 计数器值已就绪 |
| `ctx.game.counters` | table | 计数器名→整数快照（最多 64 项；名字来自游戏 OI，可能重名，重名时后者覆盖前者）。仅 `counters_ready` 时有内容 |
| `ctx.game.fps` | number | 最近 500 ms 估算 FPS |
| `ctx.game.presents` | int | 本进程 Present 累计次数 |
| `ctx.game.objects_ready` | bool | `true` = state 模块本帧已建立完整对象表（与 alterables 同时就绪） |
| `ctx.game.alterables` | table | 按对象名索引的 Active alterable 快照（0-based 下标）；当前只监视 `Ennard` / `flip it`，见 §4.2 |
| `ctx.game.alterable(name, i)` | function → int / nil | **v0.5.0** 实时读取任意 Active 对象的第 i 个 alterable（不限于监视名单；失败返回 nil），见 §4.3 |
| `ctx.game.object_rect(name)` | function → `{x,y,w,h}` / nil | **v0.5.0** 读取任意 Active 对象的场景坐标与尺寸（支持事件动态创建的对象），见 §4.3 |

[实测] 来源 `plugin_host.cpp::PushGameContext`（2026-10-26 01:48 起接入 scene/state）；实机 pid 28348 `HelloPlugin` 日志：`scene=9:Breaker Room counters=[Freddy=0 got you=0 danger level 4=1]`、`scene=27:Final Encounter counters=[got you=0 final stage=0 hour=12 power left=99 side=2]`；同一会话后段 `final stage=2 … power left=86 side=1` 说明数值随 state 刷新。计数器读取原理见 `docs/RUNTIME_STATE.md` §7/§9。

### 4.1 `ctx.audio`（Phase 6 音频探针；2026-10-26 起）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `ctx.audio.supported` | bool | 探针已安装（"audio_probe=1" 且 hook 成功）。false 时**不得**调用其余字段/函数做任何音频判断 |
| `ctx.audio.soften_enabled` | bool | 当前框架级柔化开关（可能来自 config 或任一插件调用 `set_soften`） |
| `ctx.audio.gain_percent` | int | 当前目标音量百分比（1..100；25 ≈ -12 dB） |
| `ctx.audio.last.has` | bool | 是否已有匹配样本 |
| `ctx.audio.last.name` / `.handle` | string / int | 最近匹配样本名（如 `scream op5-6`）与游戏 sound handle |
| `ctx.audio.last.chunk` / `.scream` | int / bool | 匹配到的流式 chunk 序号；是否 scream 样本 |
| `ctx.audio.last.helper` / `.how` / `.plays` / `.seq` | bool / int / int / int | 是否为 helper 路径、匹配方式（3=chunk 指纹 / 5=ambiguous）、Play 计数、匹配序号 |
| `ctx.audio.stats.creates` / `.plays` / `.matched` / `.screams` | int | 探针累计：CreateSoundBuffer / Play / 指纹命中 / scream 命中 |
| `ctx.audio.set_soften(enabled, gain_percent)` | function | 设置框架级柔化；只对已识别的 scream 生效，普通音效不动 |
| `ctx.audio.reset_soften()` | function | 恢复启动时 config 的 `audio_soften`/`audio_gain_percent`（不是"关闭"） |
| `ctx.audio.set_names({...})` | function | 设置白名单（最多 16 个名字，大小写不敏感）；只有名单内样本会被柔化 |
| `ctx.audio.clear_names()` | function | 清空白名单 = 所有已识别 scream 都柔化（默认） |

说明：
- `ctx.audio.supported` 只反映探针是否安装，与当前场景无关；是否 scream 由 `last.*`/内部匹配决定。
- 这些函数可以在 `on_load` 调用；`on_draw_ui(ui)` 只收到 `ui` 表，因此需要把 `on_update(ctx)` 里的 `ctx.audio` 保存到插件自己的变量再用（见 `plugins/SoftJumpscares/main.lua`）。
- 实测：pid 20980 加载 SoftJumpscares → 日志 `audio| plugin set_soften=1 gain=25%`；帧 27 触发 `scream op5-6` 后同毫秒出现 `audio| soften … gain=25% off_cb=-1204 (readback=-1204)`；GUI 截图 `logs/probe/p6e-plugin-ui.png`。

### 4.2 `ctx.game.alterables` 与 `objects_ready`（Phase 7 新增）

`ctx.game.alterables` 的上层键是对象名，值是 0-based 的 alterable 索引表；当前 state 模块只监视两个对象（`Ennard`、`flip it`），其余对象不入表以控制每帧开销。对象表尚未建立、或发生换帧/内存复用重建时，`objects_ready=false`、`alterables` 为空/缺项——**此时不得下任何威胁结论**。

```lua
if ctx.game.objects_ready and ctx.game.alterables and ctx.game.alterables.Ennard then
    local st = ctx.game.alterables.Ennard[3]   -- 0..10 / 99，含义见 docs/THREAT_RULES.md
end
```

[实测 pid 24336] `state|` 行记录 `Ennard[3] 0 -> 1 -> 0 -> 5 -> 6 -> 99`，插件在同一帧读到相同值；DebugInspector 面板显示 `Ennard[0..3]: 1 1 0 6` 时预警为高警“通风口”。`flip it[0]` 是死亡链分支条件（2=分支A，0=分支B），当前只读取、不写入。

### 4.3 `ctx.game.alterable()` 与 `ctx.game.object_rect()`（v0.5.0 新增）

两个函数用于读取**任意 Active 对象**的实时状态，解决两类问题：①帧 31 Freddy 的精确阶段（`[6]`）/侧别（`[0]`）不在 `alterables` 白名单；②`Active 19`（Bonnet）、`Bonnie scare` 等**事件运行时创建**的对象不在 Published 对象表。

| 函数 | 返回 | 实现 |
| --- | --- | --- |
| `ctx.game.alterable(name, i)` | int 或 nil | `state::ReadAlterableLive`：按对象名找对象 → `hoOffsetValue@0x80` → `rVal` → `CValue`（16 字节 stride，type 必须 0，值在 +8） |
| `ctx.game.object_rect(name)` | `{x,y,w,h}` 或 nil | `state::ReadObjectRect`：`hoX@0x4C`、`hoY@0x54`（int 变体）、`hoImgWidth@0x60`、`hoImgHeight@0x64` |

对象查找顺序：先在 Published 对象表（场景切换时的快照）里找；找不到时回退 **RunHeader 对象链表**（`rh+0x1278`、`rhNObjects@0x3C`，每项 8 字节 `{objPtr, routine}`，用 `hoOi@0x12` 查 OI 名；上限 4096）。回退命中时框架写日志 `state| live rect fallback name=Active 19 obj=... x=... y=... w=... h=...`（启动后前 8 次）。

```lua
local phase = ctx.game.alterable("Freddy", 6)   -- 0 idle / 1,2,4 语音 / 3 到同侧门 / 5 跨对侧门
local r = ctx.game.object_rect("Active 19")      -- Bonnet 行走体：x 随时间减小
if phase and r then ... end
```

注意/限制：
- 读不到时返回 nil，**不得**把 nil 当作 0；不得用本 API 写内存（写入由框架 QoL 层负责，插件无写权限）。
- `object_rect` 是**场景/投影坐标**，不是屏幕像素；跨分辨率/DPI 使用时要按场景宽高换算（帧 31 场景 1900×800）。
- 动态对象回退依赖 Fusion 运行时布局（build 286 实测 `rhObjectList=0x1278`、`rhNObjects=0x3C`）；版本不符时函数返回 nil 并只在启动日志提示，不尝试其它偏移。
- 实测：pid 26784 帧 31 `state| live rect fallback name=Active 19 obj=1D3E7838 x=1581 y=828 w=700 h=700`；pid 23520 `ctx.game.alterable("Freddy",6)` 读到 3 时插件输出 `alert=high dir=left reason=Freddy alt6=3 alt0=1 closed=0`。插件实现见 `plugins/ThreatTelegraphs/main.lua`（`altv()` / `obj_rect()`），行规见 `docs/THREAT_RULES.md` §17。

## 5. `ui`（on_draw_ui / on_draw_overlay 参数）

| 函数 | 对应 ImGui |
| --- | --- |
| `ui.text(s)` | `TextUnformatted(s)` |
| `ui.text_disabled(s)` | `TextDisabled` |
| `ui.separator()` | `Separator` |
| `ui.same_line()` | `SameLine` |
| `ui.checkbox(label, value)` → bool | `Checkbox`；返回勾选后的布尔值 |
| `ui.slider_int(label, value, min, max)` → int | `SliderInt`；返回滑动后的整数（P6 新增，1..100 用于音量） |
| `ui.button(label)` → bool | `Button`；本帧被点击返回 true（P6 新增） |

v0 共 7 个面板原语（文本/禁用文本/分隔/同行/复选框/整数滑条/按钮），足以做状态显示、小开关与参数调节；更复杂的控件等 API v1 再评估。

### 5.1 `on_draw_overlay(ui)` 的前景原语（Phase 7 新增）

`on_draw_overlay(ui)` 收到的 `ui` 表**不是**面板控件表，而是前景 DrawList 原语（坐标单位为物理客户区像素，原点左上）；颜色是 ImU32（可用 `IM_COL32(r,g,b,a)` 或 ABGR 整数，缺省白色）：

| 函数 | 说明 |
| --- | --- |
| `ui.viewport()` → `w,h` | 当前客户区像素尺寸（缩放/分辨率变化会自动反映） |
| `ui.text_size(text[, size])` → `w,h` | 测量文本尺寸（size 为像素字号，缺省 22） |
| `ui.text_at(x,y,text[, color[, size]])` | 在 (x,y) 画文本 |
| `ui.rect_filled(x,y,w,h[, color[, rounding]])` | 填充矩形（可圆角） |
| `ui.rect(x,y,w,h[, color[, thickness[, rounding]]])` | 描边矩形 |
| `ui.line(x1,y1,x2,y2[, color[, thickness]])` | 画线 |

示例（来自 `plugins/ThreatTelegraphs/main.lua`）：

```lua
function on_draw_overlay(ui)
    local w, h = ui.viewport()
    local s = h / 1440.0
    local tw, th = ui.text_size("通风口", 22 * s)
    ui.rect_filled(w * 0.5 - tw/2 - 8*s, h * 0.145 - th/2 - 8*s, tw + 16*s, th + 16*s,
                   0xCC0000CC, 5*s)
    ui.text_at(w * 0.5 - tw/2, h * 0.145 - th/2, "通风口", 0xFF3030FF, 22 * s)
end
```

限制：只支持上述图元（没有 shader/图片/中文字体外的字体控制）；文本不做自动换行；每帧绘制量应保持在几十个图元以内。

## 6. 启用/禁用与持久化

- 框架 GUI "框架/插件" 区：每个插件一行复选框 → `plugins::SetEnabled(i, enabled)` → **原子替换**清单文件（写 `plugin.ini.tmp` 再 `MoveFileEx`）→ 请求重载（`RequestReloadPlugins`）。日志：`plugin| set enabled=0 id=… manifest=…`、`plugin| reload requested (had N entries)`、`summary …`。
- 也可以直接编辑 `plugin.ini` 的 `enabled=0/1`，重启游戏（或点 GUI"重新扫描插件"）生效。
- 禁用时若插件有 `on_shutdown`，会先调用再卸载 [实测：`HelloPlugin shutdown; updates=1153 draws=619`]。
- 重启后状态从清单恢复 [实测 pid 20904：`plugin| manifest … enabled=0` → `disabled by manifest`]。

## 7. 错误处理与限流

- 所有 Lua 调用走 `lua_pcall`；错误被捕获并记到该插件 `errors`、`error`，连续错误不会弹出/终止游戏。
- 日志限流：每个插件前 3 次错误逐条记录，之后每 600 次记录一条（`plugin| ERROR id=… on_update: … (errors=N)`）[实测：fixture runtime 3 帧即 errors=3]。
- 加载期错误（语法/清单/plugin_info）只影响该插件；其他插件继续加载（离线 harness 6 个夹具同时验证）[实测]。
- 插件目录缺失/为空：日志 `plugin| root missing or empty …`，状态栏显示 `root missing`，不视为框架错误。
- 修复插件后：GUI 点"重新扫描插件"即可重载全部插件（无需重启游戏）。

## 8. 日志关键行（`Workspace/logs/framework/slmodf-*.log`）

```
plugin| scan root=…\Workspace\plugins
plugin| manifest dir=… id=… name=… version=… api=1 enabled=1 entry=…\main.lua
plugin| loaded id=… name=… version=… api=1 entry=… callbacks load=1 update=1 draw=1 overlay=1 shutdown=1
plugin| summary total=1 loaded=1 disabled=0 api_mismatch=0 error=0 invalid=0
plugin| [info] …（插件 sl.log 输出）
plugin| set enabled=0 id=… manifest=…
plugin| disabled by manifest id=… / plugin| disabled after on_load error id=…
plugin| ERROR id=… on_update: … (errors=3)
plugin| MISMATCH id=… api: plugin api_version=99, framework supports 1
plugin| Lua 5.4 state created (safe libs only: base/table/string/math/coroutine/utf8)
```

## 9. 最小示例

`plugin.ini`：

```ini
id=example.hello
name=Hello Plugin
version=0.1.0
api_version=1
enabled=1
entry=main.lua
```

`main.lua`：

```lua
local updates = 0
local checked = false

function plugin_info()
    return { id = "example.hello", name = "Hello Plugin", version = "0.1.0", api_version = 1 }
end

function on_load(ctx)
    sl.log("loaded; framework=" .. tostring(ctx.framework_version))
end

function on_update(ctx)
    updates = updates + 1
    -- GameAdapter v0: 只读场景 + 计数器快照（未就绪时不要下结论）
    local danger = "?"
    if ctx.game.counters_ready then
        danger = tostring(ctx.game.counters["danger level 4"])
    end
    if updates % 60 == 1 then
        sl.log(string.format("update #%d scene=%d:%s counters_ready=%s danger4=%s",
                             updates, ctx.game.scene_id, ctx.game.scene_name,
                             tostring(ctx.game.counters_ready), danger))
    end
end

function on_draw_ui(ui)
    ui.text("Hello Plugin  update=" .. tostring(updates))
    checked = ui.checkbox("示例开关", checked)
    ui.text("当前: " .. tostring(checked))
end

function on_shutdown()
    sl.log("bye; updates=" .. tostring(updates))
end
```

可运行样板：`Workspace/plugins/HelloPlugin/`（最小生命周期与中文 UI）与 `Workspace/plugins/SoftJumpscares/`（ctx.audio 实战：开关+滑条+白名单+reset）。SoftJumpscares 的要点：

```lua
local audio  -- 从 on_update(ctx) 保存，on_draw_ui(ui) 使用
function on_load(ctx)
    if ctx.audio and ctx.audio.supported then
        ctx.audio.set_soften(true, 25)          -- 只对已识别 scream 生效
        ctx.audio.clear_names()                  -- 默认全部 scream
    else
        sl.warn("audio probe not installed: set audio_probe=1 and restart")
    end
end
function on_update(ctx) audio = ctx.audio end
function on_draw_ui(ui)
    if not audio or not audio.supported then return end
    local en = ui.checkbox("柔化 jumpscare 尖叫", audio.soften_enabled)
    if en ~= audio.soften_enabled then audio.set_soften(en, audio.gain_percent) end
    local gain = ui.slider_int("目标音量 (%)", audio.gain_percent, 1, 100)
    if gain ~= audio.gain_percent then audio.set_soften(true, gain) end
end
```

## 10. 验证记录（2026-10-25 / 10-26）

| 场景 | 结果 | 证据 |
| --- | --- | --- |
| 离线夹具（6 个：good/mismatch/syntax/disabled/runtime/sandbox） | `SUMMARY4 pass=1 total=6 vtx=2586 good=1/3/0 mismatch=1 syntax=1 disabled=1 runtime_errors=3 sandbox=1/0` | `logs/framework/harness-20261025-205954.log`；命令 `harness32.exe --plugins` |
| 真实游戏加载 HelloPlugin | 面板 `插件 API v1 | total=1 loaded=1`；插件自绘区显示 update/draw/frame_id，中文正常 | pid 3588（21:00）；`logs/probe/p4-plugins-gui.png` |
| GUI 内禁用 → 写回清单 | `plugin| set enabled=0` → `disabled by manifest` → summary disabled=1；`plugin.ini` 变 `enabled=0` | pid 23104（21:11）；`logs/probe/p4-plugin-disabled.png` |
| 重启后保持禁用 | `manifest … enabled=0` → `disabled by manifest` | pid 20904（21:12） |
| GUI 内重新启用 | `plugin| loaded …` → summary loaded=1；清单回 `enabled=1` | 21:12:28 点击（`imgui click at 86.0,809.0`） |
| 最终构建复测（去掉调试日志） | 21:13:53 disable → disabled=1；21:14:03 enable → loaded=1；Present 2765/FPS 60.0 | pid 19188；`logs/probe/p4-final-plugin-disabled.png`、`p4-final-plugins.png` |
| Alt+Tab 焦点切换（GUI 打开） | presents 237→2038、fps 58.0、device 不变、无 DEVICELOST | pid 19188（21:14）；`logs/framework/slmodf-20261025-211338-19188.log` |
| SoftJumpscares 插件（ctx.audio） | 插件加载并 `audio\| plugin set_soften=1 gain=25%`；帧 27 强制死亡链上 `scream op5-6` 被柔化（readback -1204）；插件 UI（开关/滑条/白名单/统计）正常 | pid 20980（2026-10-26 03:56）；`logs/framework/slmodf-20261026-035632-20980.log`、`logs/probe/p6d-plugin-report.txt`、`logs/probe/p6e-plugin-ui.png` |
| 插件的 `ctx.audio` 在真实 GUI 自绘区 | 显示 `探针: true \| 柔化: true \| 目标: 25%`，播放/匹配/尖叫计数随游戏刷新 | pid 28380（2026-10-26 03:58）；`logs/probe/p6e-plugin-ui.png` |
| ThreatTelegraphs 帧 27 实机预警 | `Ennard[3]` 0→1→0→5→6→99；低警/高警/攻击日志与截图齐全；高警时门未关显示“通风口 + 关门” | pid 24336（2026-10-26 06:06–06:10）；`logs/framework/slmodf-20261026-060601-24336.log`、`logs/probe/p7-threat-high-vent-24336.png`、`p7-gui-scroll-24336.png`；规则见 `docs/THREAT_RULES.md` |
| `on_draw_overlay` 绘制路径 | 插件回调发现 `callbacks ... overlay=1`；GUI 关闭/打开均绘制（高警截图 GUI 打开时仍在 GUI 之上） | 同上 |

复现（离线）：

```bash
"/d/Scoop/apps/git/current/bin/bash.exe" Workspace/framework/tests/build_harness.sh
cd Workspace/framework/bin && ./harness32.exe --plugins     # rc=0 = PASS
```

复现（真实游戏，Windows 会话必须已解锁）：

```bash
Workspace/framework/bin/launcher32.exe --test-exit 40 --toggle-gui 10
```

## 11. 已知限制与下一步

- `ctx.game` 已是真实只读状态（Phase 5 GameAdapter v0）：`supported=false` 或 `counters_ready=false` 时插件必须显示"未知"，不得当作安全/危险；scene 快速路径失效时 `scene_id` 可能短暂为 `-1`（最长约 1 s）。
- 计数器名字可能重复（如 Breaker Room 的 16 个 `compute`），Lua 表里只保留最后一个；需要精确区分请等 API v1 的按 OI 句柄访问。
- 插件仍拿不到任何原始地址/指针，也不提供写内存、Hook、文件写接口（沙箱：base/table/string/math/coroutine/utf8）。
- 插件不能跨插件共享 Lua 状态（各自环境）；如需协作，后续提供框架级服务/事件（API v1 评估）。
- `on_draw_ui` 只在 GUI 打开时执行；常驻 HUD/预警请用 Phase 7 的 `on_draw_overlay(ui)`（不依赖 GUI，原语见 §5.1）。面板原语与前景原语是两套 `ui` 表，不能混用。
- `ctx.audio` 需要 config `audio_probe=1` 且**重启游戏**才可用（探针在启动早期安装）；默认 `audio_probe=0` 时插件应显示"未安装"而不是报错。
- 柔化是框架级全局状态：任一插件调用 `set_soften` 会改变所有已识别 scream 的下一帧到音量；当前没有 per-sample gain 或事件回调（API v1 评估）。
- 白名单最多 16 个名字、按样本名精确匹配（大小写不敏感）；空名单 = 全部 scream。
- `ctx.audio.last` 是最近一次匹配（last-wins），高频播放时可能跳过中间样本；需要完整事件流需 API v1。
- 插件只在渲染线程跑，单帧耗时过长会拖慢 Present；后续可加 per-plugin 耗时统计与超时告警。
- Lua 5.4.7 许可证 MIT，vender 位置 `Workspace/third_party/lua-5.4.7`，随 DLL 静态链接；分发 DLL 时需附 MIT 声明（P8 许可证矩阵）。

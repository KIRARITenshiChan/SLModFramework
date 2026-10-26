-- HelloPlugin: Phase 4 API v0 smoke test plugin.
-- Only the framework API is available: no io/os/package/debug, no loadfile,
-- no require. sl.log() writes into the framework log.

local updates = 0
local draws = 0
local checked = false
local last_frame = -1
local last_scene = -1
local last_scene_name = ""
local last_counters = "{}"

function plugin_info()
    return {
        id = "example.hello",
        name = "Hello Plugin",
        version = "0.1.0",
        api_version = 1,
    }
end

function on_load(ctx)
    sl.log("HelloPlugin loaded; framework=" .. tostring(ctx.framework_version) ..
           " api=" .. tostring(sl.api_version) .. " game_supported=" .. tostring(ctx.game.supported))
end

function on_update(ctx)
    updates = updates + 1
    last_frame = ctx.game.frame_id
    last_scene = ctx.game.scene_id
    last_scene_name = ctx.game.scene_name or ""
    -- GameAdapter v0: ctx.game.counters is a name->value snapshot (only when
    -- supported and counters_ready). Show a few scene-specific values below.
    local parts = {}
    if ctx.game.counters_ready then
        for _, key in ipairs({ "option", "Freddy", "got you", "final stage", "hour",
                               "power left", "side", "danger level 4" }) do
            local v = ctx.game.counters[key]
            if v ~= nil then parts[#parts + 1] = key .. "=" .. tostring(v) end
        end
    end
    last_counters = table.concat(parts, " ")
    if updates % 60 == 1 then
        sl.log(string.format("HelloPlugin update #%d frame_id=%d fps=%.1f presents=%d scene=%d:%s counters=[%s]",
                             updates, ctx.game.frame_id, ctx.game.fps, ctx.game.presents,
                             last_scene, last_scene_name, last_counters))
    end
end

function on_draw_ui(ui)
    draws = draws + 1
    ui.text("Hello Plugin v0.1.0  (" .. tostring(_VERSION) .. ", api " .. tostring(sl.api) .. ")")
    ui.text_disabled("on_update 次数: " .. tostring(updates) ..
                     " | on_draw_ui 次数: " .. tostring(draws) ..
                     " | 最近 frame_id: " .. tostring(last_frame))
    ui.text("场景: [" .. tostring(last_scene) .. "] " ..
            (last_scene_name ~= "" and last_scene_name or "<未知>"))
    ui.text_disabled("GameAdapter v0 计数器: " .. last_counters)
    checked = ui.checkbox("示例复选框 (插件自己的状态)", checked)
    ui.text("复选框是否勾选: " .. tostring(checked))
end

function on_shutdown()
    sl.log("HelloPlugin shutdown; updates=" .. tostring(updates) .. " draws=" .. tostring(draws))
end

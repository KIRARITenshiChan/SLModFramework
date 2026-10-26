function plugin_info()
    return { id = "test.good", name = "Good Plugin", version = "1.0.0", api_version = 1 }
end

local n = 0
function on_load(ctx) sl.log("fixture good on_load") end
function on_update(ctx) n = n + 1 end
function on_draw_ui(ui)
    ui.text("GoodPlugin OK updates=" .. tostring(n))
    ui.checkbox("good-checkbox", false)
end
function on_shutdown() sl.log("fixture good on_shutdown") end

function plugin_info()
    return { id = "test.runtime", name = "Runtime Error Plugin", version = "0.1.0", api_version = 1 }
end
function on_update(ctx) error("boom from fixture") end
function on_draw_ui(ui) ui.text("RuntimeError plugin still draws") end

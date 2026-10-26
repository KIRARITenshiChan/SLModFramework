function plugin_info()
    return { id = "test.disabled", name = "Disabled Plugin", version = "0.1.0", api_version = 1 }
end
function on_load(ctx) error("disabled plugin must not load") end

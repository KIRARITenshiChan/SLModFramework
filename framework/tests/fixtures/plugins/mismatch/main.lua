function plugin_info()
    return { id = "test.mismatch", name = "API Mismatch Plugin", version = "2.0.0", api_version = 99 }
end
function on_update(ctx) error("should never run") end

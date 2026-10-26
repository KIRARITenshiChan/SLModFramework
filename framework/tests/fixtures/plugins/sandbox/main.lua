-- Fails loudly if the sandbox leaks a dangerous global.
local forbidden = { "io", "os", "package", "debug", "dofile", "loadfile", "load", "require" }

function plugin_info()
    return { id = "test.sandbox", name = "Sandbox Probe Plugin", version = "0.1.0", api_version = 1 }
end

function on_load(ctx)
    local leaks = {}
    for i = 1, #forbidden do
        local name = forbidden[i]
        if _G[name] ~= nil then leaks[#leaks + 1] = name end
    end
    if #leaks > 0 then error("sandbox leak: " .. table.concat(leaks, ",")) end
    sandbox_ok = true
    sl.log("fixture sandbox on_load: no forbidden globals visible")
end

local ticks = 0
function on_update(ctx) ticks = ticks + 1 end
function on_draw_ui(ui)
    ui.text("SandboxProbe OK ticks=" .. tostring(ticks) .. " sandbox_ok=" .. tostring(sandbox_ok))
end

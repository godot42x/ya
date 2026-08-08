-- ============================================================================
-- Engine source root.
--
-- Source is organized into three product tiers, each module living in its own
-- directory with a self-describing xmake.lua:
--
--   Foundation/   shared infrastructure (Core / RHI / RHI+Backend)
--   Framework/    product lines (GUI framework; Game: scene/resource/render/
--                 gameplay/physics)
--   Product/      assembled products (Host runtime shell / Editor)
--
-- This file only provides one thin helper for the genuinely shared parts
-- (export-macro injection + unity rule) and wires the tier directories up.
-- Everything module-specific -- target name, kind, sources, deps, packages,
-- include roots, exclusions -- is written explicitly in each module's own
-- xmake.lua so the build shape is readable without jumping through helpers.
-- ============================================================================

--- Injects the shared module plumbing into the current target:
---   * the module's own export macro (api_macro) as a preprocessor alias to
---     YA_API_EXPORT -- no hand-written macros, no central macro table;
---   * build/import side switches (YA_SHARED / YA_MODULE_BUILD);
---   * the unity-build rule (module sources still need add_files("**.cpp")).
--- @param api_macro string e.g. "YA_CORE_API"
function ya_std_module(api_macro)
    add_defines("YA_SHARED=1")
    add_defines("YA_MODULE_BUILD=1")
    add_defines("YA_SHARED=1", { public = true })
    add_defines(api_macro .. "=YA_API_EXPORT", { public = true })
    if get_config("ya_enable_unity-build") then
        add_rules("c++.unity_build", { batchsize = 2 })
    end
end

--- Injects every module export macro into the aggregate (ya-engine) so its
--- precompiled header can parse any engine header. Only the aggregate needs
--- this; modules get their single macro via ya_std_module().
function ya_engine_defines()
    local macros = {
        "YA_CORE_API", "YA_RHI_API", "YA_RHI_BACKEND_API", "YA_GUI_API",
        "YA_SCENE_3D_API", "YA_RESOURCE_API", "YA_RENDER_GRAPH_API",
        "YA_RENDER_3D_API", "YA_GAMEPLAY_ECS_API", "YA_PHYSICS_API",
        "YA_HOST_API", "YA_EDITOR_API",
    }
    for _, macro in ipairs(macros) do
        add_defines(macro .. "=YA_API_EXPORT")
    end
end

-- Foundation tier: shared infrastructure consumed by every product line.
includes("./Foundation/Core/xmake.lua")
includes("./Foundation/RHI/xmake.lua")

-- Framework tier: product lines. GUI framework first (self-contained);
-- Game depends on Foundation + GUI (Node scene-tree base lives in GUI).
includes("./Framework/GUI/xmake.lua")
includes("./Framework/Game/Scene/Scene3D/xmake.lua")
includes("./Framework/Game/Resource/xmake.lua")
includes("./Framework/Game/Render/Graph/xmake.lua")
includes("./Framework/Game/Render/Render3D/xmake.lua")
includes("./Framework/Game/Gameplay/ECS/xmake.lua")
includes("./Framework/Game/Physics/xmake.lua")

-- Product tier: assembled runtimes.
includes("./Product/Host/xmake.lua")
includes("./Product/Editor/xmake.lua")

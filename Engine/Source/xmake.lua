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

-- Engine/Source, captured before module files are included below.
local YA_SOURCE_ROOT = os.scriptdir()

-- Product-tier include roots keyed by short name. Headers are addressed by
-- module-name prefix (`Core/Base.h`, `GUI/Scene/Node2D.h`, `Host/App.h`,
-- `Scene/Core/Scene.h`, ...) instead of full physical paths under Engine/Source.
local YA_TIER_ROOTS = {
    Foundation = "Foundation",
    Framework  = "Framework",
    Game       = "Framework/Game",
    Scene      = "Framework/Game/Scene",
    Render     = "Framework/Game/Render",
    Gameplay   = "Framework/Game/Gameplay",
    Product    = "Product",
}

--- Exposes the product-tier include roots of THIS module (public, so the
--- roots propagate to dependents through add_deps). A module only declares
--- the tier it physically owns; other modules' tiers arrive via deps.
--- @param ... string tier names, e.g. ya_tier_include("Foundation", "Framework")
function ya_tier_include(...)
    for _, tier in ipairs({ ... }) do
        add_includedirs(path.join(YA_SOURCE_ROOT, YA_TIER_ROOTS[tier]), { public = true })
    end
end

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
        "YA_SCENE_CORE_API", "YA_SCENE_RUNTIME_API", "YA_SCENE_SERIALIZATION_API",
        "YA_SCENE_3D_API", "YA_RESOURCE_API", "YA_RENDER_GRAPH_API",
        "YA_RENDER_3D_API", "YA_ECS_CORE_API", "YA_GAMEPLAY_ECS_API", "YA_GAMEPLAY_ANIMATION_API", "YA_PHYSICS_API",
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
includes("./Framework/Game/Scene/Core/xmake.lua")
includes("./Framework/Game/Scene/Runtime/xmake.lua")
includes("./Framework/Game/Scene/Serialization/xmake.lua")
includes("./Framework/Game/Scene/Scene3D/xmake.lua")
includes("./Framework/Game/Resource/xmake.lua")
includes("./Framework/Game/Render/Graph/xmake.lua")
includes("./Framework/Game/Render/Render3D/xmake.lua")
includes("./Framework/Game/Gameplay/Animation/xmake.lua")
includes("./Framework/Game/Gameplay/ECS/Core/xmake.lua")
includes("./Framework/Game/Gameplay/ECS/xmake.lua")
includes("./Framework/Game/Physics/xmake.lua")

-- Product tier: assembled runtimes.
includes("./Product/Host/xmake.lua")
includes("./Product/Editor/xmake.lua")

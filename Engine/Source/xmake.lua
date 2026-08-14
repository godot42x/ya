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

--- Module target kind shared by every engine module: the same sources,
--- deps and package lists serve both linkage forms.
---   shared:   every module is its own shared library
---   monolith: modules become static and link into each product exe
--- @return string "shared" or "static"
function ya_target_kind()
    return (get_config("ya_linkage") or "shared") == "monolith" and "static" or "shared"
end

--- Kind for aggregate/meta facade targets that carry no sources of their
--- own: a real shared library in shared mode (compat facade), a pure
--- dependency group (phony, no archive) in monolith mode.
function ya_meta_kind()
    return (get_config("ya_linkage") or "shared") == "monolith" and "phony" or "shared"
end

--- Injects the shared module plumbing into the current target:
---   * the module's own export macro (api_macro) as a preprocessor alias to
---     YA_API_EXPORT -- no hand-written macros, no central macro table;
---   * build/import side switches (YA_SHARED / YA_MODULE_BUILD);
---   * the unity-build rule (module sources still need add_files("**.cpp")).
--- The switches follow ya_linkage: in monolith mode modules are static and
--- nothing imports/exports across DLL boundaries, so YA_SHARED=0 makes
--- YA_API_EXPORT resolve to empty on every platform (Windows static libs
--- must not see dllexport/dllimport on their own declarations).
--- @param api_macro string e.g. "YA_CORE_API"
function ya_std_module(api_macro)
    local monolith = (get_config("ya_linkage") or "shared") == "monolith"
    if monolith then
        add_defines("YA_SHARED=0", { public = true })
    else
        add_defines("YA_SHARED=1")
        add_defines("YA_MODULE_BUILD=1")
        add_defines("YA_SHARED=1", { public = true })
    end
    add_defines(api_macro .. "=YA_API_EXPORT", { public = true })
    if get_config("ya_enable_unity-build") then
        add_rules("c++.unity_build", { batchsize = 2 })
    end
    if is_plat("windows") then
        -- Unity-build objects of large modules exceed the default COFF
        -- section limit (C1128); /bigobj is required once sources are
        -- merged into a single translation unit.
        add_cxxflags("/bigobj")
    end

end

--- Injects every module export macro into the aggregate (ya-engine) so its
--- precompiled header can parse any engine header. Only the aggregate needs
--- this; modules get their single macro via ya_std_module().
function ya_engine_defines()
    local macros = {
        "YA_CORE_API", "YA_RHI_API", "YA_RHI_BACKEND_API", "YA_GUI_API",
        "YA_APP_KERNEL_API", "YA_APP_CONTROL_API", "YA_MODULE_MANAGER_API",
        "YA_SCENE_CORE_API", "YA_SCENE_RUNTIME_API", "YA_SCENE_SERIALIZATION_API",
        "YA_SCENE_3D_API", "YA_RESOURCE_API", "YA_RENDER_GRAPH_API",
        "YA_RENDER_3D_API", "YA_RENDER_ECS_ADAPTERS_API", "YA_ECS_CORE_API", "YA_GAMEPLAY_ECS_API", "YA_GAMEPLAY_SYSTEMS_API", "YA_COMPONENT_LINKAGE_API", "YA_PHYSICS_API",
        "YA_GAME_RUNTIME_API", "YA_GAME_EDITOR_API", "YA_RESOURCE_CORE_API", "YA_RESOURCE_LOADER_API",
    }
    for _, macro in ipairs(macros) do
        add_defines(macro .. "=YA_API_EXPORT")
    end
end

-- App tier: the windowless shared application main chain (kernel + control
-- plane) plus the optional module system. App must not depend on GUI, window
-- or any app-form semantics; the module system is a separate opt-in target
-- so windowless/GUI-only apps don't have to link it.
includes("./App/Kernel/xmake.lua")
includes("./App/Control/xmake.lua")
includes("./App/Module/xmake.lua")

-- Foundation tier: shared infrastructure consumed by every product line.
includes("./Foundation/Core/xmake.lua")
includes("./Foundation/RHI/xmake.lua")

-- Framework tier: product lines. Hierarchy first (renderer-independent
-- scene-tree base); GUI framework is self-contained; Game depends on
-- Foundation + Hierarchy + GUI.
includes("./Framework/Hierarchy/xmake.lua")
includes("./Framework/GUI/xmake.lua")
includes("./GUI/Host/xmake.lua")

-- Game product line + product tier: engine profile only. The gui profile
-- never pulls ECS/Scene3D/Resource/RenderGraph/Render3D/Physics/Host/Editor
-- (or their sources/packages/shader groups) into the build graph.
if get_config("ya_profile") ~= "gui" then
    includes("./Framework/Game/Scene/Core/xmake.lua")
    includes("./Framework/Game/Scene/Runtime/xmake.lua")
    includes("./Framework/Game/Scene/Serialization/xmake.lua")
    includes("./Framework/Game/Scene/Scene3D/xmake.lua")
    includes("./Framework/Game/Resource/Core/xmake.lua")
    includes("./Framework/Game/Resource/Loader/xmake.lua")
    includes("./Framework/Game/Resource/xmake.lua")
    includes("./Framework/Game/Render/Graph/xmake.lua")
    includes("./Framework/Game/Render/Render3D/xmake.lua")
    includes("./Framework/Game/Gameplay/ECS/Core/xmake.lua")
    includes("./Framework/Game/Gameplay/Linkage/xmake.lua")
    includes("./Framework/Game/Gameplay/Systems/xmake.lua")
    includes("./Framework/Game/Physics/xmake.lua")
    includes("./Framework/Game/Render/Adapters/xmake.lua")

    -- App-form tier: assembled runtimes/editor shells.
    includes("./Applications/GameRuntime/xmake.lua")
    includes("./Applications/GameEditor/xmake.lua")
end

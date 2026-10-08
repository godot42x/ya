-- ============================================================================
-- Engine source root.
--
-- Source is organized into two tiers, each module living in its own
-- directory with a self-describing xmake.lua:
--
--   Framework/     engine-agnostic reusable capability layer (Core / RHI /
--                  App / GUI / Render / Scripting / Resource / Scene /
--                  Physics / ECS)
--   Applications/  assembled app forms (GameRuntime / GameEditor)
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
-- 根据 `ya_linkage` 配置决定元构建的目标类型。
-- 当配置为 `monolith`（单体构建）时返回 `phony`（伪目标），否则返回 `shared`（动态库）。
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
--- Routes the target's global C++ allocations through the engine's allocator
--- layer (see Core/Memory/Allocator.h): compiles the target's own copy of the
--- replaced `operator new` / `operator delete` and selects the counting
--- process allocator.
---
--- Every C++ module needs its own copy of the replacement: Mach-O binds an
--- image's undefined `operator new` to that same image, and MSVC gives every
--- DLL its own CRT, so one copy anywhere else would only ever route one
--- module's allocations. All copies forward to the same Core entry points.
---
--- Profile builds only, for now: that is the mode that measures, and routing
--- every allocation is a change worth validating where it is observed first.
--- Turning it on for every build (and swapping in another backend) is this
--- function plus the alias at the bottom of Allocator.h.
function ya_process_allocator()
    -- is_mode(), not get_config("mode"): while the project description is
    -- being evaluated the config table does not carry the build mode yet, so
    -- get_config("mode") reads nil and any comparison silently passes.
    if not is_mode("profile") then
        return
    end
    add_files(path.join(YA_SOURCE_ROOT, "Framework/Core/Memory/ProcessAllocatorInstall.cpp"))
    add_defines("YA_ALLOCATOR_COUNTING=1")
end

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
        add_rules("c++.unity_build", { batchsize = 6 })
    end
    if is_plat("windows") then
        -- Unity-build objects of large modules exceed the default COFF
        -- section limit (C1128); /bigobj is required once sources are
        -- merged into a single translation unit.
        add_cxxflags("/bigobj")
    end

    ya_process_allocator()
end

--- Injects every module export macro into the aggregate (ya-engine) so its
--- precompiled header can parse any engine header. Only the aggregate needs
--- this; modules get their single macro via ya_std_module().
function ya_engine_defines()
    local macros = {
        "YA_CORE_API", "YA_RHI_API", "YA_RHI_BACKEND_API", "YA_RENDER_RESOURCES_API", "YA_RENDER_2D_API", "YA_GUI_API",
        "YA_APP_KERNEL_API", "YA_APP_CONTROL_API", "YA_MODULE_MANAGER_API",
        "YA_SCENE_CORE_API", "YA_SCENE_RUNTIME_API", "YA_SCENE_SERIALIZATION_API",
        "YA_SCENE_3D_API", "YA_SCENE_2D_API", "YA_RESOURCE_API", "YA_RENDER_GRAPH_API",
        "YA_RENDER_3D_API", "YA_RENDER_ECS_ADAPTERS_API", "YA_ECS_CORE_API", "YA_GAMEPLAY_ECS_API", "YA_ECS_SYSTEMS_API", "YA_COMPONENT_LINKAGE_API", "YA_PHYSICS_API",
        "YA_GAME_RUNTIME_API", "YA_GAME_EDITOR_API", "YA_RESOURCE_CORE_API", "YA_RESOURCE_LOADER_API",
    }
    for _, macro in ipairs(macros) do
        add_defines(macro .. "=YA_API_EXPORT")
    end
end

-- Framework tier (engine-agnostic reusable capability layer), ordered by
-- dependency: Core + RHI at the bottom, then the windowless App main chain
-- (kernel + control plane + optional module system), the renderer-independent
-- Hierarchy base, and the GUI framework (runtime + tooling + host). App must
-- not depend on GUI, window or any app-form semantics; the module system is
-- a separate opt-in target so windowless/GUI-only apps don't have to link it.
includes("./Framework/Core/xmake.lua")
includes("./Framework/RHI/xmake.lua")
includes("./Framework/Render/Resources/xmake.lua")
includes("./Framework/Render/Render2D/xmake.lua")
includes("./Framework/App/Kernel/xmake.lua")
includes("./Framework/App/Control/xmake.lua")
includes("./Framework/App/Module/xmake.lua")
includes("./Framework/Hierarchy/xmake.lua")
includes("./Framework/GUI/xmake.lua")

-- Engine capability modules + app-form tier: engine profile only. The gui
-- profile never pulls ECS/Scene3D/Resource/RenderGraph/Render3D/Physics/Host/
-- Editor (or their sources/packages/shader groups) into the build graph.
if get_config("ya_profile") ~= "gui" then
    includes("./Framework/Scene/Core/xmake.lua")
    includes("./Framework/Scene/Runtime/xmake.lua")
    includes("./Framework/Scene/Serialization/xmake.lua")
    includes("./Framework/Scene/Scene3D/xmake.lua")
    includes("./Framework/Scene/Scene2D/xmake.lua")
    includes("./Framework/Resource/Core/xmake.lua")
    includes("./Framework/Resource/Loader/xmake.lua")
    includes("./Framework/Resource/xmake.lua")
    includes("./Framework/Render/Graph/xmake.lua")
    includes("./Framework/Render/Render3D/xmake.lua")
    includes("./Framework/ECS/Core/xmake.lua")
    includes("./Framework/ECS/Linkage/xmake.lua")
    includes("./Framework/ECS/Systems/xmake.lua")
    includes("./Framework/Script/Lua/xmake.lua")
    includes("./Framework/Physics/xmake.lua")
    includes("./Framework/Render/Adapters/xmake.lua")

    -- App-form tier: assembled runtimes/editor shells.
    includes("./Applications/GameRuntime/xmake.lua")
    includes("./Applications/GameEditor/xmake.lua")
end

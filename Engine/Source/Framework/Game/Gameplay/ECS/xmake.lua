target("ya-gameplay-ecs")
    set_kind("shared")
    ya_std_module("YA_GAMEPLAY_ECS_API")
    -- Fat-module transition: the module's own source tree is exposed through
    -- the Gameplay tier root ("ECS/..."); render reach is compile-time only
    -- (the .cpp files still use Render3D material types) until the fat module
    -- dissolves into gameplay-systems / render-ecs-adapters.
    ya_tier_include("Gameplay", "Render")
    -- Core ECS infrastructure lives in ya-ecs-core (ECS/Core/); this target
    -- keeps the gameplay components/systems until the Phase 2 split into
    -- ya-gameplay-systems / ya-component-linkage / ya-render-ecs-adapters.
    add_files("**.cpp|Core/**.cpp")
    add_headerfiles("**.h|Core/**.h")
    -- Scene line types (Scene data / lifecycle / Node3D) are consumed by the
    -- systems.
    add_deps("ya-scene-3d", "ya-scene-core", "ya-scene-runtime")
    -- Resource/GUI headers are reached from the components' resolve code.
    add_deps("ya-resource-core", "ya-resource-loader", "ya-resource-runtime", "ya-gui-resources")
    -- Public headers reference the gameplay systems (ScriptingSystem base).
    add_deps("ya-gameplay-systems", { public = true })
    -- Fat module for now: ECS components/systems reference render + resource
    -- types (Mesh/Material/pipeline headers), so RHI is a visible dependency.
    add_deps("ya-foundation-core", "ya-ecs-core", "ya-rhi", { public = true })
    -- ECS public headers still reach Scene data/lifecycle (LuaScriptComponent,
    -- ComponentLinkageSystem, ResourceResolveSystem) through the Scene tier
    -- include root; a dep edge would cycle with ya-scene-core (Scene.h owns an
    -- entt registry). Phase 2 (ya-ecs-core) resolves this: scene deps on the
    -- ECS core, ECS systems lose their scene/render reach.
    -- Render3D headers are compiled from the component/system .cpp files
    -- (render tier include); inject the render-side export macros only.
    add_defines("YA_RENDER_3D_API=YA_API_EXPORT", "YA_GUI_API=YA_API_EXPORT", "YA_RESOURCE_API=YA_API_EXPORT")
    add_packages("entt", "glm", "nlohmann_json", "sol2", { public = true })
    add_packages("lua", "quickjs-ng", "cxxopts")
    if is_plat("macosx") then
        -- Transition: ECS systems call host App services (see plan.md §10);
        -- symbols resolve from the final binary until the app-services
        -- interface lands.
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    end

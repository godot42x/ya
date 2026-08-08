target("ya-gameplay-ecs")
    set_kind("shared")
    ya_std_module("YA_GAMEPLAY_ECS_API")
    -- Fat-module transition: ECS headers still reach render/resource/GUI/host
    -- headers without a corresponding dep edge (see plan.md §10.6); once
    -- ya-gameplay is split these extra tiers move out with the files.
    ya_tier_include("Gameplay", "Game", "Scene", "Render", "Framework", "Product")
    -- Core ECS infrastructure lives in ya-ecs-core (ECS/Core/); this target
    -- keeps the gameplay components/systems until the Phase 2 split into
    -- ya-gameplay-systems / ya-component-linkage / ya-render-ecs-adapters.
    add_files("**.cpp|Core/**.cpp")
    add_headerfiles("**.h|Core/**.h")
    -- Scene3D types (TransformComponent/Node3D) are consumed by the systems.
    add_deps("ya-scene-3d")
    -- Fat module for now: ECS components/systems reference render + resource
    -- types (Mesh/Material/pipeline headers), so RHI is a visible dependency.
    add_deps("ya-foundation-core", "ya-ecs-core", "ya-foundation-rhi", { public = true })
    -- ECS public headers still reach Scene data/lifecycle (LuaScriptComponent,
    -- ComponentLinkageSystem, ResourceResolveSystem) through the Scene tier
    -- include root; a dep edge would cycle with ya-scene-core (Scene.h owns an
    -- entt registry). Phase 2 (ya-ecs-core) resolves this: scene deps on the
    -- ECS core, ECS systems lose their scene/render reach.
    -- Transition: the fat ECS module still compiles render/resource/host
    -- headers (App services, Mesh/Material pipeline types). Planned
    -- decoupling: split ya-gameplay behind an app-services interface (see
    -- plan.md §10); until then every engine export macro is injected here.
    ya_engine_defines()
    add_packages("entt", "glm", "nlohmann_json", "sol2", { public = true })
    add_packages("lua", "quickjs-ng", "cxxopts")
    if is_plat("macosx") then
        -- Transition: ECS systems call host App services (see plan.md §10);
        -- symbols resolve from the final binary until the app-services
        -- interface lands.
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    end

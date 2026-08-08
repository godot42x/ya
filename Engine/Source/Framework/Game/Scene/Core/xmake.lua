-- Scene core: scene data (Scene registry + node-tree composition) and the
-- lifecycle registration seam (ISceneLifecycleHost) shared by scene-runtime,
-- scene-serialization and the Host. Kept free of Render3D and Host types.
target("ya-scene-core")
    set_kind("shared")
    ya_std_module("YA_SCENE_CORE_API")
    ya_tier_include("Scene", "Gameplay", "Framework", "Game")
    add_files("**.cpp")
    add_headerfiles("**.h")
    -- Scene.h composes ECS entities, the GUI node tree and Node3D; it also
    -- references Resource model types. All public because Scene.h is public.
    -- ECS reach is the core infrastructure only (ecs-core); the entity-scene
    -- contract (EntitySceneBridge.cpp) is implemented here.
    add_deps("ya-foundation-core", "ya-ecs-core", "ya-gui-runtime", "ya-scene-3d", "ya-resource", { public = true })

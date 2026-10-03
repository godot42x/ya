-- Scene2D: authored 2D scene data (Sprite2DComponent, TilemapComponent with
-- its query / edit faces). Pure data on top of the 3D scene tree: a sprite or
-- tilemap is a Node3D with a TransformComponent, so there is no Transform2D.
-- No renderer dependency: expanding a tilemap into draw candidates lives in
-- ya-render-3d (Render3D/Common/TilemapExtraction.h).
target("ya-scene-2d")
    set_kind(ya_target_kind())
    ya_std_module("YA_SCENE_2D_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-ecs-core", "ya-scene-3d", { public = true })
    -- TransformSystem::computeWorldMatrix (tilemap query face) and Scene
    -- (SpriteAnimationSystem walks its registry).
    add_deps("ya-ecs-systems", "ya-scene-core")
    add_packages("glm", "entt", { public = true })

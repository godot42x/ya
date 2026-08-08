ya_module("ya-scene-3d", "GAME_SCENE_3D", {
    include_root = "../../../..",
    deps = {
        "ya-gui-runtime",
        "ya-gameplay-ecs",
        "ya-foundation-core",
    },
    packages = { "glm" },
})

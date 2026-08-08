ya_module("ya-physics", "GAME_PHYSICS", {
    include_root = "../../..",
    deps = {
        "ya-foundation-core",
        "ya-gameplay-ecs",
    },
    packages = {
        "joltphysics",
        "glm",
    },
})

ya_module("ya-gameplay-ecs", "GAMEPLAY_ECS", {
    include_root = "../../../..",
    deps = {
        "ya-foundation-core",
        -- Fat module: ECS components/systems reference render + resource
        -- types (Mesh/Material/pipeline headers), so the RHI include inputs
        -- (generated shader headers) must be visible here.
        "ya-foundation-rhi",
    },
    packages = {
        "entt",
        "glm",
        "lua",
        "sol2",
        "quickjs-ng",
        "cxxopts",
    },
})

ya_module("ya-ecs", "ECS", {
    deps = {
        "ya-core",
        -- Fat module: ECS components/systems reference render + resource
        -- types (Mesh/Material/pipeline headers), so the RHI include inputs
        -- (generated shader headers) must be visible here.
        "ya-rhi",
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

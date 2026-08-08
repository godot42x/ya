ya_module("ya-foundation-core", "FOUNDATION_CORE", {
    include_root = "../..",
    deps = {
        "utility.cc",
        "log.cc",
        "reflects-core",
    },
    packages = {
        "glm",
        "nlohmann_json",
        "libsdl3",
        -- Reflection.h pulls in ECS/ECSRegistry.h (pre-existing coupling,
        -- tracked as a follow-up to decouple; see Reflection.h TODO).
        "entt",
    },
})

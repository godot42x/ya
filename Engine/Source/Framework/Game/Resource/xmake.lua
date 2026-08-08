ya_module("ya-resource", "GAME_RESOURCE", {
    include_root = "../../..",
    deps = { "ya-foundation-rhi" },
    -- TinyGLTF single-header implementation lives next to its wrapper.
    exclude = "Model/TinyGLTF.cpp",
    unity_ignored = {
        "Model/TinyGLTF.cpp",
    },
    packages = {
        "glm",
        "nlohmann_json",
        "stb",
        "ktx",
        "tinygltf",
        "assimp",
        "vulkansdk",
        "cxxopts",
    },
})

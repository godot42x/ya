ya_module("ya-resource", "RESOURCE", {
    deps = { "ya-rhi" },
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

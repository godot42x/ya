-- Resource loader: asset importers (Assimp / TinyGLTF / stb) producing the
-- imported-data contracts from ya-resource-core. No RHI/GUI/Host reach.
target("ya-resource-loader")
    set_kind(ya_target_kind())
    ya_std_module("YA_RESOURCE_LOADER_API")
    ya_tier_include("Game")
    add_includedirs("./include", { public = true })
    add_files("**.cpp|Model/TinyGLTF.cpp")
    add_files("Model/TinyGLTF.cpp", { unity_ignored = true })
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-resource-core", { public = true })
    add_packages("assimp", "tinygltf", "stb")

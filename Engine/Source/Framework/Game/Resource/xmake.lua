-- Resource runtime: AssetManager, caches, GPU meshes/models and the
-- engine asset-ref resolver. Depends on core + loader + RHI/GUI; never
-- reaches Scene/ECS/Host/Render3D.
target("ya-resource-runtime")
    set_kind(ya_target_kind())
    ya_std_module("YA_RESOURCE_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp|Core/**.cpp|Loader/**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h|Core/**.h|Loader/**.h")
    add_deps("ya-foundation-core", "ya-rhi", "ya-resource-core", { public = true })
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan", "ya-resource-loader")
    add_packages("glm", "nlohmann_json", { public = true })
    add_packages("stb", "ktx", "vulkansdk", "cxxopts")

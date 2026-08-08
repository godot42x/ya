-- Resource runtime: AssetManager, caches, GPU meshes/models and the
-- engine asset-ref resolver. Depends on core + loader + RHI/GUI; never
-- reaches Scene/ECS/Host/Render3D.
target("ya-resource-runtime")
    set_kind("shared")
    ya_std_module("YA_RESOURCE_API")
    ya_tier_include("Game")
    add_files("**.cpp|Core/**.cpp|Loader/**.cpp")
    add_headerfiles("**.h|Core/**.h|Loader/**.h")
    add_deps("ya-rhi", "ya-rhi-backend-common", "ya-rhi-vulkan", "ya-gui-runtime", "ya-resource-core", "ya-resource-loader", { public = true })
    if is_plat("macosx") then
        -- Transition: Game-layer modules still call host App services at
        -- runtime (see plan.md §10). Symbols resolve from the final binary
        -- (ya-runtime / editor) which links ya-host. Remove once the
        -- app-services interface replaces direct App access.
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    end
    add_packages("glm", "nlohmann_json", { public = true })
    add_packages("stb", "ktx", "vulkansdk", "cxxopts")

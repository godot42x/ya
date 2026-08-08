target("ya-resource")
    set_kind("shared")
    ya_std_module("YA_RESOURCE_API")
    add_includedirs("../../..", { public = true })
    add_files("**.cpp|Model/TinyGLTF.cpp")
    add_files("Model/TinyGLTF.cpp", { unity_ignored = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-rhi", "ya-foundation-rhi-backend", "ya-gui-runtime", { public = true })
    if is_plat("macosx") then
        -- Transition: Game-layer modules still call host App services at
        -- runtime (see plan.md §10). Symbols resolve from the final binary
        -- (ya-runtime / editor) which links ya-host. Remove once the
        -- app-services interface replaces direct App access.
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    end
    -- Transition: the game product line still compiles render/host headers
    -- (AssetManager reaches App; material types reach Render3D/RenderGraph).
    -- Planned decoupling: see plan.md §10; until then every engine export
    -- macro is injected here.
    ya_engine_defines()
    add_packages("glm", "nlohmann_json", "stb", "ktx", "tinygltf", "assimp", "vulkansdk", "cxxopts", { public = true })

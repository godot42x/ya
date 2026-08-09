target("ya-editor")
    set_kind(ya_target_kind())
    ya_std_module("YA_EDITOR_API")
    add_includedirs("./include", { public = true })
    add_headerfiles("./include/**.h", { public = true })
    add_files("**.cpp")
    if get_config("ya_linkage") == "monolith" then
        -- Loaded by the runtime host; engine symbols resolve from the host
        -- exe (single engine instance) instead of embedding static libs.
        add_deps("ya-engine", { links = false })
        add_shflags("-undefined", "dynamic_lookup", { force = true })
    else
        add_deps("ya-engine")
        add_links("ya-engine")
    end
    -- imgui/imguizmo are now single shared libraries (global ImGui state);
    -- the editor consumes the same images as host.
    add_deps("imgui-local", "imguizmo-local")
    add_includedirs("../../../ThirdParty/ImGui", { public = true })
    add_includedirs("../../../ThirdParty/ImGui/Backends", { public = true })
    add_includedirs("../../../ThirdParty/ImGui/misc/cpp", { public = true })
    add_includedirs("../../../ThirdParty/ImGui/misc/freetype", { public = true })
    add_includedirs("../../../ThirdParty/ImGuizmo", { public = true })
    if is_plat("windows") then
        add_cxxflags("/bigobj")
        add_defines("IMGUI_API=__declspec(dllimport)")
        add_defines("IMGUI_IMPL_API=__declspec(dllimport)")
        add_defines("USE_IMGUI_API")
    end

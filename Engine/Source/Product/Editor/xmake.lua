target("ya-editor")
    set_kind(ya_target_kind())
    ya_std_module("YA_EDITOR_API")
    ya_tier_include("Product")
    add_includedirs("./include", { public = true })
    add_headerfiles("./include/**.h", { public = true })
    add_files("**.cpp")
    add_deps("ya-engine")
    add_links("ya-engine")
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

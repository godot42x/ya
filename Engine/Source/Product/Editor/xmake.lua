target("ya-editor")
do
    set_kind("shared")
    if get_config("ya_enable_unity-build") then
        add_rules("c++.unity_build", { batchsize = 3 })
        add_files("**.cpp", { unity_group = "Editor" })
    end
    add_files("**.cpp")
    add_deps("ya-engine")
    add_links("ya-engine")
    add_includedirs("../../..", { public = true })
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
end

add_requires("vulkan-headers")
add_requires("freetype")

-- TODO: disable 3rd pkg warning?
-- rule("disable all warning")
-- do -- disable all third-party package warnings
--     add_cxxflags("/W0", { tools = { "msvc" } })
--     add_cxxflags("-w", { tools = { "gcc", "clang" } })
-- end

target("imgui-local")
do
    set_kind("static")

    -- use sdl3 + vulkan + opengl
    add_packages("libsdl3")
    add_packages("vulkan-headers")
    add_packages("freetype")

    -- add_rules("c++.unity_build", { batchsize = 2 }) -- no need to build multiple times, and sub modules cannot manage recursively dependency
    add_files("./ImGui/*.cpp", "./ImGui/misc/cpp/*.cpp")
    -- add_headerfiles("ImGui/", "(./ImGui/misc/cpp/*.h)")
    add_includedirs("./ImGui/", { public = true })
    add_includedirs("./ImGui/misc/cpp/", { public = true })
    add_includedirs("./ImGui/backends/", { public = true })
    add_includedirs("./ImGui/misc/freetype/", { public = true })

    -- sdl3
    add_files("./ImGui/Backends/imgui_impl_sdl3.cpp")
    -- add_headerfiles("(./ImGui/Backends/imgui_impl_sdl3.h)")
    -- vulkan
    add_files("./ImGui/Backends/imgui_impl_vulkan.cpp")
    -- add_headerfiles("(./ImGui/backends/imgui_impl_vulkan.h)")

    -- enable freetype + wchar32 for unicode support
    add_files("./ImGui/misc/freetype/imgui_freetype.cpp")
    -- add_headerfiles("misc/freetype/imgui_freetype.h")
    add_packages("freetype")
    add_defines("IMGUI_ENABLE_FREETYPE")
    add_defines("IMGUI_USE_WCHAR32")

    if is_kind("shared") and is_plat("windows", "mingw") then
        add_defines("IMGUI_API=__declspec(dllexport)")
    end



    -- after_install(function(target)
    --     local config_file = path.join(target:installdir(), "include/imconfig.h")
    --     if has_config("wchar32") then
    --         io.gsub(config_file, "//#define IMGUI_USE_WCHAR32", "#define IMGUI_USE_WCHAR32")
    --     end
    --     if has_config("freetype") then
    --         io.gsub(config_file, "//#define IMGUI_ENABLE_FREETYPE", "#define IMGUI_ENABLE_FREETYPE")
    --     end
    -- end)
end

target("imguizmo-local")
do
    set_kind("static")
    -- add_rules("c++.unity_build", { batchsize = 2 })

    add_deps("imgui-local")

    add_defines("IMGUI_DEFINE_MATH_OPERATORS")
    add_files("./ImGuizmo/*.cpp")
    -- add_headerfiles("./ImGuizmo/*.h")
    add_includedirs("./ImGuizmo/", { public = true })

    if is_plat("windows") and is_kind("shared") then
        add_rules("utils.symbols.export_all", { export_classes = true })
    end

    on_test(function(package)
        assert(package:check_cxxsnippets({
            test = [[
            void test() {
                ImGuiIO& io = ImGui::GetIO();
                ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
            }
        ]]
        }, { configs = { languages = "c++11" }, includes = { "imgui.h", "ImGuizmo.h" } }))
    end)
end

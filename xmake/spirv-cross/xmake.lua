package("spirv-cross")
do
    set_homepage("https://github.com/KhronosGroup/SPIRV-Cross")
    set_description("SPIRV-Cross is a practical tool and library for performing reflection on SPIR-V and disassembling SPIR-V back to high level languages. ")
    set_license("Apache-2.0")

    add_urls("https://github.com/KhronosGroup/SPIRV-Cross.git", { alias = "git" })

    -- add_urls("https://github.com/ocornut/imgui/archive/refs/tags/$(version).tar.gz")
    -- add_urls("https://github.com/ocornut/imgui.git", { alias = "git" })

    add_version("git:v1.4.304.1","vulkan-dsk-1.4.304.1")


    -- add_configs("dx9", { description = "Enable the dx9 backend", default = false, type = "boolean" })
    -- add_configs("dx10", { description = "Enable the dx10 backend", default = false, type = "boolean" })
    -- add_configs("dx11", { description = "Enable the dx11 backend", default = false, type = "boolean" })
    -- add_configs("dx12", { description = "Enable the dx12 backend", default = false, type = "boolean" })
    -- add_configs("glfw", { description = "Enable the glfw backend", default = false, type = "boolean" })
    -- add_configs("opengl2", { description = "Enable the opengl2 backend", default = false, type = "boolean" })
    -- add_configs("opengl3", { description = "Enable the opengl3 backend", default = false, type = "boolean" })
    -- add_configs("sdl2", { description = "Enable the sdl2 backend with sdl2_renderer", default = false, type = "boolean" })
    -- add_configs("sdl2_no_renderer",
    --     { description = "Enable the sdl2 backend without sdl2_renderer", default = false, type = "boolean" })
    -- add_configs("sdl2_renderer", { description = "Enable the sdl2 renderer backend", default = false, type = "boolean" })
    -- add_configs("vulkan", { description = "Enable the vulkan backend", default = false, type = "boolean" })
    -- add_configs("win32", { description = "Enable the win32 backend", default = false, type = "boolean" })
    -- add_configs("wgpu", { description = "Enable the wgpu backend", default = false, type = "boolean" })
    -- add_configs("freetype",
    --     { description = "Use FreeType to build and rasterize the font atlas", default = false, type = "boolean" })
    -- add_configs("user_config", { description = "Use user config (disables test!)", default = nil, type = "string" })
    -- add_configs("wchar32",
    --     { description = "Use 32-bit for ImWchar (default is 16-bit)", default = false, type = "boolean" })

    -- -- deprecated configs (kept for backwards compatibility)
    -- add_configs("sdlrenderer", { description = "(deprecated)", default = false, type = "boolean" })
    -- add_configs("glfw_opengl3", { description = "(deprecated)", default = false, type = "boolean" })
    -- add_configs("glfw_vulkan", { description = "(deprecated)", default = false, type = "boolean" })
    -- add_configs("sdl2_opengl3", { description = "(deprecated)", default = false, type = "boolean" })


    on_load(function(package)
        -- begin: backwards compatibility
        -- if package:config("sdl2") or package:config("sdlrenderer") then
        --     package:config_set("sdl2_renderer", true)
        -- end
        -- if package:config("glfw_opengl3") then
        --     package:config_set("glfw", true)
        --     package:config_set("opengl3", true)
        -- end
        -- if package:config("glfw_vulkan") then
        --     package:config_set("glfw", true)
        --     package:config_set("vulkan", true)
        -- end
        -- if package:config("sdl2_opengl3") then
        --     package:config_set("sdl2", true)
        --     package:config_set("opengl3", true)
        -- end
        -- -- end: backwards compatibility
        -- if package:config("shared") and is_plat("windows", "mingw") then
        --     package:add("defines", "IMGUI_API=__declspec(dllimport)")
        -- end
        -- if package:config("glfw") then
        --     package:add("deps", "glfw")
        -- end
        -- if package:config("opengl3") then
        --     if not package:gitref() and package:version():lt("1.84") then
        --         package:add("deps", "glad")
        --         package:add("defines", "IMGUI_IMPL_OPENGL_LOADER_GLAD")
        --     end
        -- end
        -- if package:config("sdl2_no_renderer") then
        --     package:add("deps", "libsdl2")
        -- end
        -- if package:config("sdl2_renderer") then
        --     package:add("deps", "libsdl2 >=2.0.17")
        -- end
        -- if package:config("vulkan") then
        --     package:add("deps", "vulkansdk")
        -- end
        -- if package:config("wgpu") then
        --     package:add("deps", "wgpu-native")
        -- end
        -- if package:config("freetype") then
        --     package:add("deps", "freetype")
        -- end
    end)

    on_install(function(package)
        local configs = {
            -- dx9           = package:config("dx9"),
            -- dx10          = package:config("dx10"),
            -- dx11          = package:config("dx11"),
            -- dx12          = package:config("dx12"),
            -- glfw          = package:config("glfw"),
            -- opengl2       = package:config("opengl2"),
            -- opengl3       = package:config("opengl3"),
            -- glad          = package:config("opengl3") and (not package:gitref() and package:version():lt("1.84")),
            -- sdl2          = package:config("sdl2") or package:config("sdl2_no_renderer"),
            -- sdl2_renderer = package:config("sdl2_renderer"),
            -- vulkan        = package:config("vulkan"),
            -- win32         = package:config("win32"),
            -- wgpu          = package:config("wgpu"),
            -- freetype      = package:config("freetype"),
            -- user_config   = package:config("user_config"),
            -- wchar32       = package:config("wchar32")
        }

        os.cp(path.join(package:scriptdir(), "port", "xmake.lua"), "xmake.lua")
        import("package.tools.xmake").install(package, configs)
    end)

    on_test(function(package)
        -- if package:config("user_config") ~= nil then return end
        -- local includes = { "imgui.h" }
        -- local defines
        -- if package:config("sdl2_renderer") or package:config("sdl2_no_renderer") then
        --     table.insert(includes, "SDL.h")
        --     defines = "SDL_MAIN_HANDLED"
        -- end
        -- assert(package:check_cxxsnippets({
        --     test = [[
        --     void test() {
        --         IMGUI_CHECKVERSION();
        --         ImGui::CreateContext();
        --         ImGuiIO& io = ImGui::GetIO();
        --         ImGui::NewFrame();
        --         ImGui::Text("Hello, world!");
        --         ImGui::ShowDemoWindow(NULL);
        --         ImGui::Render();
        --         ImGui::DestroyContext();
        --     }
        -- ]]
        -- }, { configs = { languages = "c++14", defines = defines }, includes = includes }))
    end)
end

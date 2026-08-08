-- GUI compose: viewport/UI compose pass. Only needed when render-target
-- composition is used.
target("ya-gui-compose")
    set_kind("shared")
    ya_std_module("YA_GUI_API")
    ya_tier_include("Framework")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", "ya-rhi", { public = true })
    add_deps("ya-gui-scene", "ya-gui-draw2d", "ya-gui-resources")
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan")
    add_packages("glm", { public = true })

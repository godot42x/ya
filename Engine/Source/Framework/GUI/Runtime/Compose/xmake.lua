-- GUI compose: viewport/UI compose pass. Only needed when render-target
-- composition is used.
target("ya-gui-compose")
    set_kind(ya_target_kind())
    ya_std_module("YA_GUI_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", "ya-rhi", { public = true })
    add_deps("ya-gui-widgets", "ya-render-2d", "ya-render-resources")
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan")
    add_packages("glm", { public = true })

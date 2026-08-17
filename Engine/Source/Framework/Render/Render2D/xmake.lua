-- Render2D: sprite/text/line batching and the 2D pipeline. Depends on the
-- Render resources layer for fonts/textures.
target("ya-render-2d")
    set_kind(ya_target_kind())
    ya_std_module("YA_RENDER_2D_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", "ya-rhi", { public = true })
    add_deps("ya-render-resources")
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan")
    add_packages("glm", { public = true })

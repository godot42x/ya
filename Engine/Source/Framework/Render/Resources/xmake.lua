-- Render resources: font/glyph management and texture-slot binding. Depends on
-- Core/RHI only; public API exposed through the Render/Resources/ forwarding
-- root.
target("ya-render-resources")
    set_kind(ya_target_kind())
    ya_std_module("YA_RENDER_RESOURCES_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("*.h")
    add_deps("ya-foundation-core", "ya-rhi", { public = true })
    -- TextureSlotBinding resolves through the backend builtin texture library;
    -- the RHI factory entry points (descriptor pools / pipeline layouts) are
    -- implemented by the platform backend.
    add_deps("ya-rhi-backend-common", "ya-rhi-vulkan")
    add_packages("freetype")
    add_packages("glm", { public = true })

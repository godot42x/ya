-- RHI module: interface + platform-independent implementation only. The
-- concrete backends live in their own targets under Backend/ (see
-- ./Backend/xmake.lua) and are never collected through a parent exclusion
-- glob. Generated shader headers (Common.Limits.*, Sprite2D.*, ...) are
-- public include inputs consumed through RenderDefines.h; the paths are
-- derived from this file's directory (Engine/Source/Foundation/RHI).
target("ya-rhi")
    set_kind(ya_target_kind())
    ya_std_module("YA_RHI_API")
    add_includedirs("./include", { public = true })
    -- Only the shared shader-common generated headers (Common.*.slang.h /
    -- Common.*.glsl.h) are public interface of the RHI; the render-3d group
    -- lives in the Generated roots and is propagated by its own consumers.
    add_includedirs(path.join(os.scriptdir(), "../../../Shader/Slang/Generated/Common"), { public = true })
    add_includedirs(path.join(os.scriptdir(), "../../../Shader/GLSL/Generated/Common"), { public = true })
    add_files("Core/**.cpp")
    add_files("Shader/**.cpp")
    add_files("RenderDefines.cpp", "Shader.cpp", "NativeWindow.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("Core/**.h")
    add_headerfiles("Shader/**.h")
    add_headerfiles("Render.h", "RenderDefines.h", "Shader.h", "NativeWindow.h")
    add_deps("ya-foundation-core", "utility.cc", { public = true })
    add_packages("glm", "entt", "vulkansdk", "libsdl3", { public = true })
    -- Regenerates Engine/Shader/*/Generated when shader sources change.
    add_rules("ya.shader.codegen")

includes("./Backend/xmake.lua")

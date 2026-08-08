-- RHI module. Generated shader headers (Common.Limits.*, Sprite2D.*, ...) are
-- public include inputs consumed through RenderDefines.h; the paths are
-- derived from this file's directory (Engine/Source/Foundation/RHI).
target("ya-foundation-rhi")
    set_kind("shared")
    ya_std_module("YA_RHI_API")
    add_includedirs("../..", { public = true })
    add_includedirs(path.join(os.scriptdir(), "../../../Shader/Slang/Generated"), { public = true })
    add_includedirs(path.join(os.scriptdir(), "../../../Shader/GLSL/Generated"), { public = true })
    add_files("**.cpp|Backend/**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "utility.cc", { public = true })
    add_packages("glm", "entt", "vulkansdk", "libsdl3", { public = true })
    -- Regenerates Engine/Shader/*/Generated when shader sources change.
    add_rules("ya.shader.codegen")

includes("./Backend/xmake.lua")

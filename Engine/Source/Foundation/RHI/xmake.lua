ya_module("ya-foundation-rhi", "FOUNDATION_RHI", {
    include_root = "../..",
    deps = {
        "ya-foundation-core",
        "utility.cc",
    },
    packages = {
        "glm",
        -- Runtime shader compilation (Slang / shaderc / spirv-cross) and the
        -- SDL_gpu helper used by Shader.cpp belong to the RHI shader layer.
        "vulkansdk",
        "libsdl3",
    },
    shader_generated = true,
    -- Backend lives in its own module (ya-rhi-backend) below.
    exclude = "Backend/**.cpp",
    extra_setup = function()
        -- Regenerates Engine/Shader/*/Generated when shader sources change;
        -- the generated headers are public include inputs of this module.
        add_rules("ya.shader.codegen")
    end,
})

includes("./Backend/xmake.lua")

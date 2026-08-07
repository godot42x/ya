-- ============================================================================
-- Engine module aggregator.
--
-- Every engine module follows one shape (see `ya_module` below) and lives in
-- its own directory with its own xmake.lua; this file only wires them up.
-- The shared setup exists in exactly one place instead of being copy-pasted
-- per module:
--   * export-macro plumbing (BUILD_SHARED_YA / BUILD_LIBRARY, see Core/Api.h)
--   * the common include root (Engine/Source)
--   * per-module unity grouping
-- Each module globs its own sources ("**.cpp") and never reaches outside its
-- directory, so the target list stays in sync with the directory layout.
-- ============================================================================

function ya_module(name, root, opts)
    opts = opts or {}
    local exclude = opts.exclude or ""
    local includeRoot = opts.include_root or ".."

    target(name)
        set_kind("static")
        -- Export plumbing (see Core/Api.h): every module is compiled into the
        -- single shared aggregate (ya-engine); consumers only see the import
        -- side. xmake stamps both defines, so no module hand-writes
        -- dllexport/dllimport.
        add_defines("YA_SHARED=1")
        add_defines("YA_MODULE_BUILD=1")
        add_includedirs(includeRoot, { public = true })

        if opts.shader_generated then
            -- Generated Slang/GLSL headers (Common.Limits.*, Sprite2D.*, ...)
            -- are consumed through RenderDefines.h and the UI shaders.
            add_includedirs("../../Shader/Slang/Generated", { public = true })
            add_includedirs("../../Shader/GLSL/Generated", { public = true })
        end

        if get_config("ya_enable_unity-build") then
            add_rules("c++.unity_build", { batchsize = 2 })
            if exclude ~= "" then
                add_files("**.cpp|" .. exclude, { unity_group = root })
            else
                add_files("**.cpp", { unity_group = root })
            end
        end
        if exclude ~= "" then
            add_files("**.cpp|" .. exclude)
        else
            add_files("**.cpp")
        end
        add_headerfiles("**.h")

        -- Single-header third-party implementations (VMA / STB / TinyGLTF)
        -- must stay out of unity batches: a sibling TU in the same batch can
        -- include the header first, which blocks the _IMPLEMENTATION macro
        -- expansion via the include guard.
        for _, f in ipairs(opts.unity_ignored or {}) do
            add_files(f, { unity_ignored = true })
        end

        for _, dep in ipairs(opts.deps or {}) do
            add_deps(dep, { public = true })
        end
        for _, pkg in ipairs(opts.packages or {}) do
            add_packages(pkg, { public = true })
        end
        if opts.extra_setup then
            opts.extra_setup()
        end
        target_end()
end

includes("./Core/xmake.lua")
includes("./RHI/xmake.lua")
includes("./RenderGraph/xmake.lua")
includes("./UI/xmake.lua")
includes("./Scene/xmake.lua")
includes("./ECS/xmake.lua")
includes("./Resource/xmake.lua")
includes("./Render3D/xmake.lua")
includes("./Physics/xmake.lua")
includes("./Host/xmake.lua")
includes("./Editor/xmake.lua")

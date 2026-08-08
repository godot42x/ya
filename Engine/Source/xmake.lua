-- ============================================================================
-- Engine module aggregator.
--
-- Source is organized into three product tiers, each module living in its own
-- directory with its own xmake.lua:
--
--   Foundation/   shared infrastructure (Core / RHI / RHI+Backend)
--   Framework/    product lines (GUI framework; Game: scene/resource/render/
--                 gameplay/physics)
--   Product/      assembled products (Host runtime shell / Editor)
--
-- This file only wires the tiers up and provides one thin helper (`ya_module`)
-- that removes copy-paste for the shared parts:
--   * export-macro plumbing (YA_SHARED / YA_MODULE_BUILD, see Core/Api.h)
--   * the common include root (Engine/Source)
--   * per-module unity grouping + source globbing
-- Each module globs its own sources ("**.cpp") and never reaches outside its
-- directory, so the target list stays in sync with the directory layout.
-- Module-specific wiring (deps / packages / shader codegen / unity exclusions)
-- is declared in each module's own xmake.lua, not flattened here.
-- ============================================================================

-- Engine/Source, captured before module files are included below (each module
-- xmake.lua runs with its own scriptdir, so paths must be derived from here).
local YA_SOURCE_ROOT = os.scriptdir()

function ya_module(name, root, opts)
    opts = opts or {}
    local exclude     = opts.exclude or ""
    local includeRoot = opts.include_root or ".."

    target(name)
        set_kind(opts.kind or "static")
        -- Export plumbing (see Core/Api.h): xmake stamps both defines, so no
        -- module hand-writes dllexport/dllimport.
        add_defines("YA_SHARED=1")
        add_defines("YA_MODULE_BUILD=1")
        add_includedirs(includeRoot, { public = true })

        if opts.shader_generated then
            -- Generated Slang/GLSL headers (Common.Limits.*, Sprite2D.*, ...)
            -- are consumed through RenderDefines.h and the UI shaders.
            local engineRoot = path.join(YA_SOURCE_ROOT, "..")
            add_includedirs(path.join(engineRoot, "Shader/Slang/Generated"), { public = true })
            add_includedirs(path.join(engineRoot, "Shader/GLSL/Generated"), { public = true })
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

-- Foundation tier: shared infrastructure consumed by every product line.
includes("./Foundation/Core/xmake.lua")
includes("./Foundation/RHI/xmake.lua")

-- Framework tier: product lines. GUI framework first (self-contained);
-- Game depends on Foundation + GUI (Node scene-tree base lives in GUI).
includes("./Framework/GUI/xmake.lua")
includes("./Framework/Game/Scene/Scene3D/xmake.lua")
includes("./Framework/Game/Resource/xmake.lua")
includes("./Framework/Game/Render/Graph/xmake.lua")
includes("./Framework/Game/Render/Render3D/xmake.lua")
includes("./Framework/Game/Gameplay/ECS/xmake.lua")
includes("./Framework/Game/Physics/xmake.lua")

-- Product tier: assembled runtimes.
includes("./Product/Host/xmake.lua")
includes("./Product/Editor/xmake.lua")

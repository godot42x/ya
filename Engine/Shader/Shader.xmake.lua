-- Shader manifest: every shader source belongs to exactly one consumption
-- group. The engine profile generates all groups; the gui profile generates
-- only shader-common (limits/layout shared by every profile) and shader-gui
-- (Sprite2D), so no 3D shader is compiled, generated or packaged for GUI.
--   common    Slang/Common/** + GLSL/Common/**        -> Generated/Common/
--   gui       Sprite2D.slang + Sprite2DLine.slang     -> Generated/
--   render3d  remaining Slang/** + GLSL/**             -> Generated/
--   test      GLSL/Test/**                             -> Generated/
local SHADER_MANIFEST = {
    common = {
        slang = { "Engine/Shader/Slang/Common/" },
        glsl  = { "Engine/Shader/GLSL/Common/" },
    },
    gui = {
        slang = {
            "Engine/Shader/Slang/Sprite2D.slang",
            "Engine/Shader/Slang/Sprite2DLine.slang",
        },
        glsl = {},
    },
    render3d = {
        -- Everything that is not claimed by another group.
        slang = { "Engine/Shader/Slang/" },
        glsl  = { "Engine/Shader/GLSL/" },
    },
    test = {
        slang = {},
        glsl  = { "Engine/Shader/GLSL/Test/" },
    },
}

local SHADER_GROUPS_ORDER = { "common", "gui", "render3d", "test" }

local function _profile_groups(profile)
    if profile == "gui" then
        return { "common", "gui" }
    end
    return SHADER_GROUPS_ORDER
end

local function _collect_group_files(group)
    local slangFiles = {}
    local glslFiles  = {}
    for _, pat in ipairs(SHADER_MANIFEST[group].slang) do
        table.join2(slangFiles, os.files(pat .. "**.slang"))
    end
    for _, pat in ipairs(SHADER_MANIFEST[group].glsl) do
        table.join2(glslFiles, os.files(pat .. "**.glsl"))
    end
    table.sort(slangFiles)
    table.sort(glslFiles)
    return slangFiles, glslFiles
end

-- render3d is the catch-all group: it claims every file not owned by
-- common/gui/test.
local function _collect_render3d_files()
    local owned = {}
    for _, group in ipairs({ "common", "gui", "test" }) do
        local s, g = _collect_group_files(group)
        for _, f in ipairs(s) do
            owned[f] = true
        end
        for _, f in ipairs(g) do
            owned[f] = true
        end
    end
    local slangFiles = {}
    for _, f in ipairs(os.files("Engine/Shader/Slang/**.slang")) do
        if not owned[f] then
            table.insert(slangFiles, f)
        end
    end
    local glslFiles = {}
    for _, f in ipairs(os.files("Engine/Shader/GLSL/**.glsl")) do
        if not owned[f] then
            table.insert(glslFiles, f)
        end
    end
    table.sort(slangFiles)
    table.sort(glslFiles)
    return slangFiles, glslFiles
end

local function _collect_groups_files(groups)
    local slangFiles = {}
    local glslFiles  = {}
    for _, group in ipairs(groups) do
        local s, g
        if group == "render3d" then
            s, g = _collect_render3d_files()
        else
            s, g = _collect_group_files(group)
        end
        table.join2(slangFiles, s)
        table.join2(glslFiles, g)
    end
    table.sort(slangFiles)
    table.sort(glslFiles)
    return slangFiles, glslFiles
end

local function _shader_codegen_inputs(groups)
    local files = {
        "Engine/Shader/slang_gen_header.py",
        "Engine/Shader/glsl_gen_header.py",
        "Engine/Shader/shader_config.py",
        "requirements.txt",
    }
    local slangFiles, glslFiles = _collect_groups_files(groups)
    table.join2(files, slangFiles)
    table.join2(files, glslFiles)
    table.sort(files)
    return files
end

local function _make_shader_codegen_runner(run_command, uv, python)
    if uv then
        return function(script, args)
            local uvArgs = {
                "run",
                "--offline",
                "--with-requirements",
                "./requirements.txt",
                "python",
                script,
            }
            table.join2(uvArgs, args)
            run_command(uv.program, uvArgs)
        end
    end

    if python then
        return function(script, args)
            local pythonArgs = { script }
            table.join2(pythonArgs, args)
            run_command(python.program, pythonArgs)
        end
    end

    assert(false, "uv or python3/python not found for shader codegen")
end

local function _run_shader_codegen(run_script, groups)
    local now    = os.mclock()

    do
        run_script("Engine/Shader/shader_config.py", {
            "--config", "Engine/Config/Engine.jsonc",
            "--glsl-output", "Engine/Shader/GLSL/Common/Limits.glsl",
            "--slang-output", "Engine/Shader/Slang/Common/Limits.slang",
        })
    end

    for _, group in ipairs(groups) do
        local slangFiles, glslFiles
        if group == "render3d" then
            slangFiles, glslFiles = _collect_render3d_files()
        else
            slangFiles, glslFiles = _collect_group_files(group)
        end

        -- The common group is the shared generated-interface for every
        -- profile: it lives in its own Generated/Common subdirectory so the
        -- RHI target only exposes that sub-root, never the whole Generated
        -- tree.
        local slangOut = group == "common" and "Engine/Shader/Slang/Generated/Common"
                        or "Engine/Shader/Slang/Generated"
        local glslOut  = group == "common" and "Engine/Shader/GLSL/Generated/Common"
                        or "Engine/Shader/GLSL/Generated"

        if #slangFiles > 0 then
            local args = {
                "--output-dir", slangOut,
                "--include-dir", "Engine/Shader/Slang",
                "--slang-root", "Engine/Shader/Slang",
            }
            for _, f in ipairs(slangFiles) do
                table.insert(args, f)
            end
            run_script("Engine/Shader/slang_gen_header.py", args)
        end

        if #glslFiles > 0 then
            local args = {
                "--output-dir", glslOut,
                "--namespace", "ya::glsl_types",
                "--include-dir", "Engine/Shader/GLSL",
            }
            for _, f in ipairs(glslFiles) do
                table.insert(args, f)
            end
            run_script("Engine/Shader/glsl_gen_header.py", args)
        end
    end

    local cost = os.mclock() - now
    print("ya-shader cost: ", cost, "ms")
end

rule("ya.shader.codegen")
do
    on_prepare(function(target)
        import("core.project.depend")
        import("lib.detect.find_tool")
        import("utils.progress")

        local dependfile = path.join(target:autogendir(), "rules", "ya", "shader_codegen.d")
        local uv = find_tool("uv")
        local python = find_tool("python3") or find_tool("python")
        local runScript = _make_shader_codegen_runner(os.vrunv, uv, python)
        os.mkdir(path.directory(dependfile))
        local profile = get_config("ya_profile") or "engine"
        local groups  = _profile_groups(profile)

        depend.on_changed(function()
            progress.show(0, "${color.build.object}generating.shader %s", target:name())
            _run_shader_codegen(runScript, groups)
        end, {
            files = _shader_codegen_inputs(groups),
            dependfile = dependfile,
        })
    end)
end

task("ya-shader")
do
    set_menu {
    }

    on_run(function()
        import("lib.detect.find_tool")

        local uv = find_tool("uv")
        local python = find_tool("python3") or find_tool("python")
        local runScript = _make_shader_codegen_runner(os.execv, uv, python)
        local profile = get_config("ya_profile") or "engine"
        _run_shader_codegen(runScript, _profile_groups(profile))
    end)
end

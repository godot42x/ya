-- Shader manifest: every shader source belongs to exactly one consumption
-- group. The engine profile generates all groups; the gui profile generates
-- only shader-common (limits/layout shared by every profile) and shader-gui
-- (Sprite2D), so no 3D shader is compiled, generated or packaged for GUI.
--   common    Slang/Common/**                         -> Generated/Common/
--   gui       Sprite2D.slang + Sprite2DLine.slang     -> Generated/
--   render3d  remaining Slang/**                      -> Generated/
--
-- Slang is the only shader language of the engine: the GLSL/shaderc backend was
-- retired, so there is no second source axis and no glsl_gen_header step.
local SHADER_MANIFEST = {
    common = {
        slang = { "Engine/Shader/Slang/Common/" },
    },
    gui = {
        slang = {
            "Engine/Shader/Slang/Sprite2D.slang",
            "Engine/Shader/Slang/Sprite2DLine.slang",
        },
    },
    render3d = {
        -- Everything that is not claimed by another group.
        slang = { "Engine/Shader/Slang/" },
    },
}

local SHADER_GROUPS_ORDER = { "common", "gui", "render3d" }

local function _profile_groups(profile)
    if profile == "gui" then
        return { "common", "gui" }
    end
    return SHADER_GROUPS_ORDER
end

local function _collect_group_files(group)
    local slangFiles = {}
    for _, pat in ipairs(SHADER_MANIFEST[group].slang) do
        table.join2(slangFiles, os.files(pat .. "**.slang"))
    end
    table.sort(slangFiles)
    return slangFiles
end

-- render3d is the catch-all group: it claims every file not owned by
-- common/gui.
local function _collect_render3d_files()
    local owned = {}
    for _, group in ipairs({ "common", "gui" }) do
        for _, f in ipairs(_collect_group_files(group)) do
            owned[f] = true
        end
    end
    local slangFiles = {}
    for _, f in ipairs(os.files("Engine/Shader/Slang/**.slang")) do
        if not owned[f] then
            table.insert(slangFiles, f)
        end
    end
    table.sort(slangFiles)
    return slangFiles
end

local function _collect_groups_files(groups)
    local slangFiles = {}
    for _, group in ipairs(groups) do
        local files
        if group == "render3d" then
            files = _collect_render3d_files()
        else
            files = _collect_group_files(group)
        end
        table.join2(slangFiles, files)
    end
    table.sort(slangFiles)
    return slangFiles
end

local function _shader_codegen_inputs(groups)
    local files = {
        "Engine/Shader/slang_gen_header.py",
        "Engine/Shader/shader_config.py",
        "requirements.txt",
    }
    table.join2(files, _collect_groups_files(groups))
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
            "--slang-output", "Engine/Shader/Slang/Common/Limits.slang",
        })
    end

    for _, group in ipairs(groups) do
        local slangFiles
        if group == "render3d" then
            slangFiles = _collect_render3d_files()
        else
            slangFiles = _collect_group_files(group)
        end

        -- The common group is the shared generated-interface for every
        -- profile: it lives in its own Generated/Common subdirectory so the
        -- RHI target only exposes that sub-root, never the whole Generated
        -- tree.
        local slangOut = group == "common" and "Engine/Shader/Slang/Generated/Common"
                        or "Engine/Shader/Slang/Generated"

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

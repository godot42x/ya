local function _shader_codegen_inputs()
    local files = {
        "Engine/Shader/slang_gen_header.py",
        "Engine/Shader/glsl_gen_header.py",
        "Engine/Shader/shader_config.py",
        "requirements.txt",
    }
    table.join2(files, os.files("Engine/Shader/Slang/**.slang"))
    table.join2(files, os.files("Engine/Shader/GLSL/**.glsl"))
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

local function _run_shader_codegen(run_script)
    local now    = os.mclock()

    do
        run_script("Engine/Shader/shader_config.py", {
            "--config", "Engine/Config/Engine.jsonc",
            "--glsl-output", "Engine/Shader/GLSL/Common/Limits.glsl",
            "--slang-output", "Engine/Shader/Slang/Common/Limits.slang",
        })
    end

    do
        local script    = "Engine/Shader/slang_gen_header.py"
        local outputDir = "Engine/Shader/Slang/Generated"
        local args      = {
            "--output-dir", outputDir,
            "--include-dir", "Engine/Shader/Slang",
            "--slang-root", "Engine/Shader/Slang",
        }
        for _, f in ipairs(os.files("Engine/Shader/Slang/**.slang")) do
            table.insert(args, f)
        end
        run_script(script, args)
    end

    do
        local script    = "Engine/Shader/glsl_gen_header.py"
        local outputDir = "Engine/Shader/GLSL/Generated"
        local args      = {
            "--output-dir", outputDir,
            "--namespace", "ya::glsl_types",
            "--include-dir", "Engine/Shader/GLSL",
        }
        for _, f in ipairs(os.files("Engine/Shader/GLSL/**.glsl")) do
            table.insert(args, f)
        end
        run_script(script, args)
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

        depend.on_changed(function()
            progress.show(0, "${color.build.object}generating.shader %s", target:name())
            _run_shader_codegen(runScript)
        end, {
            files = _shader_codegen_inputs(),
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
        _run_shader_codegen(runScript)
    end)
end

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
        assert(uv or python, "uv or python3/python not found for shader codegen")
        os.mkdir(path.directory(dependfile))

        depend.on_changed(function()
            progress.show(0, "${color.build.object}generating.shader %s", target:name())
            _run_shader_codegen(function(script, args)
                if uv then
                    local uvArgs = {
                        "run",
                        "--with-requirements",
                        "./requirements.txt",
                        "python",
                        script,
                    }
                    table.join2(uvArgs, args)
                    os.vrunv(uv.program, uvArgs)
                    return
                end

                local pythonArgs = { script }
                table.join2(pythonArgs, args)
                os.vrunv(python.program, pythonArgs)
            end)
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
        assert(uv or python, "uv or python3/python not found for shader codegen")
        _run_shader_codegen(function(script, args)
            if uv then
                local uvArgs = {
                    "run",
                    "--with-requirements",
                    "./requirements.txt",
                    "python",
                    script,
                }
                table.join2(uvArgs, args)
                os.execv(uv.program, uvArgs)
                return
            end

            local pythonArgs = { script }
            table.join2(pythonArgs, args)
            os.execv(python.program, pythonArgs)
        end)
    end)
end

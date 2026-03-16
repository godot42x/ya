task("ya-shader")
do
    set_menu {
    }

    on_run(function()
        local now    = os.mclock()
        local python = "python"
        local useUv  = os.execv("uv", { "--version" }, { stdout = os.nuldev(), stderr = os.nuldev() }) == 0

        local function run_python(script, args)
            if useUv then
                local uvArgs = {
                    "run",
                    "--with-requirements",
                    "./requirements.txt",
                    "python",
                    script,
                }
                for _, arg in ipairs(args) do
                    table.insert(uvArgs, arg)
                end
                os.execv("uv", uvArgs)
                return
            end

            local pythonArgs = { script }
            for _, arg in ipairs(args) do
                table.insert(pythonArgs, arg)
            end
            os.execv(python, pythonArgs)
        end

        -- Step 0: generate Common/Limits.glsl from Engine.json (shader.defines)
        do
            local script = "Engine/Shader/shader_config.py"
            local args = {
                "--config", "Engine/Config/Engine.jsonc",
                "--glsl-output", "Engine/Shader/GLSL/Common/Limits.glsl",
                "--slang-output", "Engine/Shader/Slang/Common/Limits.slang",
            }
            run_python(script, args)
        end

        -- Step 1: slang -> C++ header (single Python process for all files)
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
            run_python(script, args)
        end

        -- Step 2: glsl -> C++ header (single Python process for all files)
        do
            local script    = "Engine/Shader/glsl_gen_header.py"
            local outputDir = "Engine/Shader/GLSL/Generated"
            local args      = {
                "--output-dir", outputDir,
                "--namespace", "ya::glsl_types",
                "--config", "Engine/Config/Engine.jsonc",
                "--include-dir", "Engine/Shader/GLSL",
            }
            for _, f in ipairs(os.files("Engine/Shader/GLSL/**.glsl")) do
                table.insert(args, f)
            end
            run_python(script, args)
        end
        local cost = os.mclock() - now
        print("ya-shader cost: ", cost, "ms")
    end)
end

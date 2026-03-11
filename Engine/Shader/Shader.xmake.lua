
target("shader")
do
    set_kind("object")
    set_group("shader")

    on_build(function(target)
        -- Step 0: generate Common/Limits.glsl from Engine.json (shader.defines)
        do
            import("core.base.json")
            local configPath = "Engine/Config/Engine.json"
            local limitsPath = "Engine/Shader/GLSL/Common/Limits.glsl"
            local cfg = json.loadfile(configPath) or {}
            local defines = (cfg.shader or {}).defines or {}

            local function to_define_literal(v)
                if type(v) == "number" then
                    local iv = math.tointeger(v)
                    if iv ~= nil then
                        return tostring(iv)
                    end
                end
                return tostring(v)
            end

            -- Build the file content (#undef before #define ensures override safety)
            local function build_limits_content(path)
                local header_lines = {
                    "// Auto-generated from " .. path .. " — DO NOT EDIT.",
                    "// Modify Engine.json shader.defines instead.",
                    "",
                }
                for name, val in pairs(defines) do
                    table.insert(header_lines, "#undef " .. name)
                    table.insert(header_lines, "#define " .. name .. " " .. to_define_literal(val))
                end
                table.insert(header_lines, "")
                return table.concat(header_lines, "\n")
            end

            -- Write GLSL version
            local glslContent = build_limits_content(configPath)
            local existing = io.readfile(limitsPath)
            if existing ~= glslContent then
                io.writefile(limitsPath, glslContent)
                print("[shader] Generated " .. limitsPath)
            end

            -- Write Slang version (identical syntax, Slang preprocessor is compatible)
            local slangLimitsPath = "Engine/Shader/Slang/Common/Limits.slang"
            local slangContent = build_limits_content(configPath)
            local existingSlang = io.readfile(slangLimitsPath)
            if existingSlang ~= slangContent then
                os.mkdir(path.directory(slangLimitsPath))
                io.writefile(slangLimitsPath, slangContent)
                print("[shader] Generated " .. slangLimitsPath)
            end
        end

        -- Step 1: slang -> C++ header (single Python process for all files)
        do
            local script    = "Engine/Shader/slang_gen_header.py"
            local outputDir = "Engine/Shader/Slang/Generated"
            local args = {
                script,
                "--output-dir", outputDir,
                "--include-dir", "Engine/Shader/Slang",
            }
            for _, f in ipairs(os.files("Engine/Shader/Slang/**.slang")) do
                if not f:find("[/\\]Common[/\\]") then
                    table.insert(args, f)
                end
            end
            os.execv("python", args)
        end

        -- Step 2: glsl -> C++ header (single Python process for all files)
        do
            local script    = "Engine/Shader/glsl_gen_header.py"
            local outputDir = "Engine/Shader/GLSL/Generated"
            local args = {
                script,
                "--output-dir", outputDir,
                "--namespace",  "ya::glsl_types",
                "--config",     "Engine/Config/Engine.json",
                "--include-dir", "Engine/Shader/GLSL",
            }
            for _, f in ipairs(os.files("Engine/Shader/GLSL/**.glsl")) do
                table.insert(args, f)
            end
            os.execv("python", args)
        end
    end)
end


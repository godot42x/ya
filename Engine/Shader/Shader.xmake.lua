
target("shader")
do
    set_kind("object")
    set_group("shader")

    on_build(function(target)
        -- slang -> C++ header (single Python process for all files)
        do
            local script    = "Engine/Shader/slang_gen_header.py"
            local outputDir = "Engine/Shader/Slang/Generated"
            local args = { script, "--output-dir", outputDir }
            for _, f in ipairs(os.files("Engine/Shader/Slang/**.slang")) do
                table.insert(args, f)
            end
            os.execv("python", args)
        end

        -- glsl -> C++ header (single Python process for all files)
        do
            local script    = "Engine/Shader/glsl_gen_header.py"
            local outputDir = "Engine/Shader/GLSL/Generated"
            local args = { script, "--output-dir", outputDir, "--namespace", "ya::glsl_types" }
            for _, f in ipairs(os.files("Engine/Shader/GLSL/**.glsl")) do
                table.insert(args, f)
            end
            os.execv("python", args)
        end
    end)
end


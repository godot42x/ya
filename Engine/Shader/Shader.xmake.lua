rule("glslc.combined")
do
    set_extensions(".glsl")
    on_build_file(function(target, sourcefile, opt)
        -- local cmd = string.format("xmake glslc-combined-to-spv -f %s", sourcefile)
        local separator = "#next_shader_stage"
        local shaderStoragePath = "Engine/Shader/GLSL"
        local cachePath = "Engine/Intermediate/Shader/GLSL"

        local function preprocessCombinedSource(filepath)
            -- Read file content
            -- print("Reading shader file:", filepath)
            local content = io.readfile(filepath)
            local len = 0
            if not content then
                print("ERROR: Failed to read shader file:", filepath)
                return {}
            end

            local shaderSources = {}
            -- 捕获最后一段之前的内容，split by separator
            for stage in content:gmatch("(.-)" .. separator) do
                -- 调试输出前30字符
                -- print("Section Start:")
                -- print(stage:sub(1, 30) .. (stage:len() > 30 and "..." or ""))
                -- print("----")
                len = len + 1
                local stageName, content = stage:match("#type%s+(%w+)%s+(.*)")
                -- print("Stage Name:", stageName)
                -- print("Content Start:")
                -- print(content:sub(1, 30) .. (content:len() > 30 and "..." or ""))
                local _, count = content:gsub("\n", "\n")
                len = len + count
                content = string.rep("\n", len) .. content
                shaderSources[stageName] = content
            end
            -- print("xxxxxxxxxxxxxxxxxxxx")
            -- 2. 捕获最后一段（分隔符之后的内容）
            local last_section = content:match(".*" .. separator .. "(.*)$")
            len = len + 2
            if last_section and last_section:len() > 0 then
                -- print("Last Section:")
                -- print(last_section:sub(1, 30) .. (last_section:len() > 30 and "..." or ""))

                -- print("Stage Name:", stageName)
                -- print("Content Start:")
                -- print(content:sub(1, 30) .. (content:len() > 30 and "..." or ""))

                local stageName, content = last_section:match("#type%s+(%w+)%s+(.*)")
                local _, count = content:gsub("\n", "\n")
                content = string.rep("\n", len) .. content
                len = len + count

                shaderSources[stageName] = content
            end
            return shaderSources
        end
        -- print(sourcefile)
        local sources = preprocessCombinedSource(sourcefile)
        local filename = sourcefile:sub(1, -6)          -- remove .glsl
        filename = filename:sub(#shaderStoragePath + 2) -- remove shaderStoragePath and leading slash
        print(filename, ":")

        local extensionMap = {
            vertex = "vert",
            fragment = "frag",
            compute = "comp",
            geometry = "geom",
            tessellation_control = "tesc",
            tessellation_evaluation = "tese",
        }

        for stage, source in pairs(sources) do
            -- print("Found %s shader:", stage)
            -- print(string.sub(source, 1, 100))

            local ext = extensionMap[stage]

            local glslFilename = string.format("%s.%s.glsl", filename, ext)
            local glslPath = path.join(cachePath, glslFilename)
            io.writefile(glslPath, source)


            local spvFileName = string.format("%s.%s.spv", filename, ext)
            local spvPath = path.join(cachePath, spvFileName)

            local transferPath = function(content)
                -- convert cache_path to storage path
                content = string.gsub(content, "\\", "/")

                -- Engine/Shader/GLSL/Test/HelloTexture.fragment.glsl:72
                -- remove fragment or vertex or something before .glsl
                content = string.gsub(content, ".fragment", "")
                content = string.gsub(content, ".vertex", "")
                content = string.gsub(content, cachePath, shaderStoragePath)
                return content
            end


            try {
                function()
                    -- print("Compiling ", glslPath, "to SPIR-V:", spvPath)
                    local cmd = string.format("glslc -fshader-stage=%s\t -o %s \t%s", stage, spvPath, glslPath)
                    print(cmd)
                    os.run(cmd)
                end,
                catch {
                    function(errors)
                        print("Execption:")
                        if errors and #errors > 0 then
                            -- convert cache_path to storage path
                            errors = transferPath(errors)
                            print("error:")
                            print(errors)
                        end


                        print("Failed to compile", glslPath)
                        print("==================================================================")
                        os.raise("Aborting due to compilation error")
                    end,
                },
            }
        end
    end)
end


local function processSlangSource(sourcefile, outputDir)
    -- Incremental build: skip if generated header is newer than source
end

rule("slang.gen-header")
do
    set_extensions(".slang")

    on_build_file(function(target, sourcefile, opt)
        local outputDir  = "Engine/Shader/Slang/Generated"
        local script     = "Engine/Shader/slang_gen_header.py"

        local basename   = path.filename(sourcefile) -- e.g. PhongLit.slang
        local headerFile = path.join(outputDir, basename .. ".h")
        if os.isfile(headerFile) then
            local srcTime = os.mtime(sourcefile)
            local hdrTime = os.mtime(headerFile)
            if hdrTime >= srcTime then
                return
            end
        end

        os.execv("python", { script, sourcefile, outputDir })
    end)
end



target("shader")
do
    set_kind("object")
    -- add_rules("glslc.combined")
    add_rules("slang.gen-header")
    set_group("shader")
    add_files("./Slang/**.slang")
end


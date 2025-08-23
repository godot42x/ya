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

target("shader")
do
    set_kind("object")
    add_rules("glslc.combined")
    set_group("shader")
    add_files("./**.glsl")
end


-- task("glslc-combined-to-spv")
-- do
--     set_menu {
--         usage = "xmake glslc-combined-to-spv [filename] [shader_path]",
--         description = "Compile combined GLSL shader to SPIR-V",
--         options = {
--             { "a",  "all",            "k",  false,                             "Grab all files under shader_storage_path" },
--             { "p",  "platform",       "kv", nil,                               "Platform to build for (windows|linux|ios)" },
--             { "f",  "filename",       "kv", nil,                               "Shader filename (without .glsl extension)" },
--             { "ss", "shader_storage", "kv", "Engine/Shader/GLSL",              "Shader storage path" },
--             { "cp", "cache_path",     "kv", "Engine/Intermediate/Shader/GLSL", "Shader cached path" },
--         }
--     }

--     on_run(function()
--         import("core.base.option")


--         local bAll = option.get("all")
--         local _filename = option.get("filename")
--         local shaderStoragePath = option.get("shader_storage")
--         local cachePath = option.get("cache_path")

--         -- Process the shader
--         print("grab all files:", bAll)
--         print("Shader path:", shaderStoragePath)
--         print("Cache path:", cachePath)
--         print("Processing shader:", _filename)

--         os.mkdir(cachePath)

--         local separator = "#next_shader_stage"
--         local bAbort = false

--         local executor = function(fileName)
--             -- Lua version of GLSLProcessor::preprocessCombinedSource (simplified)
--             local function preprocessCombinedSource(filepath)
--                 -- Read file content
--                 -- print("Reading shader file:", filepath)
--                 local content = io.readfile(filepath)
--                 if not content then
--                     print("ERROR: Failed to read shader file:", filepath)
--                     return {}
--                 end

--                 local shaderSources = {}
--                 -- 捕获最后一段之前的内容，split by separator
--                 for stage in content:gmatch("(.-)" .. separator) do
--                     -- 调试输出前30字符
--                     -- print("Section Start:")
--                     -- print(stage:sub(1, 30) .. (stage:len() > 30 and "..." or ""))
--                     -- print("----")
--                     local stageName, content = stage:match("#type%s+(%w+)%s+(.*)")
--                     -- print("Stage Name:", stageName)
--                     -- print("Content Start:")
--                     -- print(content:sub(1, 30) .. (content:len() > 30 and "..." or ""))
--                     shaderSources[stageName] = content
--                 end
--                 -- print("xxxxxxxxxxxxxxxxxxxx")
--                 -- 2. 捕获最后一段（分隔符之后的内容）
--                 local last_section = content:match(".*" .. separator .. "(.*)$")
--                 if last_section and last_section:len() > 0 then
--                     -- print("Last Section:")
--                     -- print(last_section:sub(1, 30) .. (last_section:len() > 30 and "..." or ""))
--                     local stageName, content = last_section:match("#type%s+(%w+)%s+(.*)")
--                     shaderSources[stageName] = content
--                 end



--                 return shaderSources
--             end



--             local filepath = path.join(shaderStoragePath, fileName .. ".glsl")
--             local source = preprocessCombinedSource(filepath)
--             if not source or not source["vertex"] or not source["fragment"] then
--                 print("ERROR: No shader stages found in file")
--                 return
--             end


--             -- write to different file
--             for stage, source in pairs(source) do
--                 -- print("Found %s shader:", stage)
--                 -- print(string.sub(source, 1, 100))

--                 local glslFilename = string.format("%s.%s.glsl", fileName, stage)
--                 local glslPath = path.join(cachePath, glslFilename)
--                 io.writefile(glslPath, source)


--                 local spvFileName = string.format("%s.%s.spv", fileName, stage)
--                 local spvPath = path.join(cachePath, spvFileName)

--                 local transferPath = function(content)
--                     -- convert cache_path to storage path
--                     content = string.gsub(content, "\\", "/")
--                     content = string.gsub(content, cachePath, shaderStoragePath)
--                     return content
--                 end


--                 try {
--                     function()
--                         -- print("Compiling ", glslPath, "to SPIR-V:", spvPath)
--                         local cmd = string.format("glslc -fshader-stage=%s\t -o %s \t%s", stage, spvPath, glslPath)
--                         print(cmd)
--                         os.run(cmd)
--                     end,
--                     catch {
--                         function(stderr)
--                             print("Execption:")
--                             if stderr and #stderr > 0 then
--                                 -- convert cache_path to storage path
--                                 stderr = transferPath(stderr)
--                                 print("stderr:")
--                                 print(stderr)
--                             end


--                             print("Failed to compile", glslPath)
--                             print("==================================================================")
--                             bAbort = true
--                         end,
--                     },
--                 }
--             end
--         end

--         if bAll then
--             local files = os.files(path.join(shaderStoragePath, "**.glsl"))
--             -- for _, file in ipairs(files) do
--             --     print(file)
--             -- --
--             for _, file in ipairs(files) do
--                 -- remove last 5 characters
--                 file:gsub("\\", "/")                    -- convert backslashes to forward slashes
--                 -- print(file)
--                 file = file:sub(#shaderStoragePath + 2) -- remove shaderStoragePath and leading slash
--                 file = file:sub(1, -6)                  -- remove .glsl
--                 -- print(file)
--                 executor(file)
--             end
--         else
--             if not _filename or _filename == "" then
--                 print("ERROR: No filename specified")
--                 return
--             end
--             if _filename:match("%.glsl$") then
--                 _filename = _filename:sub(1, -6) -- remove .glsl
--             end
--             executor(_filename)
--         end

--         if bAbort then
--             os.raise("Aborting due to compilation error")
--         end
--     end)
-- end

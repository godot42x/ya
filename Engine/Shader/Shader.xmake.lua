task("ya-shader")
do
    set_menu {
    }

    on_run(function()
        local now    = os.mclock()
        local python = "python"
        local useUv  = os.execv("uv", { "--version" }, { stdout = os.nuldev(), stderr = os.nuldev() }) == 0
        --[[
        TODO: check has network reason and fix python path not work:
        → ~/1/c/ya:(main)↑ $ make r t=HelloMaterial
        xmake b HelloMaterial 
        checking for Xcode SDK ... /Applications/Xcode.app (sdk: 26.4, arm64-apple-macos26.3)
        -- ENABLE UNITY BUILD
        -- ENABLE UNITY BUILD
        -- ENABLE UNITY BUILD
        ya
        error: cannot execv(python Engine/Shader/slang_gen_header.py --output-dir Engine/Shader/Slang/Generated --include-dir Engine/Shader/Slang --slang-root Engine/Shader/Slang Engine/Shader/Slang/DeferredRender/Unified_LightPass.slang Engine/Shader/Slang/DeferredRender/Unified_GBufferPass_Unlit.slang Engine/Shader/Slang/DeferredRender/Unified_GBufferPass_PBR.slang Engine/Shader/Slang/DeferredRender/Types.slang Engine/Shader/Slang/DeferredRender/Unified_GBufferPass_Phong.slang Engine/Shader/Slang/Misc/BasicPostprocessing.slang Engine/Shader/Slang/Misc/CubeMap2PBRIrradianceMap.slang Engine/Shader/Slang/Misc/pbr_generate_brdf_lut.slang Engine/Shader/Slang/Misc/CubeMap2PBRPrefilterEnv.slang Engine/Shader/Slang/Misc/EquidistantCylindrical2CubeMap.slang Engine/Shader/Slang/Misc/PBRCommon.slang Engine/Shader/Slang/Misc/DebugSkinning.slang Engine/Shader/Slang/CombineShadowMappingGenerate.slang Engine/Shader/Slang/PhongLit.slang Engine/Shader/Slang/PBRForward.slang Engine/Shader/Slang/Shadow/PointShadowCull.comp.slang Engine/Shader/Slang/Shadow/PointShadowIndirect.slang Engine/Shader/Slang/Shadow/PointShadowCommon.slang Engine/Shader/Slang/Common/Skinning.slang Engine/Shader/Slang/Common/Helper.slang Engine/Shader/Slang/Common/Limits.slang), No such file or directory
        error: exec(xmake ya-shader) failed(255), unknown reason

        warning: add_cxflags("/Zc:preprocessor") is ignored, please pass `{force = true}` or call `set_policy("check.auto_ignore_flags", false)` if you want to set it.
        ]]

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

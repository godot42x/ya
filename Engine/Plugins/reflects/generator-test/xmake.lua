---@diagnostic disable: undefined-global
-- ============================================================================
-- 反射代码生成测试项目
-- ============================================================================

add_requires("gtest")

--[[
TODO:
使用单独的generator进程来所有文件，会不会更快一点？而不是xmake的depend机制
]] 


-- ============================================================================
-- 反射代码自动生成规则
-- 功能：
--   1. 扫描 .h/.hpp 文件中的 [[refl::uclass]] 标记
--   2. 在编译前自动调用 Python 生成器生成反射代码
--   3. 支持增量编译（只重新生成修改过的文件）
-- ============================================================================
rule("reflects_generator")
-- 设置规则适用的文件扩展名
set_extensions(".h", ".hpp")

-- 在编译每个源文件之前执行
before_buildcmd_file(function(target, batchcmds, sourcefile, opt)
    -- 导入依赖模块
    import("core.base.option")
    import("core.project.depend")

    -- ====================================================================
    -- 步骤 1: 检查文件是否需要生成反射代码
    -- ====================================================================
    local content = io.readfile(sourcefile)
    if not content or not content:find("%[%[refl::uclass%]%]") then
        -- 文件不包含反射标记，跳过
        return
    end

    -- ====================================================================
    -- 步骤 2: 准备路径和文件名
    -- ====================================================================
    local scriptdir = os.scriptdir() -- xmake.lua 所在目录
    local generatorScript = path.join(scriptdir, "../generator/main.py")
    local outputDir = path.join(scriptdir, "intermediate/generates")

    -- 生成输出文件名: common.h -> common.generated.h
    local filename = path.filename(sourcefile)
    local basename = path.basename(sourcefile)
    local outputFile = path.join(outputDir, basename .. ".generated.h")

    -- ====================================================================
    -- 步骤 3: 增量编译检查（避免重复生成未修改的文件）
    -- ====================================================================
    local dependFile = target:dependfile(outputFile)
    local dependInfo = depend.load(dependFile) or {}

    -- 检查源文件和生成器脚本是否被修改
    local bSourceChanged = depend.is_changed(dependInfo, {
        files = { sourcefile, generatorScript },
        values = { filename }
    })

    if not bSourceChanged and os.isfile(outputFile) then
        -- 文件未修改且输出已存在，跳过生成
        return
    end

    -- ====================================================================
    -- 步骤 4: 执行反射代码生成
    -- ====================================================================
    -- 确保输出目录存在
    if not os.isdir(outputDir) then
        os.mkdir(outputDir)
    end

    -- 显示进度并执行 Python 生成器
    batchcmds:show_progress(opt.progress, "generating reflection for %s", filename)
    batchcmds:vrunv("python", {
        generatorScript,
        sourcefile,
        "-o", outputFile
    })

    -- ====================================================================
    -- 步骤 5: 更新依赖信息
    -- ====================================================================
    dependInfo.files = { sourcefile, generatorScript }
    dependInfo.values = { filename }
    depend.save(dependInfo, dependFile)

    -- 添加输出文件到依赖跟踪
    batchcmds:add_depfiles(sourcefile)
    batchcmds:set_depmtime(os.mtime(outputFile))
    batchcmds:set_depcache(dependFile)
end)

-- ============================================================================
-- 反射系统测试目标
-- 用途：测试反射代码生成和运行时反射功能
-- ============================================================================
target("reflects-generator-test")
do
    set_kind("binary")

    -- ========================================================================
    -- 源文件
    -- ========================================================================
    -- C++ 实现文件
    add_files("./src/*.cpp")

    -- 头文件（应用反射生成规则）
    -- 注意：只有包含 [[refl::uclass]] 的头文件才会触发反射代码生成
    add_files("./src/*.h", { rules = "reflects_generator" })

    -- ========================================================================
    -- 包含目录
    -- ========================================================================
    add_includedirs("src", { public = true })                      -- 源代码目录
    add_includedirs("./intermediate/generates", { public = true }) -- 生成的反射代码目录
    add_includedirs("../core/src", { public = true })              -- 反射核心库源码
    add_includedirs("../core/includes", { public = true })         -- 反射核心库头文件

    -- ========================================================================
    -- 依赖
    -- ========================================================================
    add_deps("reflects-core") -- 反射运行时库
    add_packages("gtest")     -- Google Test 测试框架

    -- ========================================================================
    -- 编译选项
    -- ========================================================================
    set_languages("cxx20") -- 使用 C++20 标准
end

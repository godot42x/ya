//
// Shader/GLSLProcessor.cpp
// GLSL-specific shader compilation via shaderc
//

#include "../Shader.h"

#include "Render/Shader/ShaderInternal.h"

#include <shaderc/shaderc.h>
#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#include <sstream>
#include <unordered_map>

#include "utility.cc/string_utils.h"
#include "Core/System/VirtualFileSystem.h"

namespace ya
{

namespace
{
constexpr std::string_view kEolChars =
#if _WIN32
    "\r\n";
#elif defined(__linux__) || defined(__APPLE__)
    "\n";
#else
    #error "Unsupported platform for kEolChars"
#endif

using shader_internal::ShaderStageSource;
} // namespace

namespace EShaderStage
{
shaderc_shader_kind toShadercType(EShaderStage::T Stage)
{
    switch (Stage) {
    case Vertex:
        return shaderc_glsl_vertex_shader;
    case Fragment:
        return shaderc_glsl_fragment_shader;
    case Geometry:
        return shaderc_glsl_geometry_shader;
    case Compute:
        return shaderc_glsl_compute_shader;
    case Task:
    case Mesh:
        YA_CORE_ASSERT(false, "Task/Mesh shader stages are not supported by shaderc GLSL path; use Slang");
        return shaderc_shader_kind(0);
    default:
        UNREACHABLE();
        break;
    }

    YA_CORE_ASSERT(false, "Unknown shader type!");
    return shaderc_shader_kind(0);
}
} // namespace EShaderStage



// ============================================================
// GLSL Compilation Helpers
// ============================================================

auto getOption(bool bOptimized)
{
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetTargetSpirv(shaderc_spirv_version_1_3);
    const bool optimize = true;
    if (optimize) {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
    }
    options.AddMacroDefinition("YA_PLATFORM_VULKAN", "1");
    return options;
}

static const char* getStageMacroName(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return "SHADER_STAGE_VERTEX";
    case EShaderStage::Fragment:
        return "SHADER_STAGE_FRAGMENT";
    case EShaderStage::Geometry:
        return "SHADER_STAGE_GEOMETRY";
    case EShaderStage::Compute:
        return "SHADER_STAGE_COMPUTE";
    default:
        return nullptr;
    }
}

struct ShadercIncludeResultData
{
    shaderc_include_result result{};
    std::string            sourceName;
    std::string            content;
};

struct ShadercVfsIncluder : public shaderc::CompileOptions::IncluderInterface
{
    static constexpr std::string_view kGlslBaseDir = "Engine/Shader/GLSL";

    shaderc_include_result* GetInclude(const char* requested_source,
                                       shaderc_include_type /*type*/,
                                       const char* requesting_source,
                                       size_t /*include_depth*/) override
    {
        auto* data = new ShadercIncludeResultData();

        auto reqPath = std::filesystem::path(requested_source ? requested_source : "");
        auto srcPath = std::filesystem::path(requesting_source ? requesting_source : "");

        std::filesystem::path resolvedPath;
        if (reqPath.is_absolute()) {
            resolvedPath = reqPath;
        }
        else {
            resolvedPath = (srcPath.parent_path() / reqPath).lexically_normal();
        }

        data->sourceName = resolvedPath.generic_string();
        bool ok          = false;
        if (VFS::get()->isFileExists(data->sourceName))
        {
            ok = VirtualFileSystem::get()->readFileToString(data->sourceName, data->content);
        }

        if (!ok) {
            auto fallback     = (std::filesystem::path(kGlslBaseDir) / reqPath).lexically_normal();
            auto fallbackName = fallback.generic_string();
            if (VirtualFileSystem::get()->readFileToString(fallbackName, data->content)) {
                data->sourceName = fallbackName;
                ok               = true;
            }
            if (!ok) {
                data->content = std::format("#error \"Failed to include file: {}\" or fallback file: {}", data->sourceName, fallbackName);
                YA_CORE_ERROR("Shader include failed: '{}' or '{}' requested by '{}'", data->sourceName, fallbackName, requesting_source ? requesting_source : "");
            }
        }

        data->result.source_name        = data->sourceName.c_str();
        data->result.source_name_length = data->sourceName.size();
        data->result.content            = data->content.c_str();
        data->result.content_length     = data->content.size();
        data->result.user_data          = data;
        return &data->result;
    }

    void ReleaseInclude(shaderc_include_result* include_result) override
    {
        if (!include_result) {
            return;
        }
        auto* data = static_cast<ShadercIncludeResultData*>(include_result->user_data);
        delete data;
    }
};

// ============================================================
// GLSLProcessor Implementation
// ============================================================

std::filesystem::path GLSLProcessor::GetCachePath(bool bVulkan) const
{
    auto cacheDir = stdpath(intermediateStoragePath) / (bVulkan ? "Vulkan" : "OpenGL");
    auto cacheKey = curFileName.empty() ? curFilePath.generic_string() : curFileName;
    return cacheDir / shader_internal::makeCacheFileName(cacheKey);
}

bool GLSLProcessor::compileToSpv(std::string_view filename, std::string_view content, EShaderStage::T stage, 
                                   const std::vector<std::string>& defines, std::vector<ir_t>& outSpv)
{
    shaderc::Compiler compiler;

    auto options = getOption(false);
    options.SetIncluder(std::make_unique<ShadercVfsIncluder>());

    if (const char* stageMacro = getStageMacroName(stage))
    {
        options.AddMacroDefinition(stageMacro, "1");
    }

    for (const auto& def : defines)
    {
        auto eq = def.find('=');
        if (eq != std::string::npos)
        {
            auto name = ut::str::trim(std::string_view(def).substr(0, eq));
            auto val  = ut::str::trim(std::string_view(def).substr(eq + 1));
            if (!name.empty()) {
                options.AddMacroDefinition(std::string(name), std::string(val.empty() ? "1" : val));
            }
            continue;
        }

        auto ws = def.find_first_of(" \t");
        if (ws != std::string::npos)
        {
            auto name = ut::str::trim(std::string_view(def).substr(0, ws));
            auto val  = ut::str::trim(std::string_view(def).substr(ws + 1));
            if (!name.empty()) {
                options.AddMacroDefinition(std::string(name), std::string(val.empty() ? "1" : val));
            }
            continue;
        }

        auto name = ut::str::trim(std::string_view(def));
        if (!name.empty()) {
            options.AddMacroDefinition(std::string(name), "1");
        }
    }

    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        content.data(),
        EShaderStage::toShadercType(stage),
        filename.data(),
        "main",
        options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        YA_CORE_ERROR("{}:\n{}", EShaderStage::toString(stage), result.GetErrorMessage());
        return false;
    }

    outSpv = spirv_ir_t(result.begin(), result.end());
    return true;
}

std::unordered_map<EShaderStage::T, std::string> GLSLProcessor::preprocessCombinedSource(const stdpath& filepath)
{
    std::unordered_map<EShaderStage::T, std::string> shaderSources;

    std::string contentStr;

    if (!VirtualFileSystem::get()->readFileToString(filepath.string(), contentStr))
    {
        YA_CORE_ERROR("Failed to read shader file: {}", filepath.string());
        return {};
    }

    std::string_view source(contentStr.begin(), contentStr.end());

    const bool hasTypeToken = source.find("#type", 0) != std::string::npos;

    if (!hasTypeToken)
    {
        auto hasStageMacro = [&](std::string_view macro) {
            return source.find(macro) != std::string::npos;
        };

        if (hasStageMacro("SHADER_STAGE_VERTEX")) {
            shaderSources[EShaderStage::Vertex] = contentStr;
        }
        if (hasStageMacro("SHADER_STAGE_FRAGMENT")) {
            shaderSources[EShaderStage::Fragment] = contentStr;
        }
        if (hasStageMacro("SHADER_STAGE_GEOMETRY")) {
            shaderSources[EShaderStage::Geometry] = contentStr;
        }
        if (hasStageMacro("SHADER_STAGE_COMPUTE")) {
            shaderSources[EShaderStage::Compute] = contentStr;
        }

        if (!shaderSources.empty()) {
            return shaderSources;
        }

        YA_CORE_ERROR("Shader source '{}' has no '#type' markers and no SHADER_STAGE_* macros", filepath.string());
        return {};
    }

    std::string_view typeToken    = "#type";
    const size_t     typeTokenLen = typeToken.size();
    size_t           pos          = source.find(typeToken, 0);

    while (pos != std::string::npos)
    {
        size_t eol = source.find_first_of(kEolChars, pos);
        YA_CORE_ASSERT(eol != std::string::npos, "Syntax error");

        size_t           begin = pos + typeTokenLen + 1;
        std::string_view type  = source.substr(begin, eol - begin);
        type                   = ut::str::trim(type);

        EShaderStage::T shader_type = EShaderStage::fromString(type);

        size_t nextLinePos = source.find_first_not_of(kEolChars, eol);
        pos          = source.find(typeToken, nextLinePos);
        size_t count = (nextLinePos == std::string::npos ? source.size() - 1 : nextLinePos);

        size_t lineNumber = std::count(source.begin(), source.begin() + nextLinePos, '\n');

        std::string codes = std::string(source.substr(nextLinePos, pos - count));

        std::string paddedCodes;
        paddedCodes.reserve(lineNumber + codes.size());
        paddedCodes.append(lineNumber, '\n');
        paddedCodes.append(codes);

        auto [_, Ok] = shaderSources.insert(std::make_pair(shader_type, paddedCodes));
        YA_CORE_ASSERT(Ok, "Failed to insert this shader source");
    }
    return shaderSources;
}

bool GLSLProcessor::processSpvFiles(std::string_view vertFile, std::string_view fragFile, stage2spirv_t& outSpvMap)
{
    YA_CORE_INFO("Found spv shader files for {}: {} and {}", curFileName, vertFile, fragFile);

    auto toSpirv = [](std::string_view ctx, const std::string& src, std::vector<ir_t>& spirv) {
        if (src.size() % sizeof(ir_t) != 0)
        {
            YA_CORE_WARN("Cached vertex shader file size is not a multiple of ir_t size: {}", ctx);
            return false;
        }
        spirv.resize(src.size() / sizeof(ir_t));
        std::memcpy(spirv.data(), src.data(), src.size());
        return true;
    };

    std::vector<ir_t> vertSpv;
    std::string       vertFileStr;
    if (!VirtualFileSystem::get()->readFileToString(vertFile, vertFileStr))
    {
        YA_CORE_WARN("Failed to read cached vertex shader file: {}", vertFile);
        return false;
    }
    if (!toSpirv(curFileName, vertFileStr, vertSpv)) {
        YA_CORE_ERROR("Failed to convert cached vertex shader file to SPIR-V: {}", vertFile);
        return false;
    }
    outSpvMap[EShaderStage::Vertex] = std::move(vertSpv);

    std::vector<ir_t> fragSpv;
    std::string       fragFileStr;
    if (!VirtualFileSystem::get()->readFileToString(fragFile, fragFileStr))
    {
        YA_CORE_ERROR("Failed to read cached fragment shader file: {}", fragFile);
        return false;
    }
    if (!toSpirv(curFileName, fragFileStr, fragSpv)) {
        YA_CORE_ERROR("Failed to convert cached fragment shader file to SPIR-V: {}", fragFile);
        return false;
    }
    outSpvMap[EShaderStage::Fragment] = std::move(fragSpv);

    return true;
}

bool GLSLProcessor::processCombinedSource(const stdpath& filepath, const std::vector<std::string>& defines, stage2spirv_t& outSpvMap)
{
    std::unordered_map<EShaderStage::T, std::string> shaderSources = preprocessCombinedSource(filepath);

    if (shaderSources.empty())
    {
        YA_CORE_ERROR("Failed to preprocess shader source: {}", filepath.string());
        return false;
    }

    for (auto&& [stage, source] : shaderSources)
    {
        std::vector<ir_t> spv;
        if (!compileToSpv(filepath.string(), source, stage, defines, spv))
        {
            YA_CORE_ERROR("Failed to compile shader: {}", filepath.string());
            return false;
        }
        outSpvMap[stage] = std::move(spv);
    }

    return true;
}

std::optional<GLSLProcessor::stage2spirv_t> GLSLProcessor::process(const ShaderDesc& ci, EShaderProcessMode mode)
{
    const auto cacheKey = ci.cacheKey();
    YA_CORE_ASSERT(!cacheKey.empty(), "ShaderDesc cache key cannot be empty");

    stage2spirv_t ret;
    uint64_t      sourceHash = 0;

    auto hasValidStageSet = [&](const stage2spirv_t& stageMap) {
        if (stageMap.contains(EShaderStage::Vertex) && stageMap.contains(EShaderStage::Fragment)) {
            return true;
        }
        return stageMap.contains(EShaderStage::Compute) && stageMap.size() == 1;
    };

    auto compileStageSource = [&](const ShaderStageSource& stageSource, const char* errorTag) -> bool {
        std::vector<ir_t> spv;
        if (!compileToSpv(stageSource.path, stageSource.source, stageSource.stage, ci.defines, spv)) {
            YA_CORE_ERROR("Failed to compile {} shader stage: {}", errorTag, stageSource.path);
            return false;
        }
        ret[stageSource.stage] = std::move(spv);
        return true;
    };

    if (ci.sourceMode == ShaderDesc::ESourceMode::StageFiles)
    {
        std::vector<ShaderStageSource> stageSources;
        stageSources.reserve(ci.stageFiles.size());

        for (const auto& stageFile : ci.stageFiles)
        {
            if (ret.contains(stageFile.stage)) {
                YA_CORE_ERROR("Duplicate stage entry in ShaderDesc::stageFiles, stage={}", static_cast<int>(stageFile.stage));
                return {};
            }

            std::filesystem::path stagePath(stageFile.file);
            if (!stagePath.is_absolute()) {
                stagePath = stdpath(shaderStoragePath) / stagePath;
            }

            auto stagePathStr = stagePath.generic_string();
            std::string stageSource;
            if (!VirtualFileSystem::get()->readFileToString(stagePathStr, stageSource)) {
                YA_CORE_ERROR("Failed to read explicit stage-file shader source: {}", stagePathStr);
                return {};
            }

            stageSources.push_back(ShaderStageSource{
                .stage  = stageFile.stage,
                .path   = stagePathStr,
                .source = std::move(stageSource),
            });
            ret[stageFile.stage] = {};
        }

        ret.clear();
        curFileName = cacheKey;
        curFilePath.clear();
        sourceHash = shader_internal::buildShaderSourceHash(stageSources, ci.defines);
        const auto cachePath = GetCachePath(true);
        if (mode == EShaderProcessMode::UseCache && shader_internal::loadShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_INFO("Loaded shader disk cache for {}", cacheKey);
            return {std::move(ret)};
        }

        for (const auto& stageSource : stageSources) {
            if (!compileStageSource(stageSource, "explicit stage-file")) {
                return {};
            }
        }

        if (!hasValidStageSet(ret)) {
            YA_CORE_ERROR("Explicit stage-files mode requires vertex+fragment or compute-only stages: {}", cacheKey);
            return {};
        }

        if (!shader_internal::saveShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_WARN("Failed to write shader disk cache: {}", cachePath.generic_string());
        }
        YA_CORE_INFO("Preprocessed explicit stage-files shader for {}: {} stages found", cacheKey, ret.size());
    }
    else
    {
        std::string shaderName = ci.shaderName;
        YA_CORE_ASSERT(!shaderName.empty(), "SingleShader mode requires shaderName");
        if (!shaderName.ends_with(".glsl")) {
            shaderName += ".glsl";
        }

        curFileName = ut::str::replace(shaderName, ".glsl", "");
        curFilePath = stdpath(shaderStoragePath) / shaderName;

        std::string shaderSource;
        if (!VirtualFileSystem::get()->readFileToString(curFilePath.generic_string(), shaderSource)) {
            YA_CORE_ERROR("Failed to read shader source: {}", curFilePath.generic_string());
            return {};
        }

        sourceHash = shader_internal::buildShaderSourceHash({ShaderStageSource{
            .stage  = EShaderStage::Vertex,
            .path   = curFilePath.generic_string(),
            .source = shaderSource,
        }}, ci.defines);
        const auto cachePath = GetCachePath(true);
        if (mode == EShaderProcessMode::UseCache && shader_internal::loadShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_INFO("Loaded shader disk cache for {}", shaderName);
            return {std::move(ret)};
        }

        ret.clear();
        if (processCombinedSource(curFilePath, ci.defines, ret)) {
            YA_CORE_INFO("Preprocessed shader source for {}: {} stages found", shaderName, ret.size());
        }
        else {
            YA_CORE_ERROR("Failed to preprocess shader source: {}", shaderName);
            return {};
        }

        if (!hasValidStageSet(ret)) {
            YA_CORE_ERROR("SingleShader mode requires vertex+fragment or compute-only stages: {}", shaderName);
            return {};
        }

        if (!shader_internal::saveShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_WARN("Failed to write shader disk cache: {}", cachePath.generic_string());
        }
    }

    for (const auto& [stage, spirv] : ret) {
        if (!shader_internal::isValidSpirvModule(spirv)) {
            YA_CORE_ERROR("Invalid SPIR-V module for stage {}: Missing or incorrect magic number.", EShaderStage::T2Strings[stage]);
            YA_CORE_ASSERT(false, "SPIR-V validation failed for stage {}", EShaderStage::T2Strings[stage]);
        }
    }

    return {std::move(ret)};
}

} // namespace ya

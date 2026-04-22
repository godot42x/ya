//
/*
 * @ Author: godot42
 * @ Create Time: 2025-02-27 01:14:28
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-22 00:40:40
 * @ Description:
 */

#include "Shader.h"

#include "Render/Shader/ShaderInternal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <shaderc/shaderc.h>
#include <shaderc/shaderc.hpp>
#include <stdio.h>
#include <string>
#include <unordered_map>

#include <spirv_cross/spirv.h>
#include <spirv_cross/spirv_cross.hpp>

#include "utility.cc/string_utils.h"

#include <SDL3/SDL_gpu.h>

#include "Core/System/VirtualFileSystem.h"

// Slang runtime API
#include <slang/slang-com-ptr.h>
#include <slang/slang.h>

namespace ya
{
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
    default:
        UNREACHABLE();
        break;
    }

    YA_CORE_ASSERT(false, "Unknown shader type!");
    return shaderc_shader_kind(0);
}

const char* getOpenGLCacheFileExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached.opengl.vert";
    case EShaderStage::Fragment:
        return ".cached.opengl.frag";
    case EShaderStage::Geometry:
        return ".cached.opengl.geom";
    default:
        UNREACHABLE();
        break;
    }
    YA_CORE_ASSERT(false, "Unknown shader type!");
    return "";
}

const char* getVulkanCacheFileExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return ".cached.vulkan.vert";
    case EShaderStage::Fragment:
        return ".cached.vulkan.frag";
    case EShaderStage::Geometry:
        return ".cached.vulkan.geom";
    default:
        UNREACHABLE();
        break;
    }
    YA_CORE_ASSERT(false, "Unknown shader type!");
    return "";
}

const char* getSpvOutputExtension(EShaderStage::T stage)
{
    switch (stage)
    {
    case EShaderStage::Vertex:
        return "vert.spv";
    case EShaderStage::Fragment:
        return "frag.spv";
    case EShaderStage::Geometry:
        return "geom.spv";
    default:
        UNREACHABLE();
        break;
    }
    // YA_CORE_ASSERT(false);
    return "";
}


SDL_GPUShaderStage toSDLStage(EShaderStage::T Stage)
{
    switch (Stage) {
    case Vertex:
        return SDL_GPU_SHADERSTAGE_VERTEX;
    case Fragment:
        return SDL_GPU_SHADERSTAGE_FRAGMENT;
    // case Geometry:
    // return SDL_GPU_SHADERSTAGE_GEOMETRY;
    default:
        break;
    }
    YA_CORE_ASSERT(false, "Unknown shader type!");
    return (SDL_GPUShaderStage)-1;
}
} // namespace EShaderStage

namespace shader_internal
{

constexpr uint64_t SHADER_HASH_OFFSET = 14695981039346656037ull;
constexpr uint64_t SHADER_HASH_PRIME  = 1099511628211ull;

uint64_t hashBytes(uint64_t seed, const void* data, size_t size)
{
    auto hash  = seed;
    auto bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= SHADER_HASH_PRIME;
    }
    return hash;
}

template <typename T>
void hashAppendPod(uint64_t& hash, const T& value)
{
    hash = hashBytes(hash, &value, sizeof(T));
}

void hashAppendString(uint64_t& hash, std::string_view value)
{
    const uint64_t size = value.size();
    hashAppendPod(hash, size);
    if (!value.empty()) {
        hash = hashBytes(hash, value.data(), value.size());
    }
}

uint64_t hashString(std::string_view value)
{
    auto hash = SHADER_HASH_OFFSET;
    hashAppendString(hash, value);
    return hash;
}

std::optional<std::string> resolveShaderIncludePath(std::string_view requestedSource, std::string_view requestingSource)
{
    auto reqPath = std::filesystem::path(requestedSource);
    auto srcPath = std::filesystem::path(requestingSource);

    auto resolvedPath = reqPath.is_absolute()
        ? reqPath
        : (srcPath.parent_path() / reqPath).lexically_normal();
    auto resolvedName = resolvedPath.generic_string();
    if (VFS::get()->isFileExists(resolvedName)) {
        return resolvedName;
    }

    auto fallback = (std::filesystem::path("Engine/Shader/GLSL") / reqPath).lexically_normal();
    auto fallbackName = fallback.generic_string();
    if (VFS::get()->isFileExists(fallbackName)) {
        return fallbackName;
    }
    return std::nullopt;
}

bool appendShaderDependencyHash(const std::string& filePath, std::unordered_set<std::string>& visitedFiles, uint64_t& hash)
{
    auto normalizedPath = std::filesystem::path(filePath).lexically_normal().generic_string();
    if (visitedFiles.contains(normalizedPath)) {
        return true;
    }
    visitedFiles.insert(normalizedPath);

    std::string content;
    if (!VirtualFileSystem::get()->readFileToString(normalizedPath, content)) {
        return false;
    }

    hashAppendString(hash, normalizedPath);
    hashAppendString(hash, content);

    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        auto trimmed = ut::str::trim(std::string_view(line));

        // Parse C-style #include "path" or #include <path>
        if (trimmed.starts_with("#include")) {
            const auto begin = trimmed.find_first_of("\"<");
            if (begin == std::string_view::npos) {
                continue;
            }

            const auto endToken = trimmed[begin] == '<' ? '>' : '"';
            const auto end      = trimmed.find(endToken, begin + 1);
            if (end == std::string_view::npos || end <= begin + 1) {
                continue;
            }

            const auto includeName = trimmed.substr(begin + 1, end - begin - 1);
            auto resolvedPath      = resolveShaderIncludePath(includeName, normalizedPath);
            if (!resolvedPath.has_value()) {
                return false;
            }
            if (!appendShaderDependencyHash(*resolvedPath, visitedFiles, hash)) {
                return false;
            }
            continue;
        }

        // Parse Slang "import Module.Sub;" → "Module/Sub.slang"
        if (trimmed.starts_with("import ")) {
            auto importBody   = trimmed.substr(7); // skip "import "
            auto semicolonPos = importBody.find(';');
            if (semicolonPos == std::string_view::npos) {
                continue;
            }
            auto moduleName = ut::str::trim(importBody.substr(0, semicolonPos));
            if (moduleName.empty()) {
                continue;
            }

            // Convert dot-separated module path to file path: Common.Helper → Common/Helper.slang
            std::string moduleFilePath(moduleName);
            std::replace(moduleFilePath.begin(), moduleFilePath.end(), '.', '/');
            moduleFilePath += ".slang";

            // Resolve: relative to current file, then Engine/Shader/GLSL fallback, then Slang base dir
            auto resolvedPath = resolveShaderIncludePath(moduleFilePath, normalizedPath);
            if (!resolvedPath.has_value()) {
                auto slangFallback    = (std::filesystem::path("Engine/Shader/Slang") / moduleFilePath).lexically_normal();
                auto slangFallbackStr = slangFallback.generic_string();
                if (VFS::get()->isFileExists(slangFallbackStr)) {
                    resolvedPath = slangFallbackStr;
                }
            }
            if (resolvedPath.has_value()) {
                if (!appendShaderDependencyHash(*resolvedPath, visitedFiles, hash)) {
                    return false;
                }
            }
            continue;
        }
    }

    return true;
}

uint64_t buildShaderSourceHash(const std::vector<ShaderStageSource>& stageSources, const std::vector<std::string>& defines)
{
    auto sortedStages = stageSources;
    std::sort(sortedStages.begin(), sortedStages.end(), [](const ShaderStageSource& lhs, const ShaderStageSource& rhs) {
        if (lhs.stage != rhs.stage) {
            return lhs.stage < rhs.stage;
        }
        return lhs.path < rhs.path;
    });

    auto hash = SHADER_HASH_OFFSET;
    for (const auto& define : defines) {
        hashAppendString(hash, define);
    }

    std::unordered_set<std::string> visitedFiles;
    for (const auto& stageSource : sortedStages) {
        const auto stage = static_cast<uint32_t>(stageSource.stage);
        hashAppendPod(hash, stage);
        hashAppendString(hash, stageSource.path);

        if (!stageSource.path.empty() && appendShaderDependencyHash(stageSource.path, visitedFiles, hash)) {
            continue;
        }

        hashAppendString(hash, stageSource.source);
    }
    return hash;
}

std::string sanitizeCacheStem(std::string_view value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        sanitized.push_back(std::isalnum(uch) ? ch : '_');
    }
    while (!sanitized.empty() && sanitized.back() == '_') {
        sanitized.pop_back();
    }
    if (sanitized.empty()) {
        sanitized = "shader";
    }
    constexpr size_t MAX_STEM_LENGTH = 48;
    if (sanitized.size() > MAX_STEM_LENGTH) {
        sanitized.resize(MAX_STEM_LENGTH);
    }
    return sanitized;
}

std::string makeCacheFileName(std::string_view cacheKey)
{
    return std::format("{}-{:016x}.spvcache", sanitizeCacheStem(cacheKey), hashString(cacheKey));
}

bool isValidSpirvModule(const std::vector<ShaderStageIr>& spirv)
{
    return !spirv.empty() && spirv.front() == 0x07230203;
}

std::vector<std::pair<ya::EShaderStage::T, const std::vector<ShaderStageIr>*>> collectSortedStages(const ShaderStageSpirvMap& spvMap)
{
    std::vector<std::pair<ya::EShaderStage::T, const std::vector<ShaderStageIr>*>> stages;
    stages.reserve(spvMap.size());
    for (const auto& [stage, spirv] : spvMap) {
        stages.emplace_back(stage, &spirv);
    }
    std::sort(stages.begin(), stages.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    return stages;
}

bool loadShaderDiskCache(const std::filesystem::path& cachePath, uint64_t expectedHash, ShaderStageSpirvMap& outSpvMap)
{
    outSpvMap.clear();

    std::ifstream input(cachePath, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    ya::ShaderDiskCacheHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!input.good()) {
        return false;
    }
    if (header.magic != ya::ShaderDiskCacheHeader::MAGIC ||
        header.version != ya::ShaderDiskCacheHeader::VERSION ||
        header.sourceHash != expectedHash ||
        header.stageCount == 0) {
        return false;
    }

    std::vector<ya::ShaderDiskCacheStageHeader> stageHeaders(header.stageCount);
    input.read(reinterpret_cast<char*>(stageHeaders.data()), static_cast<std::streamsize>(stageHeaders.size() * sizeof(ya::ShaderDiskCacheStageHeader)));
    if (!input.good()) {
        return false;
    }

    for (const auto& stageHeader : stageHeaders) {
        if (stageHeader.wordCount == 0) {
            outSpvMap.clear();
            return false;
        }

        auto stage = static_cast<ya::EShaderStage::T>(stageHeader.stage);
        if (outSpvMap.contains(stage)) {
            outSpvMap.clear();
            return false;
        }

        std::vector<ShaderStageIr> spirv(stageHeader.wordCount);
        input.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(spirv.size() * sizeof(ShaderStageIr)));
        if (!input.good() || !isValidSpirvModule(spirv)) {
            outSpvMap.clear();
            return false;
        }

        outSpvMap[stage] = std::move(spirv);
    }

    return !outSpvMap.empty();
}

bool saveShaderDiskCache(const std::filesystem::path& cachePath, uint64_t sourceHash, const ShaderStageSpirvMap& spvMap)
{
    const auto stages = collectSortedStages(spvMap);
    if (stages.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        return false;
    }

    auto tempPath = cachePath;
    tempPath += ".tmp";
    std::filesystem::remove(tempPath, ec);
    ec.clear();

    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    ya::ShaderDiskCacheHeader header{};
    header.sourceHash = sourceHash;
    header.stageCount = static_cast<uint32_t>(stages.size());
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));

    std::vector<ya::ShaderDiskCacheStageHeader> stageHeaders;
    stageHeaders.reserve(stages.size());
    for (const auto& [stage, spirv] : stages) {
        if (!isValidSpirvModule(*spirv)) {
            output.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
        stageHeaders.push_back(ya::ShaderDiskCacheStageHeader{
            .stage     = static_cast<uint32_t>(stage),
            .wordCount = static_cast<uint32_t>(spirv->size()),
        });
    }

    output.write(reinterpret_cast<const char*>(stageHeaders.data()), static_cast<std::streamsize>(stageHeaders.size() * sizeof(ya::ShaderDiskCacheStageHeader)));
    for (const auto& [_, spirv] : stages) {
        output.write(reinterpret_cast<const char*>(spirv->data()), static_cast<std::streamsize>(spirv->size() * sizeof(ShaderStageIr)));
    }
    output.flush();
    if (!output.good()) {
        output.close();
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    output.close();

    std::filesystem::rename(tempPath, cachePath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(cachePath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, cachePath, ec);
    }
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}
} // namespace shader_internal

// ============================================================
// ShaderStorage async preloading
// ============================================================

void ShaderStorage::preloadAsync(std::vector<ShaderDesc> shaders)
{
    YA_CORE_ASSERT(!_preloadThread, "preloadAsync called while preload is already in progress");
    _preloadThread = std::make_unique<std::thread>([this, shaders = std::move(shaders)]() {
        YA_PROFILE_SCOPE_LOG("ShaderStorage::preloadAsync");
        for (const auto& desc : shaders) {
            try {
                load(desc);
            }
            catch (const std::exception& e) {
                YA_CORE_ERROR("Preload failed for shader '{}': {}", desc.cacheKey(), e.what());
            }
        }
    });
}

void ShaderStorage::waitForPreload()
{
    if (_preloadThread && _preloadThread->joinable()) {
        YA_PROFILE_SCOPE_LOG("ShaderStorage::waitForPreload");
        _preloadThread->join();
        _preloadThread.reset();
        YA_CORE_INFO("Shader preload completed");
    }
}

} // namespace ya

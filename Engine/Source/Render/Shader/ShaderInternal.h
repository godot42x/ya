#pragma once

#include "Render/Shader.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ya::shader_internal
{

using ShaderStageSpirvMap = IShaderProcessor::stage2spirv_t;
using ShaderStageIr       = IShaderProcessor::ir_t;

struct ShaderStageSource
{
    EShaderStage::T stage = EShaderStage::Vertex;
    std::string     path;
    std::string     source;
};

uint64_t buildShaderSourceHash(const std::vector<ShaderStageSource>& stageSources, const std::vector<std::string>& defines);
std::string makeCacheFileName(std::string_view cacheKey);
bool isValidSpirvModule(const std::vector<ShaderStageIr>& spirv);
bool loadShaderDiskCache(const std::filesystem::path& cachePath, uint64_t expectedHash, ShaderStageSpirvMap& outSpvMap);
bool saveShaderDiskCache(const std::filesystem::path& cachePath, uint64_t sourceHash, const ShaderStageSpirvMap& spvMap);

} // namespace ya::shader_internal

#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>



enum class EShaderStage
{
    Undefined = 0,
    Vertex,
    Fragment,
};



struct GLSLProcessor
{

    const std::string CacheRelativePath = "saved/cache/shader/opengl";


    std::string           m_Name{};
    uint32_t              m_ShaderID{0};
    std::filesystem::path m_FilePath;


  private:
    std::unordered_map<EShaderStage, std::vector<uint32_t>> m_Vulkan_SPIRV;
    std::unordered_map<EShaderStage, std::vector<uint32_t>> m_OpenGL_SPIRV;
    std::unordered_map<EShaderStage, std::string>           m_GLSL_SourceCode;


    std::unordered_map<EShaderStage, std::string> PreProcess(const std::string &source);

    void CreateVulkanBinaries(const std::unordered_map<unsigned int, std::string> &shader_sources, bool bSourceChanged);

    void CreateGLBinaries(bool bSourceChanged);

    void CreateProgram();

    void Reflect(EShaderStage stage, const std::vector<uint32_t> &shader_data);

    std::filesystem::path GetCachePath(bool bVulkan, EShaderStage stage);
    std::filesystem::path GetCacheMetaPath();
};
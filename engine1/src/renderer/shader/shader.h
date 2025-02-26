#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "C:\Users\norm\1\craft\Neon\engine\src\utils\path.h"
#include "C:\Users\norm\1\craft\Neon\engine\src\utils\trait\disable_copy.hpp"


enum class EShaderStage
{
    Undefined = 0,
    Vertex,
    Fragment,
};


namespace std
{
extern const char *to_string(EShaderStage);
}


struct ShaderScriptProcessor
{
  private:
    static std::string           m_BaseCacheRelativePath;
    static std::filesystem::path m_BaseCachedStdPath;

  public:
    void SetBaseCachePath(const std::string &path)
    {
        m_BaseCacheRelativePath = path;
        m_BaseCachedStdPath     = FPath(path);
    }
    const std::filesystem::path GetBaseCachePath() const { return m_BaseCachedStdPath; }


    std::filesystem::path GetCacheMetaPath(std::filesystem::path filepath)
    {
        return GetBaseCachePath() / (filepath.filename().string() + ".cached.meta.json");
    }
};



struct GLSLScriptProcessor : public ShaderScriptProcessor, public disable_copy_move
{


    std::string           m_Name{};
    uint32_t              m_ShaderID{0};
    std::filesystem::path m_FilePath;

  private:
    bool bOptimizeGLBinaries = false;
    bool bValid              = false;

  private:
    std::unordered_map<EShaderStage, std::vector<uint32_t>> m_Vulkan_SPIRV;
    std::unordered_map<EShaderStage, std::vector<uint32_t>> m_OpenGL_SPIRV;
    std::unordered_map<EShaderStage, std::string>           m_GLSL_SourceCode;


  public:
    GLSLScriptProcessor(const std::string &path);
    bool TakeSpv(std::unordered_map<EShaderStage, std::vector<uint32_t>> &spv_binaries);

  private:
    std::unordered_map<EShaderStage, std::string> PreProcess(const std::string &glsl_source);
    void                                          Reflect(EShaderStage stage, const std::vector<uint32_t> &shader_data);

    void CreateGLBinaries(bool bSourceChanged);
    void CreateVulkanBinaries(const std::unordered_map<EShaderStage, std::string> &shader_sources, bool bSourceChanged);

    std::filesystem::path GetCachePath(bool bVulkan, EShaderStage stage);
    std::filesystem::path GetCacheMetaPath();
};

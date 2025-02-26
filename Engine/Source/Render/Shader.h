#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "SDL3/SDL_storage.h"

#include "../Core/FileSystem.h"

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
    friend class ShaderScriptProcessorFactory;

  protected:

    SDL_Storage *cachedStorage;
    std::string  cachedStoragePath = "Engine/Intermediate/Shader/";
    std::string *cachedFileSuffix;
};


struct GLSLScriptProcessor : public ShaderScriptProcessor
{

    friend class ShaderScriptProcessorFactory;

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
    bool TakeSpv(std::unordered_map<EShaderStage, std::vector<uint32_t>> &spv_binaries);

  protected:
    GLSLScriptProcessor() {}

  private:

    std::unordered_map<EShaderStage, std::string> PreProcess(const std::string &glsl_source);
    void                                          Reflect(EShaderStage stage, const std::vector<uint32_t> &shader_data);

    void CreateGLBinaries(bool bSourceChanged);
    void CreateVulkanBinaries(const std::unordered_map<EShaderStage, std::string> &shader_sources, bool bSourceChanged);

    std::filesystem::path GetCachePath(bool bVulkan, EShaderStage stage);
    std::filesystem::path GetCacheMetaPath();
};


struct ShaderScriptProcessorFactory
{
    using Self = ShaderScriptProcessorFactory;

    enum EProcessorType
    {
        GLSL,
        HLSL,
    } processorType;

    std::string cachedStoragePath;
    bool        bSyncCreateStorage;

    Self &withCachedStoragePath(std::string_view dirPath, bool bSyncCreateStorage)
    {
        cachedStoragePath        = dirPath;
        this->bSyncCreateStorage = bSyncCreateStorage;
        return *this;
    }

    ShaderScriptProcessor *FactoryNew()
    {
        ShaderScriptProcessor *processor;

        switch (processorType) {
        case GLSL:
            processor = new GLSLScriptProcessor();
            break;
        case HLSL:
            throw std::runtime_error("unsupported lang");
            break;
        };


        processor->cachedStoragePath = cachedStoragePath;
        processor->cachedStorage     = openFileStorage(cachedStoragePath.c_str(), false);
        return processor;
    }
};

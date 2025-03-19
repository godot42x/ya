#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "SDL3/SDL_storage.h"
#include <SDL3/SDL_gpu.h>


#include "../Core/FileSystem.h"
#include "../Core/Log.h"

namespace EShaderStage
{
enum T
{
    Undefined = 0,
    Vertex,
    Fragment,
};

inline T fromString(std::string_view type)
{
    if (type == "vertex")
        return EShaderStage::Vertex;
    else if (type == "fragment" || type == "pixel")
        return EShaderStage::Fragment;

    NE_CORE_ASSERT(false, "Unknown shader type!");
    return EShaderStage::Undefined;
}

const char *toString(EShaderStage::T Stage);

SDL_GPUShaderStage toSDLStage(EShaderStage::T Stage);

} // namespace EShaderStage


namespace std
{
inline const char *to_string(EShaderStage::T stage)
{
    switch (stage) {

    case EShaderStage::Vertex:
        return "vertex";
    case EShaderStage::Fragment:
        return "fragment";
        break;
    case EShaderStage::Undefined:
        break;
    }
    NE_CORE_ASSERT(false);
    return "";
}
} // namespace std


struct Shader
{
    std::string           m_Name{};
    std::filesystem::path m_FilePath;
};

struct GLSLShader : public Shader
{
    uint32_t m_ShaderID{0};
};



struct ShaderScriptProcessor
{
    friend class ShaderScriptProcessorFactory;

  protected:

    DirectoryStore shaderStorage;
    DirectoryStore cachedStorage;

    std::string shaderStoragePath = "Engine/Shader/";
    std::string cachedStoragePath = "Engine/Intermediate/Shader/";
    std::string cachedFileSuffix;

  public:
    using ir_t          = uint32_t;
    using spirv_ir_t    = std::vector<ir_t>;
    using stage2spirv_t = std::unordered_map<EShaderStage::T, spirv_ir_t>;

    virtual std::optional<stage2spirv_t> process(std::string_view fileName) = 0;
};


struct GLSLScriptProcessor : public ShaderScriptProcessor
{

    friend class ShaderScriptProcessorFactory;


  private:

    bool bOptimizeGLBinaries = false;
    bool bValid              = false;

  private:
    std::unordered_map<EShaderStage::T, std::vector<uint32_t>> m_Vulkan_SPIRV;
    // std::unordered_map<EShaderStage::T, std::vector<uint32_t>> m_OpenGL_SPIRV;
    std::unordered_map<EShaderStage::T, std::string> m_GLSL_SourceCode;


  public:

    std::optional<stage2spirv_t> process(std::string_view fileName) override;
    void                         reflect(EShaderStage::T stage, const std::vector<uint32_t> &spirvData);

  public:
    GLSLScriptProcessor() {}

  private:


    void CreateGLBinaries(bool bSourceChanged);
    void CreateVulkanBinaries(const std::unordered_map<EShaderStage::T, std::string> &shader_sources, bool bSourceChanged);

    std::filesystem::path GetCachePath(bool bVulkan, EShaderStage::T stage);
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
    std::string shaderStoragePath;
    bool        bSyncCreateStorage;

    Self &withProcessorType(EProcessorType type)
    {
        processorType = type;
        return *this;
    }

    Self &syncCreateStorage(bool bOn)
    {
        bSyncCreateStorage = bOn;
        return *this;
    }

    Self &withCachedStoragePath(std::string_view dirPath)
    {
        cachedStoragePath = dirPath;
        return *this;
    }

    Self &withShaderStoragePath(std::string_view dirPath)
    {
        shaderStoragePath = dirPath;
        return *this;
    }


    std::shared_ptr<ShaderScriptProcessor> FactoryNew()
    {
        std::shared_ptr<ShaderScriptProcessor> processor;

        switch (processorType) {
        case GLSL:
            processor = std::make_shared<GLSLScriptProcessor>();
            break;
        case HLSL:
            throw std::runtime_error("unsupported lang");
            break;
        };

        processor->shaderStoragePath = shaderStoragePath;
        processor->shaderStorage.create(shaderStoragePath.c_str(), bSyncCreateStorage);

        processor->cachedStoragePath = cachedStoragePath;
        processor->cachedStorage.create(cachedStoragePath.c_str(), bSyncCreateStorage);

        return processor;
    }
};

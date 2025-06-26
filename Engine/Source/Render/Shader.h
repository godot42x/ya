#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>


#include "../Core/Log.h"

#include "reflect.cc/enum"
#include <spirv_cross/spirv_cross.hpp>


namespace std
{


// Add formatter specialization for spirv_cross::SPIRType
// template <>
// struct formatter<spirv_cross::SPIRType> : formatter<std::string>
// {
//     auto format(const spirv_cross::SPIRType &type, std::format_context &ctx) const
//     {
//         return std::format("[ SPIRType: {}, vecsize: {}, columns: {}, array size: {} ]",
//                            static_cast<int>(type.basetype),
//                            type.vecsize,
//                            type.columns,
//                            type.array.size());
//     }
// };
} // namespace std


namespace EShaderStage
{
enum T
{
    Undefined = 0,
    Vertex,
    Fragment,
    ENUM_MAX,
};

GENERATED_ENUM_MISC(T);

inline T fromString(std::string_view type)
{
    if (type == "vertex")
    {
        return EShaderStage::Vertex;
    }
    else if (type == "fragment" || type == "pixel")
    {
        return EShaderStage::Fragment;
    }

    NE_CORE_ASSERT(false, "Unknown shader type! {}", type);
    return EShaderStage::Undefined;
}

const char *toString(EShaderStage::T Stage);


} // namespace EShaderStage

namespace ShaderReflection
{
enum class DataType
{
    Unknown,
    Bool,
    Int,
    UInt,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Mat3,
    Mat4,
    Sampler2D,
    SamplerCube,
    Sampler3D,
    ENUM_MAX,
};

GENERATED_ENUM_MISC(DataType);

struct StageIOData
{
    std::string           name;
    DataType              type;
    uint32_t              location;
    uint32_t              offset;
    uint32_t              size;
    spirv_cross::SPIRType format;
};

struct UniformBufferMember
{
    std::string name;
    DataType    type;
    uint32_t    offset;
    uint32_t    size;
};

struct UniformBuffer
{
    std::string                      name;
    uint32_t                         set;
    uint32_t                         binding;
    uint32_t                         size;
    std::vector<UniformBufferMember> members;
};

struct Resource
{
    std::string name;
    uint32_t    binding;
    uint32_t    set;
    DataType    type;
};

struct ShaderResources
{
    EShaderStage::T            stage;
    std::vector<StageIOData>   inputs;
    std::vector<StageIOData>   outputs;
    std::vector<UniformBuffer> uniformBuffers;
    std::vector<Resource>      sampledImages;

    // Original SPIRV-Cross resources
    spirv_cross::ShaderResources spirvResources;
};

// Utility functions for shader reflection
DataType SpirType2DataType(const spirv_cross::SPIRType &type);
uint32_t getDataTypeSize(DataType type);
} // namespace ShaderReflection

struct SPIRVHelper
{
    static uint32_t getVertexAlignedOffset(uint32_t current_offset, const spirv_cross::SPIRType &type);
    static uint32_t getSpirvTypeSize(const spirv_cross::SPIRType &type);
};



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

    std::string shaderStoragePath = "Shader/";
    std::string cachedStoragePath = "Intermediate/Shader/";
    std::string cachedFileSuffix;

  public:
    std::string tempProcessingPath;

  public:
    using ir_t          = uint32_t;
    using spirv_ir_t    = std::vector<ir_t>;
    using stage2spirv_t = std::unordered_map<EShaderStage::T, spirv_ir_t>;


  protected:
    // ShaderScriptProcessor() {}

  public:


    virtual std::optional<stage2spirv_t>                    process(std::string_view fileName)                                 = 0;
    [[nodiscard]] virtual ShaderReflection::ShaderResources reflect(EShaderStage::T stage, const std::vector<ir_t> &spirvData) = 0;
};


struct GLSLScriptProcessor : public ShaderScriptProcessor
{

    friend class ShaderScriptProcessorFactory;


  private:

    bool bOptimizeGLBinaries = false;
    bool bValid              = false;

  protected:
    // GLSLScriptProcessor() {}

  public:

    std::optional<stage2spirv_t>      process(std::string_view fileName) override;
    ShaderReflection::ShaderResources reflect(EShaderStage::T stage, const std::vector<ir_t> &spirvData) override;

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

    Self &withProcessorType(EProcessorType type)
    {
        processorType = type;
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


    template <typename T>
    std::shared_ptr<T> FactoryNew()
    {
        std::shared_ptr<T> processor;

        switch (processorType) {
        case GLSL:
            processor = std::make_shared<GLSLScriptProcessor>();
            break;
        case HLSL:
            throw std::runtime_error("HLSL not supported yet");
            break;
        };

        processor->shaderStoragePath = shaderStoragePath;
        processor->cachedStoragePath = cachedStoragePath;

        return processor;
    }
};

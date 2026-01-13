#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "Render.h"


#include "../Core/Log.h"

#include "Core/Debug/Instrumentor.h"
#include "reflect.cc/enum"
#include <spirv_cross/spirv_cross.hpp>
#include <utility>



namespace std
{


// Add formatter specialization for spirv_cross::SPIRType
template <>
struct formatter<spirv_cross::SPIRType> : formatter<std::string>
{
    auto format(const spirv_cross::SPIRType &type, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(),
                              "[ SPIRType: {}, vecsize: {}, columns: {}, array size: {} ]",
                              static_cast<int>(type.basetype),
                              type.vecsize,
                              type.columns,
                              type.array.size());
    }
};
} // namespace std


namespace ya
{

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

struct IShaderProcessor
{
    friend class ShaderProcessorFactory;
    using stdpath = std::filesystem::path;

  protected:

    stdpath     shaderStoragePath       = "Engine/Shader/";
    stdpath     intermediateStoragePath = "Intermediate/Shader/";
    std::string cachedFileSuffix;

  public:
    std::string           curFileName;
    std::filesystem::path curFilePath;

  public:
    using ir_t          = uint32_t;
    using spirv_ir_t    = std::vector<ir_t>;
    using stage2spirv_t = std::unordered_map<EShaderStage::T, spirv_ir_t>;


  protected:
    // ShaderScriptProcessor() {}
    virtual ~IShaderProcessor() = default;

  public:


    virtual std::optional<stage2spirv_t>                    process(const ShaderDesc &ci)                                      = 0;
    [[nodiscard]] virtual ShaderReflection::ShaderResources reflect(EShaderStage::T stage, const std::vector<ir_t> &spirvData) = 0;
};


struct GLSLProcessor : public IShaderProcessor
{

    friend class ShaderProcessorFactory;


  private:

    bool bOptimizeGLBinaries = false;
    bool bValid              = false;

  protected:
    // GLSLProcessor() {}

  public:

    std::optional<stage2spirv_t>      process(const ShaderDesc &ci) override;
    ShaderReflection::ShaderResources reflect(EShaderStage::T stage, const std::vector<ir_t> &spirvData) override;

    auto compileToSpv(std::string_view filename, std::string_view content, EShaderStage::T stage, const std::vector<std::string> &defines, std::vector<ir_t> &outSpv) -> bool;



  private:

    std::filesystem::path GetCachePath(bool bVulkan, EShaderStage::T stage);
    std::filesystem::path GetCacheMetaPath();


    bool                                             processCombinedSource(const stdpath &filepath, const std::vector<std::string> &defines, stage2spirv_t &outSpvMap);
    std::unordered_map<EShaderStage::T, std::string> preprocessCombinedSource(const stdpath &filepath);

    bool processSpvFiles(std::string_view vertFile, std::string_view fragFile, stage2spirv_t &outSpvMap);
};


struct ShaderStorage
{
    std::shared_ptr<IShaderProcessor>                                _processor;
    std::unordered_map<std::string, IShaderProcessor::stage2spirv_t> _shaderCache;

    ShaderStorage(std::shared_ptr<IShaderProcessor> processor)
        : _processor(std::move(processor)) {}


    [[nodiscard]] std::shared_ptr<IShaderProcessor> getProcessor() const { return _processor; }

    [[nodiscard]] const IShaderProcessor::stage2spirv_t *getCache(const std::string &key) const
    {
        auto it = _shaderCache.find(key);
        if (it == _shaderCache.end()) {
            YA_CORE_WARN("Shader not found in cache: {}", key);
            return nullptr;
        }
        return &it->second;
    }

    void removeCache(const std::string &key)
    {
        auto it = _shaderCache.find(key);
        if (it != _shaderCache.end()) {
            _shaderCache.erase(it);
            YA_CORE_INFO("Removed shader from cache: {}", key);
        }
    }

    const IShaderProcessor::stage2spirv_t *load(const ShaderDesc &ci)
    {
        // TODO: cache it
        // auto it = _shaderCache.find(ci.shaderName);
        // if (it != _shaderCache.end()) {
        //     YA_CORE_INFO("Shader already in cache: {}", ci.shaderName);
        //     if (!ci.bDirty) {
        //         return &it->second;
        //     }
        //     YA_CORE_INFO("Shader is dirty, reloading: {}", ci.shaderName);
        //     _shaderCache.erase(it);
        // }

        YA_PROFILE_SCOPE_LOG(std::format("ShaderStorage::load {}", ci.shaderName).c_str());
        auto opt = getProcessor()->process(ci);
        if (!opt.has_value()) {
            throw std::runtime_error(std::format("Failed to process shader: {}", ci.shaderName));
        }
        _shaderCache[ci.shaderName.data()] = std::move(*opt);
        return &_shaderCache.at(ci.shaderName);
    }
};


class ShaderProcessorFactory
{
  public:
    using Self = ShaderProcessorFactory;

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
            processor = std::make_shared<GLSLProcessor>();
            break;
        case HLSL:
            throw std::runtime_error("HLSL not supported yet");
            break;
        };

        processor->shaderStoragePath       = shaderStoragePath;
        processor->intermediateStoragePath = cachedStoragePath;

        return processor;
    }
};

} // namespace ya
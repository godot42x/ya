#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
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
    auto format(const spirv_cross::SPIRType& type, std::format_context& ctx) const
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
    std::string name;
    DataType     type;
    uint32_t     location;
    uint32_t     offset;
    uint32_t     size;
    uint32_t     vecsize  = 0;  // from SPIRType
    uint32_t     basetype = 0;  // from SPIRType
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

struct PushConstantBuffer
{
    std::string name;
    uint32_t    size = 0; // Declared struct size in bytes
};

struct Resource
{
    std::string name;
    uint32_t    binding   = 0;
    uint32_t    set       = 0;
    DataType    type      = DataType::Unknown;
    uint32_t    arraySize = 1; // 1 for non-array, >1 for array (e.g. samplerCube[MAX_POINT_LIGHTS])
};

struct ShaderResources
{
    EShaderStage::T            stage;
    std::vector<StageIOData>   inputs;
    std::vector<StageIOData>   outputs;
    std::vector<UniformBuffer> uniformBuffers;
    std::vector<Resource>      sampledImages;
    std::vector<PushConstantBuffer> pushConstantBuffers;

    // Original SPIRV-Cross resources
    spirv_cross::ShaderResources spirvResources;
};

/// Result of merging reflection data from multiple shader stages.
struct MergedResources
{
    /// Push constant ranges (stageFlags merged across stages).
    std::vector<PushConstantRange>                pushConstants;
    /// Descriptor set layouts built from all stages, ordered by set index.
    std::vector<DescriptorSetLayoutDesc>          descriptorSetLayouts;
    /// Vertex attributes extracted from the vertex stage.
    std::vector<StageIOData>                      vertexInputs;
};

/// Merge reflection data from multiple shader stages into a single
/// pipeline-layout description.  Each element in `stageResources` must
/// carry a distinct `stage` value.
MergedResources merge(const std::vector<ShaderResources>& stageResources);

// Utility functions for shader reflection
DataType SpirType2DataType(const spirv_cross::SPIRType& type);
uint32_t getDataTypeSize(DataType type);
} // namespace ShaderReflection

struct SPIRVHelper
{
    static uint32_t getVertexAlignedOffset(uint32_t current_offset, const spirv_cross::SPIRType& type);
    static uint32_t getSpirvTypeSize(const spirv_cross::SPIRType& type);
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

enum class EShaderProcessMode
{
    UseCache,
    ForceRecompile,
};

struct ShaderDiskCacheHeader
{
    static constexpr uint32_t MAGIC   = 0x59415343; // YASC
    static constexpr uint32_t VERSION = 1;

    uint32_t magic      = MAGIC;
    uint32_t version    = VERSION;
    uint64_t sourceHash = 0;
    uint32_t stageCount = 0;
};

struct ShaderDiskCacheStageHeader
{
    uint32_t stage     = 0;
    uint32_t wordCount = 0;
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

    virtual std::optional<stage2spirv_t>                    process(const ShaderDesc& ci, EShaderProcessMode mode = EShaderProcessMode::UseCache) = 0;
    [[nodiscard]] virtual ShaderReflection::ShaderResources reflect(EShaderStage::T stage, const std::vector<ir_t>& spirvData)                      = 0;
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

    std::optional<stage2spirv_t>      process(const ShaderDesc& ci, EShaderProcessMode mode = EShaderProcessMode::UseCache) override;
    ShaderReflection::ShaderResources reflect(EShaderStage::T stage, const std::vector<ir_t>& spirvData) override;

    auto compileToSpv(std::string_view filename, std::string_view content, EShaderStage::T stage, const std::vector<std::string>& defines, std::vector<ir_t>& outSpv) -> bool;



  private:

    std::filesystem::path GetCachePath(bool bVulkan) const;

    bool                                             processCombinedSource(const stdpath& filepath, const std::vector<std::string>& defines, stage2spirv_t& outSpvMap);
    std::unordered_map<EShaderStage::T, std::string> preprocessCombinedSource(const stdpath& filepath);

    bool processSpvFiles(std::string_view vertFile, std::string_view fragFile, stage2spirv_t& outSpvMap);
};


// ============================================================
// SlangProcessor: compiles .slang source files to SPIR-V via
// the Slang runtime API, and reflects shader resources using
// SPIRV-Cross (same as GLSLProcessor).
// ============================================================
struct SlangProcessor : public IShaderProcessor
{
    friend class ShaderProcessorFactory;

  public:
    std::optional<stage2spirv_t>      process(const ShaderDesc& ci, EShaderProcessMode mode = EShaderProcessMode::UseCache) override;
    ShaderReflection::ShaderResources reflect(EShaderStage::T stage, const std::vector<ir_t>& spirvData) override;

  private:
    // Compile a single .slang source file + entry point to SPIR-V.
    // `source`    – full source text (read from VFS)
    // `filePath`  – path used for error messages and #include resolution
    // `entryName` – entry point function name (e.g. "vertMain")
    // `stage`     – shader stage
    // `defines`   – preprocessor macro definitions
    // `outSpv`    – output SPIR-V words
    bool compileToSpv(std::string_view source,
                      std::string_view filePath,
                      std::string_view entryName,
                      EShaderStage::T  stage,
                      const std::vector<std::string>& defines,
                      std::vector<ir_t>&              outSpv);
};


struct ShaderStorage
{
    using stage2spirv_t = IShaderProcessor::stage2spirv_t;
    using cache_value_t = std::shared_ptr<stage2spirv_t>;

    std::shared_ptr<IShaderProcessor>               _processor;
    std::shared_ptr<IShaderProcessor>               _slangProcessor; // optional, for .slang files
    std::unordered_map<std::string, cache_value_t>  _shaderCache;
    mutable std::mutex                              _cacheMutex;

    ShaderStorage(std::shared_ptr<IShaderProcessor> processor)
        : _processor(std::move(processor)) {}

    /// Attach an optional Slang processor.  When set, any ShaderDesc whose
    /// shaderName ends with ".slang" (or whose stageFiles all end with ".slang")
    /// will be routed to this processor instead of the default one.
    void setSlangProcessor(std::shared_ptr<IShaderProcessor> slangProc)
    {
        _slangProcessor = std::move(slangProc);
    }

    [[nodiscard]] std::shared_ptr<IShaderProcessor> getProcessor() const { return _processor; }

    /// Select the appropriate processor for a given ShaderDesc.
    [[nodiscard]] std::shared_ptr<IShaderProcessor> selectProcessor(const ShaderDesc& ci) const
    {
        if (_slangProcessor)
        {
            if (ci.sourceMode == ShaderDesc::ESourceMode::SingleShader &&
                !ci.shaderName.empty())
            {
                auto name = ci.shaderName;
                if (!name.ends_with(".slang"))
                    name += ".slang";
                if (ci.shaderName.ends_with(".slang"))
                    return _slangProcessor;
            }
            if (ci.sourceMode == ShaderDesc::ESourceMode::StageFiles)
            {
                for (const auto& sf : ci.stageFiles)
                {
                    if (sf.file.ends_with(".slang"))
                        return _slangProcessor;
                }
            }
        }
        return _processor;
    }

    [[nodiscard]] std::shared_ptr<const stage2spirv_t> getCache(const std::string& key) const
    {
        std::lock_guard lock(_cacheMutex);
        auto it = _shaderCache.find(key);
        if (it == _shaderCache.end()) {
            YA_CORE_WARN("Shader not found in cache: {}", key);
            return {};
        }
        return it->second;
    }

    void removeCache(const std::string& key)
    {
        std::lock_guard lock(_cacheMutex);
        auto it = _shaderCache.find(key);
        if (it != _shaderCache.end()) {
            _shaderCache.erase(it);
            YA_CORE_INFO("Removed shader from cache: {}", key);
        }
    }

    [[nodiscard]] std::shared_ptr<const stage2spirv_t> load(const ShaderDesc& ci, EShaderProcessMode mode = EShaderProcessMode::UseCache)
    {
        const auto cacheKey = ci.cacheKey();
        YA_CORE_ASSERT(!cacheKey.empty(), "Shader cache key is empty");

        if (mode == EShaderProcessMode::UseCache) {
            std::lock_guard lock(_cacheMutex);
            auto it = _shaderCache.find(cacheKey);
            if (it != _shaderCache.end()) {
                return it->second;
            }
        }

        YA_PROFILE_SCOPE_LOG(std::format("ShaderStorage::load {}", cacheKey).c_str());
        auto opt = selectProcessor(ci)->process(ci, mode);
        if (!opt.has_value()) {
            throw std::runtime_error(std::format("Failed to process shader: {}", cacheKey));
        }

        auto compiled = std::make_shared<stage2spirv_t>(std::move(*opt));
        {
            std::lock_guard lock(_cacheMutex);
            _shaderCache[cacheKey] = compiled;
        }
        return compiled;
    }

    [[nodiscard]] std::shared_ptr<const stage2spirv_t> reload(const ShaderDesc& ci)
    {
        return load(ci, EShaderProcessMode::ForceRecompile);
    }

    void validate(const ShaderDesc& ci)
    {
        const auto cacheKey = ci.cacheKey();
        YA_CORE_ASSERT(!cacheKey.empty(), "Shader cache key is empty");

        YA_PROFILE_SCOPE_LOG(std::format("ShaderStorage::validate {}", cacheKey).c_str());
        auto opt = selectProcessor(ci)->process(ci, EShaderProcessMode::ForceRecompile);
        if (!opt.has_value()) {
            throw std::runtime_error(std::format("Failed to process shader: {}", cacheKey));
        }
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
        Slang,
    } processorType;

    std::string cachedStoragePath;
    std::string shaderStoragePath;

    Self& withProcessorType(EProcessorType type)
    {
        processorType = type;
        return *this;
    }


    Self& withCachedStoragePath(std::string_view dirPath)
    {
        cachedStoragePath = dirPath;
        return *this;
    }

    Self& withShaderStoragePath(std::string_view dirPath)
    {
        shaderStoragePath = dirPath;
        return *this;
    }


    template <typename T>
    std::shared_ptr<T> FactoryNew()
    {
        std::shared_ptr<IShaderProcessor> processor;

        switch (processorType) {
        case GLSL:
            processor = std::make_shared<GLSLProcessor>();
            break;
        case Slang:
            processor = std::make_shared<SlangProcessor>();
            break;
        case HLSL:
            throw std::runtime_error("HLSL not supported yet");
            break;
        };

        processor->shaderStoragePath       = shaderStoragePath;
        processor->intermediateStoragePath = cachedStoragePath;

        return std::dynamic_pointer_cast<T>(processor);
    }
};

} // namespace ya
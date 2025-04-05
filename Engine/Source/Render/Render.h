#pragma once

#include "Core/Log.h"
#include "reflect.cc/enum.h"

#include <memory>
#include <source_location>
#include <string_view>
#include <vector>

// Forward declarations
struct SDL_GPUTexture;
struct CommandBuffer;

struct RenderAPI
{
    enum EAPIType
    {
        None = 0,
        OpenGL,
        Vulkan,
        DirectX12,
        Metal,
        SDL3GPU, // SDL3 GPU backend
        ENUM_MAX,
    };
};

enum class ESamplerType
{
    DefaultLinear = 0,
    DefaultNearest,
    PointClamp,
    PointWrap,
    LinearClamp,
    LinearWrap,
    AnisotropicClamp,
    AnisotropicWrap,
    ENUM_MAX,
};
GENERATED_ENUM_MISC(ESamplerType);

enum class EGraphicPipeLinePrimitiveType
{
    TriangleList,
    ENUM_MAX = 0,
};

struct VertexBufferDescription
{
    uint32_t slot;
    uint32_t pitch;
};

namespace EVertexAttributeFormat
{
enum T
{
    Float2 = 0,
    Float3,
    Float4,
    ENUM_MAX,
};



std::size_t T2Size(T type);

GENERATED_ENUM_MISC(T);
}; // namespace EVertexAttributeFormat


namespace ETextureFormat
{
}

struct VertexAttribute
{
    uint32_t                  location;
    uint32_t                  bufferSlot;
    EVertexAttributeFormat::T format;
    uint32_t                  offset;
};

struct ShaderCreateInfo
{
    std::string shaderName; // we use single glsl now
    uint32_t    numUniformBuffers = 0;
    uint32_t    numSamplers       = 0;
};

struct GraphicsPipelineCreateInfo
{
    ShaderCreateInfo                     shaderCreateInfo;
    std::vector<VertexBufferDescription> vertexBufferDescs;
    std::vector<VertexAttribute>         vertexAttributes;
    EGraphicPipeLinePrimitiveType        primitiveType = EGraphicPipeLinePrimitiveType::TriangleList;
};

struct Render
{
    virtual bool                              createGraphicsPipeline(const GraphicsPipelineCreateInfo &info)                        = 0;
    virtual std::shared_ptr<CommandBuffer> acquireCommandBuffer(std::source_location location = std::source_location::current()) = 0;
};
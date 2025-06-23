#pragma once

#include <memory>
#include <source_location>
#include <vector>

#include "Core/Log.h"

#include "Render/Device.h"
#include "reflect.cc/enum"

#include "Render/CommandBuffer.h"


// Forward declarations
struct CommandBuffer;

namespace RenderAPI
{
enum T
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

enum class EGraphicPipeLinePrimitiveType
{
    TriangleList,
    Line,
    ENUM_MAX,
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


extern std::size_t T2Size(T type);

GENERATED_ENUM_MISC(T);
}; // namespace EVertexAttributeFormat


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
};

namespace EFrontFaceType
{
enum T
{
    ClockWise = 0,
    CounterClockWise,
};
};

struct GraphicsPipelineCreateInfo
{
    bool                                 bDeriveInfoFromShader = true;
    ShaderCreateInfo                     shaderCreateInfo;
    std::vector<VertexBufferDescription> vertexBufferDescs;
    std::vector<VertexAttribute>         vertexAttributes;
    EGraphicPipeLinePrimitiveType        primitiveType = EGraphicPipeLinePrimitiveType::TriangleList;
    EFrontFaceType::T                    frontFaceType = EFrontFaceType::CounterClockWise;
};


extern RenderAPI::T gRenderAPIType;
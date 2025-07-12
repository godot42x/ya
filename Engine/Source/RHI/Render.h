#pragma once

#include "Core/Base.h"
#include "Core/Log.h"
#include "WindowProvider.h"
#include "reflect.cc/enum"


// Forward declarations
struct CommandBuffer;
struct RenderPassCreateInfo;

namespace ERenderAPI
{
enum T
{
    None = 0,
    OpenGL,
    Vulkan,
    DirectX12,
    Metal,
    ENUM_MAX,
};
}; // namespace ERenderAPI


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
GENERATED_ENUM_MISC(T);
extern std::size_t T2Size(T type);
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

namespace EAttachmentLoadOp
{
enum T
{
    Load = 0,
    Clear,
    DontCare,
};
};

namespace EAttachmentStoreOp
{
enum T
{
    Store = 0,
    DontCare,
};
};

namespace EFormat
{
enum T
{
    Undefined = 0,
    R8G8B8A8_UNORM,
    B8G8R8A8_UNORM,
    D32_SFLOAT,
    D24_UNORM_S8_UINT,
    ENUM_MAX,
};
};

namespace ESampleCount
{
enum T
{
    Sample_1  = 1,
    Sample_2  = 2,
    Sample_4  = 4,
    Sample_8  = 8,
    Sample_16 = 16,
    Sample_32 = 32,
    Sample_64 = 64,
};
};

namespace EPresentMode
{
enum T
{
    Immediate = 0,
    Mailbox,
    FIFO,
    FIFO_Relaxed,
};
};

namespace EColorSpace
{
enum T
{
    SRGB_NONLINEAR = 0,
    HDR10_ST2084,
    HDR10_HLG,
};
};

namespace EImageUsage
{
enum T
{
    TransferSrc            = 0x01,
    TransferDst            = 0x02,
    Sampled                = 0x04,
    Storage                = 0x08,
    ColorAttachment        = 0x10,
    DepthStencilAttachment = 0x20,
    TransientAttachment    = 0x40,
    InputAttachment        = 0x80,
};
};

namespace ECompareOp
{
enum T
{
    Never = 0,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};
};

namespace ELogicOp
{
enum T
{
    Clear = 0,
    And,
    AndReverse,
    Copy,
    AndInverted,
    NoOp,
    Xor,
    Or,
    Nor,
    Equivalent,
    Invert,
    OrReverse,
    CopyInverted,
    OrInverted,
    Nand,
    Set,
};
};

namespace ESurfaceTransform
{
enum T
{
    Identity = 0,
    Rotate90,
    Rotate180,
    Rotate270,
    HorizontalMirror,
    HorizontalMirrorRotate90,
    HorizontalMirrorRotate180,
    HorizontalMirrorRotate270,
    Inherit,
};
};

namespace ECompositeAlpha
{
enum T
{
    Opaque = 0,
    PreMultiplied,
    PostMultiplied,
    Inherit,
};
};

namespace ESharingMode
{
enum T
{
    Exclusive = 0,
    Concurrent,
};
};

namespace EColorWriteMask
{
enum T
{
    None = 0x0,
    R    = 0x1,
    G    = 0x2,
    B    = 0x4,
    A    = 0x8,
    RGB  = R | G | B,
    RGBA = R | G | B | A,
    All  = RGBA,
};
};

namespace EBlendFactor
{
enum T
{
    Zero = 0,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};
};

namespace EBlendOp
{
enum T
{
    Add = 0,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};
};

namespace ECullMode
{
enum T
{
    None = 0,
    Front,
    Back,
    FrontAndBack,
};
};

namespace EPolygonMode
{
enum T
{
    Fill = 0,
    Line,
    Point,
};
};

struct AttachmentDescription
{
    EFormat::T            format                  = EFormat::R8G8B8A8_UNORM;
    ESampleCount::T       samples                 = ESampleCount::Sample_1;
    EAttachmentLoadOp::T  loadOp                  = EAttachmentLoadOp::Clear;
    EAttachmentStoreOp::T storeOp                 = EAttachmentStoreOp::Store;
    EAttachmentLoadOp::T  stencilLoadOp           = EAttachmentLoadOp::DontCare;
    EAttachmentStoreOp::T stencilStoreOp          = EAttachmentStoreOp::DontCare;
    bool                  bInitialLayoutUndefined = true;
    bool                  bFinalLayoutPresentSrc  = true; // for color attachments
};


struct RasterizationState
{
    bool              bDepthClampEnable        = false;
    bool              bRasterizerDiscardEnable = false;
    EPolygonMode::T   polygonMode              = EPolygonMode::Fill;
    ECullMode::T      cullMode                 = ECullMode::Back;
    EFrontFaceType::T frontFace                = EFrontFaceType::CounterClockWise;
    bool              bDepthBiasEnable         = false;
    float             depthBiasConstantFactor  = 0.0f;
    float             depthBiasClamp           = 0.0f;
    float             depthBiasSlopeFactor     = 0.0f;
    float             lineWidth                = 1.0f;
};

struct BlendAttachmentState
{
    bool               bBlendEnable        = false;
    EBlendFactor::T    srcColorBlendFactor = EBlendFactor::One;
    EBlendFactor::T    dstColorBlendFactor = EBlendFactor::Zero;
    EBlendOp::T        colorBlendOp        = EBlendOp::Add;
    EBlendFactor::T    srcAlphaBlendFactor = EBlendFactor::One;
    EBlendFactor::T    dstAlphaBlendFactor = EBlendFactor::Zero;
    EBlendOp::T        alphaBlendOp        = EBlendOp::Add;
    EColorWriteMask::T colorWriteMask      = EColorWriteMask::RGBA;
};

struct ColorBlendState
{
    bool                              bLogicOpEnable = false;
    ELogicOp::T                       logicOp        = ELogicOp::Copy;
    std::vector<BlendAttachmentState> attachments;
    float                             blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct DepthStencilState
{
    bool          bDepthTestEnable       = true;
    bool          bDepthWriteEnable      = true;
    ECompareOp::T depthCompareOp         = ECompareOp::Less;
    bool          bDepthBoundsTestEnable = false;
    bool          bStencilTestEnable     = false;
    float         minDepthBounds         = 0.0f;
    float         maxDepthBounds         = 1.0f;
    // Stencil ops can be added later if needed
};

struct MultisampleState
{
    ESampleCount::T rasterizationSamples   = ESampleCount::Sample_1;
    bool            bSampleShadingEnable   = false;
    float           minSampleShading       = 1.0f;
    bool            bAlphaToCoverageEnable = false;
    bool            bAlphaToOneEnable      = false;
};

struct Viewport
{
    float x        = 0.0f;
    float y        = 0.0f;
    float width    = 0.0f;
    float height   = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct Scissor
{
    int32_t  offsetX = 0;
    int32_t  offsetY = 0;
    uint32_t width   = 0;
    uint32_t height  = 0;
};

struct ViewportState
{
    std::vector<Viewport> viewports;
    std::vector<Scissor>  scissors;
    bool                  bDynamicViewport = true;
    bool                  bDynamicScissor  = true;
};

struct GraphicsPipelineCreateInfo
{
    enum class EPrimitiveType
    {
        TriangleList,
        Line,
        ENUM_MAX,
    };

    // Basic pipeline configuration
    bool                                 bDeriveInfoFromShader = true; // whether to use vertex layout by the shader's reflection
    ShaderCreateInfo                     shaderCreateInfo;
    std::vector<VertexBufferDescription> vertexBufferDescs;
    std::vector<VertexAttribute>         vertexAttributes;
    EPrimitiveType                       primitiveType = EPrimitiveType::TriangleList;

    // Advanced pipeline states
    RasterizationState rasterizationState;
    MultisampleState   multisampleState;
    DepthStencilState  depthStencilState;
    ColorBlendState    colorBlendState;
    ViewportState      viewportState;

    // Render pass compatibility
    uint32_t subpass = 0;
};



struct RenderPassCreateInfo
{

    struct SubpassDependency
    {
        bool     bSrcExternal = false; // If true, srcSubpass is VK_SUBPASS_EXTERNAL
        uint32_t srcSubpass   = 0;
        uint32_t dstSubpass   = 0;
        // Simplified for basic usage - can be expanded later
    };

    // Simplified subpass configuration - single subpass for now
    struct SubpassInfo
    {
        std::vector<uint32_t> colorAttachmentIndices;
        uint32_t              depthStencilAttachmentIndex = UINT32_MAX; // UINT32_MAX means no depth
        std::vector<uint32_t> inputAttachmentIndices;
        // std::vector<uint32_t> resolveAttachmentIndices;
    };


    std::vector<AttachmentDescription>      attachments;
    std::vector<SubpassDependency>          dependencies;
    std::vector<SubpassInfo>                subpasses;   // Multiple subpasses can be defined, but currently we use a single subpass
    std::vector<GraphicsPipelineCreateInfo> pipelineCIs; // For compatibility checks



    uint32_t getSubpassCount() const { return static_cast<uint32_t>(subpasses.size()); }

    bool isValidSubpassIndex(uint32_t index) const { return index < subpasses.size(); }

    // Get attachment count for a specific subpass
    uint32_t getColorAttachmentCount(uint32_t subpassIndex = 0) const
    {
        if (subpassIndex >= subpasses.size())
            return 0;
        return static_cast<uint32_t>(subpasses[subpassIndex].colorAttachmentIndices.size());
    }

    bool hasDepthAttachment(uint32_t subpassIndex = 0) const
    {
        if (subpassIndex >= subpasses.size())
            return false;
        return subpasses[subpassIndex].depthStencilAttachmentIndex != UINT32_MAX;
    }

    bool isCompatible(const GraphicsPipelineCreateInfo &pipelineCI, const RenderPassCreateInfo &renderPassCI)
    {
        // Check if subpass index is valid
        if (!renderPassCI.isValidSubpassIndex(pipelineCI.subpass)) {
            return false;
        }

        // Check if the number of color blend attachments matches the subpass color attachments
        uint32_t expectedColorAttachments = renderPassCI.getColorAttachmentCount(pipelineCI.subpass);
        if (pipelineCI.colorBlendState.attachments.size() != expectedColorAttachments) {
            return false;
        }

        // Check depth testing compatibility
        bool hasDepth = renderPassCI.hasDepthAttachment(pipelineCI.subpass);
        if (pipelineCI.depthStencilState.bDepthTestEnable && !hasDepth) {
            return false; // Pipeline expects depth but render pass doesn't have it
        }

        return true;
    }

    // Validate and adjust pipeline to match render pass requirements
    void validateAndAdjust()
    {
        UNIMPLEMENTED();
    }
};


struct SwapchainCreateInfo
{
    // Surface and format configuration
    EFormat::T      imageFormat = EFormat::B8G8R8A8_UNORM;
    EColorSpace::T  colorSpace  = EColorSpace::SRGB_NONLINEAR;
    EPresentMode::T presentMode = EPresentMode::FIFO; // V-Sync by default
    bool            bVsync      = true;               // V-Sync enabled by default

    // Image configuration
    uint32_t minImageCount    = 2; // Double buffering by default
    uint32_t imageArrayLayers = 1;
    // std::vector<EImageUsage::T> imageUsageFlags  = {EImageUsage::ColorAttachment}; // Default usage

    // Transform and composite
    // ESurfaceTransform::T preTransform   = ESurfaceTransform::Identity;
    // ECompositeAlpha::T   compositeAlpha = ECompositeAlpha::Opaque;

    // Clipping and sharing
    bool bClipped = true;
    // ESharingMode::T       imageSharingMode = ESharingMode::Exclusive;
    // std::vector<uint32_t> queueFamilyIndices; // For concurrent sharing mode

    uint32_t width  = 800;
    uint32_t height = 600;
};


struct IRender
{

    virtual ~IRender() { NE_CORE_TRACE("IRender::~IRender()"); }

    struct InitParams
    {
        bool                 bVsync    = true;
        ERenderAPI::T        renderAPI = ERenderAPI::Vulkan;
        WindowProvider      &windowProvider;
        SwapchainCreateInfo  swapchainCI;
        RenderPassCreateInfo renderPassCI;
    };

    virtual bool init(const InitParams &params) = 0;
    virtual void destroy()                      = 0;
};

// Utility functions for combining image usage flags
inline uint32_t combineImageUsageFlags(const std::vector<EImageUsage::T> &usages)
{
    uint32_t combined = 0;
    for (auto usage : usages) {
        combined |= static_cast<uint32_t>(usage);
    }
    return combined;
}

// Helper function to add image usage flags
inline void addImageUsage(std::vector<EImageUsage::T> &usage, EImageUsage::T flag)
{
    // Check if usage already exists
    for (auto existingUsage : usage) {
        if (existingUsage == flag) {
            return; // Already exists
        }
    }
    usage.push_back(flag);
}

// Pipeline-RenderPass compatibility and management functions
namespace PipelineRenderPassUtils
{

// Check if a pipeline is compatible with a render pass



} // namespace PipelineRenderPassUtils
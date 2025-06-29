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
// extern std::size_t T2Size(T type);
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
    None  = 0x0,
    R     = 0x1,
    G     = 0x2,
    B     = 0x4,
    A     = 0x8,
    RGB   = R | G | B,
    RGBA  = R | G | B | A,
    All   = RGBA,
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

struct SubpassDependency
{
    uint32_t srcSubpass = 0;
    uint32_t dstSubpass = 0;
    // Simplified for basic usage - can be expanded later
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
    bool            bBlendEnable        = false;
    EBlendFactor::T srcColorBlendFactor = EBlendFactor::One;
    EBlendFactor::T dstColorBlendFactor = EBlendFactor::Zero;
    EBlendOp::T     colorBlendOp        = EBlendOp::Add;
    EBlendFactor::T srcAlphaBlendFactor = EBlendFactor::One;
    EBlendFactor::T dstAlphaBlendFactor = EBlendFactor::Zero;
    EBlendOp::T     alphaBlendOp        = EBlendOp::Add;
    EColorWriteMask::T colorWriteMask   = EColorWriteMask::RGBA;
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

    // Pipeline layout (descriptor sets, push constants)
    // This can be expanded later for uniform buffers, textures, etc.

    // Validation methods will be defined after RenderPassCreateInfo

    // Static factory methods for common configurations
    static GraphicsPipelineCreateInfo createBasic3D(const ShaderCreateInfo &shader)
    {
        GraphicsPipelineCreateInfo info;
        info.shaderCreateInfo = shader;
        info.primitiveType    = EPrimitiveType::TriangleList;

        // 3D rendering defaults
        info.rasterizationState.cullMode    = ECullMode::Back;
        info.rasterizationState.frontFace   = EFrontFaceType::CounterClockWise;
        info.rasterizationState.polygonMode = EPolygonMode::Fill;

        info.depthStencilState.bDepthTestEnable  = true;
        info.depthStencilState.bDepthWriteEnable = true;

        // Single color attachment, no blending
        BlendAttachmentState colorAttachment;
        colorAttachment.bBlendEnable   = false;
        colorAttachment.colorWriteMask = EColorWriteMask::RGBA;
        info.colorBlendState.attachments.push_back(colorAttachment);

        return info;
    }

    static GraphicsPipelineCreateInfo createTransparent3D(const ShaderCreateInfo &shader)
    {
        GraphicsPipelineCreateInfo info = createBasic3D(shader);

        // Enable alpha blending
        info.colorBlendState.attachments[0].bBlendEnable        = true;
        info.colorBlendState.attachments[0].srcColorBlendFactor = EBlendFactor::SrcAlpha;
        info.colorBlendState.attachments[0].dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha;
        info.colorBlendState.attachments[0].srcAlphaBlendFactor = EBlendFactor::One;
        info.colorBlendState.attachments[0].dstAlphaBlendFactor = EBlendFactor::Zero;

        // Disable back face culling for transparency
        info.rasterizationState.cullMode = ECullMode::None;

        return info;
    }

    static GraphicsPipelineCreateInfo create2D(const ShaderCreateInfo &shader)
    {
        GraphicsPipelineCreateInfo info;
        info.shaderCreateInfo = shader;
        info.primitiveType    = EPrimitiveType::TriangleList;

        // 2D rendering defaults - no depth testing
        info.rasterizationState.cullMode         = ECullMode::None;
        info.depthStencilState.bDepthTestEnable  = false;
        info.depthStencilState.bDepthWriteEnable = false;

        // Alpha blending for 2D UI
        BlendAttachmentState colorAttachment;
        colorAttachment.bBlendEnable        = true;
        colorAttachment.srcColorBlendFactor = EBlendFactor::SrcAlpha;
        colorAttachment.dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha;
        colorAttachment.colorWriteMask      = EColorWriteMask::RGBA;
        info.colorBlendState.attachments.push_back(colorAttachment);

        return info;
    }
};



struct RenderPassCreateInfo
{
    // Simplified subpass configuration - single subpass for now
    struct SubpassInfo
    {
        std::vector<uint32_t> colorAttachmentIndices;
        uint32_t              depthStencilAttachmentIndex = UINT32_MAX; // UINT32_MAX means no depth
        std::vector<uint32_t> inputAttachmentIndices;
        std::vector<uint32_t> resolveAttachmentIndices;
    };

    std::vector<AttachmentDescription> attachments;
    std::vector<SubpassDependency>     dependencies;
    std::vector<SubpassInfo>           subpasses; // Multiple subpasses can be defined, but currently we use a single subpass

    // Helper methods for managing pipeline-subpass relationships
    void addPipelineToSubpass(const GraphicsPipelineCreateInfo& pipelineCI, uint32_t subpassIndex = 0)
    {
        if (subpassIndex >= subpasses.size()) {
            // Expand subpasses array if needed
            subpasses.resize(subpassIndex + 1);
        }
        
        // Set the pipeline's subpass index to match
        const_cast<GraphicsPipelineCreateInfo&>(pipelineCI).subpass = subpassIndex;
    }

    uint32_t getSubpassCount() const { return static_cast<uint32_t>(subpasses.size()); }
    
    bool isValidSubpassIndex(uint32_t index) const { return index < subpasses.size(); }

    // Get attachment count for a specific subpass
    uint32_t getColorAttachmentCount(uint32_t subpassIndex = 0) const
    {
        if (subpassIndex >= subpasses.size()) return 0;
        return static_cast<uint32_t>(subpasses[subpassIndex].colorAttachmentIndices.size());
    }

    bool hasDepthAttachment(uint32_t subpassIndex = 0) const
    {
        if (subpassIndex >= subpasses.size()) return false;
        return subpasses[subpassIndex].depthStencilAttachmentIndex != UINT32_MAX;
    }


    // Static factory methods for common render pass configurations
    static RenderPassCreateInfo createBasicColorDepth(
        EFormat::T      colorFormat = EFormat::B8G8R8A8_UNORM,
        EFormat::T      depthFormat = EFormat::D32_SFLOAT,
        ESampleCount::T samples     = ESampleCount::Sample_1)
    {
        RenderPassCreateInfo info;

        // Color attachment
        AttachmentDescription colorAttachment;
        colorAttachment.format                 = colorFormat;
        colorAttachment.samples                = samples;
        colorAttachment.loadOp                 = EAttachmentLoadOp::Clear;
        colorAttachment.storeOp                = EAttachmentStoreOp::Store;
        colorAttachment.bFinalLayoutPresentSrc = true;
        info.attachments.push_back(colorAttachment);

        // Depth attachment
        AttachmentDescription depthAttachment;
        depthAttachment.format                 = depthFormat;
        depthAttachment.samples                = samples;
        depthAttachment.loadOp                 = EAttachmentLoadOp::Clear;
        depthAttachment.storeOp                = EAttachmentStoreOp::DontCare;
        depthAttachment.bFinalLayoutPresentSrc = false;
        info.attachments.push_back(depthAttachment);

        // Configure subpass
        SubpassInfo subpass;
        subpass.colorAttachmentIndices.push_back(0);
        subpass.depthStencilAttachmentIndex = 1;
        info.subpasses.push_back(subpass);

        return info;
    }

    static RenderPassCreateInfo createColorOnly(
        EFormat::T      colorFormat = EFormat::B8G8R8A8_UNORM,
        ESampleCount::T samples     = ESampleCount::Sample_1)
    {
        RenderPassCreateInfo info;

        // Color attachment only
        AttachmentDescription colorAttachment;
        colorAttachment.format                 = colorFormat;
        colorAttachment.samples                = samples;
        colorAttachment.loadOp                 = EAttachmentLoadOp::Clear;
        colorAttachment.storeOp                = EAttachmentStoreOp::Store;
        colorAttachment.bFinalLayoutPresentSrc = true;
        info.attachments.push_back(colorAttachment);

        // Configure subpass
        SubpassInfo subpass;
        subpass.colorAttachmentIndices.push_back(0);
        // No depth attachment
        info.subpasses.push_back(subpass);

        return info;
    }

    static RenderPassCreateInfo createMultisample(
        EFormat::T      colorFormat = EFormat::B8G8R8A8_UNORM,
        EFormat::T      depthFormat = EFormat::D32_SFLOAT,
        ESampleCount::T samples     = ESampleCount::Sample_4)
    {
        RenderPassCreateInfo info;

        // MSAA Color attachment
        AttachmentDescription msaaColorAttachment;
        msaaColorAttachment.format                 = colorFormat;
        msaaColorAttachment.samples                = samples;
        msaaColorAttachment.loadOp                 = EAttachmentLoadOp::Clear;
        msaaColorAttachment.storeOp                = EAttachmentStoreOp::DontCare; // Don't store MSAA buffer
        msaaColorAttachment.bFinalLayoutPresentSrc = false;
        info.attachments.push_back(msaaColorAttachment);

        // Resolve attachment (single sample)
        AttachmentDescription resolveAttachment;
        resolveAttachment.format                 = colorFormat;
        resolveAttachment.samples                = ESampleCount::Sample_1;
        resolveAttachment.loadOp                 = EAttachmentLoadOp::DontCare;
        resolveAttachment.storeOp                = EAttachmentStoreOp::Store;
        resolveAttachment.bFinalLayoutPresentSrc = true;
        info.attachments.push_back(resolveAttachment);

        // MSAA Depth attachment
        AttachmentDescription msaaDepthAttachment;
        msaaDepthAttachment.format                 = depthFormat;
        msaaDepthAttachment.samples                = samples;
        msaaDepthAttachment.loadOp                 = EAttachmentLoadOp::Clear;
        msaaDepthAttachment.storeOp                = EAttachmentStoreOp::DontCare;
        msaaDepthAttachment.bFinalLayoutPresentSrc = false;
        info.attachments.push_back(msaaDepthAttachment);

        // Configure subpass
        SubpassInfo subpass;
        subpass.colorAttachmentIndices.push_back(0);   // MSAA color
        subpass.resolveAttachmentIndices.push_back(1); // Resolve target
        subpass.depthStencilAttachmentIndex = 2;       // MSAA depth
        info.subpasses.push_back(subpass);

        return info;
    }
};


struct SwapchainCreateInfo
{
    // Surface and format configuration
    EFormat::T      imageFormat = EFormat::B8G8R8A8_UNORM;
    EColorSpace::T  colorSpace  = EColorSpace::SRGB_NONLINEAR;
    EPresentMode::T presentMode = EPresentMode::FIFO; // V-Sync by default

    // Image configuration
    uint32_t                    minImageCount    = 2; // Double buffering by default
    uint32_t                    imageArrayLayers = 1;
    std::vector<EImageUsage::T> imageUsageFlags  = {EImageUsage::ColorAttachment}; // Default usage

    // Transform and composite
    ESurfaceTransform::T preTransform   = ESurfaceTransform::Identity;
    ECompositeAlpha::T   compositeAlpha = ECompositeAlpha::Opaque;

    // Clipping and sharing
    bool                  bClipped         = true;
    ESharingMode::T       imageSharingMode = ESharingMode::Exclusive;
    std::vector<uint32_t> queueFamilyIndices; // For concurrent sharing mode

    // Window integration
    void    *windowHandle = nullptr; // Platform-specific window handle
    uint32_t width        = 800;
    uint32_t height       = 600;

    // Static factory methods for common swapchain configurations
    static SwapchainCreateInfo createDefault(uint32_t w, uint32_t h, bool bVsync = true)
    {
        SwapchainCreateInfo info;
        info.width           = w;
        info.height          = h;
        info.presentMode     = bVsync ? EPresentMode::FIFO : EPresentMode::Immediate;
        info.minImageCount   = bVsync ? 2 : 3; // Triple buffering for immediate mode
        info.imageFormat     = EFormat::B8G8R8A8_UNORM;
        info.colorSpace      = EColorSpace::SRGB_NONLINEAR;
        info.imageUsageFlags = {EImageUsage::ColorAttachment};
        return info;
    }

    static SwapchainCreateInfo createHighPerformance(uint32_t w, uint32_t h)
    {
        SwapchainCreateInfo info;
        info.width           = w;
        info.height          = h;
        info.presentMode     = EPresentMode::Mailbox; // Best performance with tear-free
        info.minImageCount   = 3;                     // Triple buffering
        info.imageFormat     = EFormat::B8G8R8A8_UNORM;
        info.colorSpace      = EColorSpace::SRGB_NONLINEAR;
        info.imageUsageFlags = {EImageUsage::ColorAttachment};
        return info;
    }

    static SwapchainCreateInfo createHDR(uint32_t w, uint32_t h, bool bVsync = true)
    {
        SwapchainCreateInfo info;
        info.width           = w;
        info.height          = h;
        info.presentMode     = bVsync ? EPresentMode::FIFO : EPresentMode::Mailbox;
        info.minImageCount   = bVsync ? 2 : 3;
        info.imageFormat     = EFormat::R8G8B8A8_UNORM; // Better for HDR workflows
        info.colorSpace      = EColorSpace::HDR10_ST2084;
        info.imageUsageFlags = {EImageUsage::ColorAttachment};
        return info;
    }

    static SwapchainCreateInfo createGameOptimized(uint32_t w, uint32_t h)
    {
        SwapchainCreateInfo info;
        info.width           = w;
        info.height          = h;
        info.presentMode     = EPresentMode::Mailbox; // Low latency
        info.minImageCount   = 3;
        info.imageFormat     = EFormat::B8G8R8A8_UNORM;
        info.colorSpace      = EColorSpace::SRGB_NONLINEAR;
        info.imageUsageFlags = {EImageUsage::ColorAttachment, EImageUsage::TransferDst}; // For screenshots, etc.
        return info;
    }
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
inline void addImageUsage(SwapchainCreateInfo &info, EImageUsage::T usage)
{
    // Check if usage already exists
    for (auto existingUsage : info.imageUsageFlags) {
        if (existingUsage == usage) {
            return; // Already exists
        }
    }
    info.imageUsageFlags.push_back(usage);
}

// Pipeline-RenderPass compatibility and management functions
namespace PipelineRenderPassUtils
{
    // Check if a pipeline is compatible with a render pass
    inline bool isCompatible(const GraphicsPipelineCreateInfo& pipelineCI, const RenderPassCreateInfo& renderPassCI)
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
    inline void validateAndAdjust(GraphicsPipelineCreateInfo& pipelineCI, const RenderPassCreateInfo& renderPassCI)
    {
        // Ensure subpass index is valid
        if (!renderPassCI.isValidSubpassIndex(pipelineCI.subpass)) {
            pipelineCI.subpass = 0; // Default to first subpass
        }

        // Adjust color blend attachments count to match render pass
        uint32_t expectedColorAttachments = renderPassCI.getColorAttachmentCount(pipelineCI.subpass);
        if (pipelineCI.colorBlendState.attachments.size() != expectedColorAttachments) {
            pipelineCI.colorBlendState.attachments.resize(expectedColorAttachments);
            
            // Initialize new attachments with default values
            for (size_t i = 0; i < pipelineCI.colorBlendState.attachments.size(); ++i) {
                if (pipelineCI.colorBlendState.attachments[i].colorWriteMask == static_cast<EColorWriteMask::T>(0)) {
                    pipelineCI.colorBlendState.attachments[i].colorWriteMask = EColorWriteMask::RGBA;
                }
            }
        }

        // Disable depth testing if render pass doesn't have depth attachment
        if (!renderPassCI.hasDepthAttachment(pipelineCI.subpass)) {
            pipelineCI.depthStencilState.bDepthTestEnable = false;
            pipelineCI.depthStencilState.bDepthWriteEnable = false;
        }
    }

    // Create a pipeline optimized for a specific subpass
    inline GraphicsPipelineCreateInfo createForSubpass(
        const ShaderCreateInfo& shader,
        const RenderPassCreateInfo& renderPassCI, 
        uint32_t subpassIndex = 0,
        bool bEnable3D = true)
    {
        GraphicsPipelineCreateInfo pipeline;
        
        if (bEnable3D) {
            pipeline = GraphicsPipelineCreateInfo::createBasic3D(shader);
        } else {
            pipeline = GraphicsPipelineCreateInfo::create2D(shader);
        }
        
        pipeline.subpass = subpassIndex;
        validateAndAdjust(pipeline, renderPassCI);
        
        return pipeline;
    }

    // Helper to create multiple pipelines for different subpasses
    inline std::vector<GraphicsPipelineCreateInfo> createForAllSubpasses(
        const ShaderCreateInfo& shader,
        const RenderPassCreateInfo& renderPassCI,
        bool bEnable3D = true)
    {
        std::vector<GraphicsPipelineCreateInfo> pipelines;
        
        for (uint32_t i = 0; i < renderPassCI.getSubpassCount(); ++i) {
            pipelines.push_back(createForSubpass(shader, renderPassCI, i, bEnable3D));
        }
        
        return pipelines;
    }
}
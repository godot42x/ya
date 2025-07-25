#pragma once

#include "Core/Base.h"
#include "Core/Log.h"
#include "WindowProvider.h"
#include "reflect.cc/enum"


// Forward declarations
struct CommandBuffer;
struct RenderPassCreateInfo;

// enum bit flags support
template <typename T>
concept enum_t = std::is_enum_v<T>;
template <enum_t Enum>
constexpr Enum operator|(Enum lhs, Enum rhs) { return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) | static_cast<std::underlying_type_t<Enum>>(rhs)); }
template <enum_t Enum>
constexpr Enum operator&(Enum lhs, Enum rhs) { return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) & static_cast<std::underlying_type_t<Enum>>(rhs)); }
template <enum_t Enum>
constexpr Enum operator^(Enum lhs, Enum rhs) { return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) ^ static_cast<std::underlying_type_t<Enum>>(rhs)); }
template <enum_t Enum>
constexpr Enum operator~(Enum lhs) { return static_cast<Enum>(~static_cast<std::underlying_type_t<Enum>>(lhs)); }
template <enum_t Enum>
constexpr Enum operator|=(Enum &lhs, Enum rhs) { return lhs = lhs | rhs; }

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


namespace EShaderStage
{
enum T
{
    Vertex   = 0x01,
    Fragment = 0x02,
    Geometry = 0x04,
    Compute  = 0x08,

};

inline T fromString(std::string_view str)
{
    if (str == "vertex")
        return Vertex;
    if (str == "fragment")
        return Fragment;
    if (str == "geometry")
        return Geometry;
    if (str == "compute")
        return Compute;

    UNREACHABLE();
    return {};
}

GENERATED_ENUM_MISC_WITH_RANGE(T, Compute);



}; // namespace EShaderStage



struct ShaderCreateInfo
{
    std::string                          shaderName; // we use single glsl now
    std::vector<VertexBufferDescription> vertexBufferDescs;
    std::vector<VertexAttribute>         vertexAttributes;
    bool                                 bDeriveFromShader = true; // whether to use vertex layout by the shader's reflection
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

namespace EImageLayout
{
enum T
{
    Undefined = 0,
    ColorAttachmentOptimal,
    DepthStencilAttachmentOptimal,
    ShaderReadOnlyOptimal,
    TransferSrcOptimal,
    TransferDstOptimal,
    PresentSrcKHR,
};
} // namespace EImageLayout

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
    InputAttachment        = 0x80, // 128
};
}; // namespace EImageUsage

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

namespace EColorComponent
{
enum T
{
    None = 0x0,
    R    = 0x1,
    G    = 0x2,
    B    = 0x4,
    A    = 0x8,
};


} // namespace EColorComponent

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
    EFormat::T            format                  = EFormat::Undefined;
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

struct ColorBlendAttachmentState
{
    int                index               = -1; // should be ref to the renderpass's color attachment index?
    bool               bBlendEnable        = false;
    EBlendFactor::T    srcColorBlendFactor = EBlendFactor::One;
    EBlendFactor::T    dstColorBlendFactor = EBlendFactor::Zero;
    EBlendOp::T        colorBlendOp        = EBlendOp::Add;
    EBlendFactor::T    srcAlphaBlendFactor = EBlendFactor::One;
    EBlendFactor::T    dstAlphaBlendFactor = EBlendFactor::Zero;
    EBlendOp::T        alphaBlendOp        = EBlendOp::Add;
    EColorComponent::T colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A;
};

struct ColorBlendState
{
    bool                                   bLogicOpEnable = false;
    ELogicOp::T                            logicOp        = ELogicOp::Copy;
    std::vector<ColorBlendAttachmentState> attachments;
    float                                  blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
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
    ESampleCount::T sampleCount            = ESampleCount::Sample_1;
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

namespace EPrimitiveType
{
enum T
{
    TriangleList,
    Line,
    ENUM_MAX,
};
}

namespace EPipelineDescriptorType
{
enum T
{
    UniformBuffer = 0,
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    StorageBuffer,
    ENUM_MAX,
};
};

namespace EPipelineDynamicFeature
{
enum T
{
    DepthTest  = 0x01,
    AlphaBlend = 0x02,
};
}

struct GraphicsPipelineLayoutCreateInfo
{
    struct PushConstant
    {
        uint32_t        offset     = 0;
        uint32_t        size       = 0;
        EShaderStage::T stageFlags = EShaderStage::Vertex | EShaderStage::Fragment; // Default to vertex and fragment stages
    };



    struct DescriptorBinding
    {
        uint32_t                   binding         = 0;
        EPipelineDescriptorType::T descriptorType  = EPipelineDescriptorType::UniformBuffer;
        uint32_t                   descriptorCount = 1;
        EShaderStage::T            stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment;
    };

    struct DescriptorSetLayout
    {
        std::vector<DescriptorBinding> bindings;
    };



    std::vector<PushConstant>        pushConstants;
    std::vector<DescriptorSetLayout> descriptorSetLayouts;
};


struct GraphicsPipelineCreateInfo
{



    // different shader/pipeline can use same pipeline layout
    // GraphicsPipelineLayoutCreateInfo *pipelineLayout = nullptr;

    ShaderCreateInfo shaderCreateInfo;

    EPipelineDynamicFeature::T dynamicFeatures = {};
    uint32_t                   subPassRef      = 0;
    EPrimitiveType::T          primitiveType   = EPrimitiveType::TriangleList;
    RasterizationState         rasterizationState;
    MultisampleState           multisampleState;
    DepthStencilState          depthStencilState;
    ColorBlendState            colorBlendState;
    ViewportState              viewportState;
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

    struct AttachmentRef
    {
        int32_t         ref    = -1;
        EImageLayout::T layout = EImageLayout::Undefined;
    };

    // Simplified subpass configuration - single subpass for now
    struct SubpassInfo
    {
        uint32_t                    subpassIndex = 0;
        std ::vector<AttachmentRef> inputAttachments;  // Single color attachment for now
        std::vector<AttachmentRef>  colorAttachments;  // Single color attachment for now
        AttachmentRef               depthAttachment;   // Single depth attachment for now
        AttachmentRef               resolveAttachment; // Single depth attachment for now
    };


    std::vector<AttachmentDescription> attachments; // all attachment
    std::vector<SubpassInfo>           subpasses;   // Multiple subpasses can be defined, but currently we use a single subpass
    std::vector<SubpassDependency>     dependencies;

    std::vector<GraphicsPipelineCreateInfo> pipelineCIs; // For compatibility checks



    uint32_t getSubpassCount() const { return static_cast<uint32_t>(subpasses.size()); }

    bool isValidSubpassIndex(uint32_t index) const { return index < subpasses.size(); }
};


struct SwapchainCreateInfo
{
    // Surface and format configuration
    EFormat::T      imageFormat = EFormat::R8G8B8A8_UNORM; // TODO: rename to surfaceFormat
    EColorSpace::T  colorSpace  = EColorSpace::SRGB_NONLINEAR;
    EPresentMode::T presentMode = EPresentMode::FIFO; // V-Sync by default
    bool            bVsync      = true;               // V-Sync enabled by default

    // Image configuration
    uint32_t minImageCount       = 3; // Double buffering by default
    uint32_t preferredImageCount = 3; // Triple buffering if supported
    uint32_t imageArrayLayers    = 1;
    // std::vector<EImageUsage::T> imageUsageFlags  = {EImageUsage::ColorAttachment}; // Default usage

    // Transform and composite
    // ESurfaceTransform::T preTransform   = ESurfaceTransform::Identity;
    // ECompositeAlpha::T   compositeAlpha = ECompositeAlpha::Opaque;

    // Clipping and sharing
    bool bClipped                = true;
    bool preferredDifferentQueue = true; // Use different queues for graphics and present if possible

    uint32_t width  = 800;
    uint32_t height = 600;
};


struct DeviceFeature
{
    std::string name;
    bool        bRequired;

    bool operator==(const DeviceFeature &other) const
    {
        return name == other.name;
    }

    bool operator<(const DeviceFeature &other) const
    {
        return name < other.name;
    }
};

struct IRender
{

    virtual ~IRender() { NE_CORE_TRACE("IRender::~IRender()"); }

    struct InitParams
    {
        bool                bVsync    = true;
        ERenderAPI::T       renderAPI = ERenderAPI::Vulkan;
        WindowProvider     *windowProvider;
        SwapchainCreateInfo swapchainCI;
    };

    virtual bool init(const InitParams &params) = 0;
    virtual void destroy()                      = 0;
};

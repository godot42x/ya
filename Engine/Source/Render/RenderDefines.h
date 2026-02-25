#pragma once

#include "Core/Base.h"

#include "Core/Handle.h"
#include "reflect.cc/enum"

#include <glm/glm.hpp>



namespace ya
{

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
constexpr Enum operator|=(Enum& lhs, Enum rhs) { return lhs = lhs | rhs; }

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

namespace ERenderingMode
{
enum T
{
    None,
    RenderPass,       // Traditional RenderPass + Subpass
    DynamicRendering, // Vulkan 1.3+ / VK_KHR_dynamic_rendering
    Auto,             // Auto select based on driver support
};
}; // namespace ERenderingMode

namespace EResolveMode
{
enum T
{
    None    = 0,
    Average = 1, // VK_RESOLVE_MODE_AVERAGE_BIT
    Min     = 2, // VK_RESOLVE_MODE_MIN_BIT
    Max     = 4, // VK_RESOLVE_MODE_MAX_BIT
};
}; // namespace EResolveMode



struct ClearColorValue
{
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};
};

struct ClearDepthStencilValue
{
    float    depth{1.0f};
    uint32_t stencil{0};
};

struct ClearValue
{
    bool isDepthStencil{false};
    union
    {
        ClearColorValue        color;
        ClearDepthStencilValue depthStencil;
    };

    ClearValue() : color{} {}
    explicit ClearValue(float r, float g, float b, float a) : isDepthStencil(false), color{r, g, b, a} {}
    explicit ClearValue(float depth, uint32_t stencil = 0) : isDepthStencil(true), depthStencil{depth, stencil} {}

    static ClearValue Black() { return ClearValue(0.0f, 0.0f, 0.0f, 1.0f); }
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
    Uint = 0,
    Float,
    Float2,
    Float3,
    Float4,
    ENUM_MAX,
};
GENERATED_ENUM_MISC(T);
extern std::size_t T2Size(T type);
}; // namespace EVertexAttributeFormat

struct VertexAttribute
{
    uint32_t                  bufferSlot;
    uint32_t                  location;
    EVertexAttributeFormat::T format;
    uint32_t                  offset;
};


namespace EShaderStage
{
enum T
{
    Vertex   = 0x01,
    Geometry = 0x02,
    Fragment = 0x04,
    Compute  = 0x08,
    Count,
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



struct ShaderDesc
{
    std::string                          shaderName;                // we use single glsl now
    bool                                 bDeriveFromShader = false; // whether to use vertex layout by the shader's reflection
    std::vector<VertexBufferDescription> vertexBufferDescs{};
    std::vector<VertexAttribute>         vertexAttributes{};
    std::vector<std::string>             defines = {}; // #define in shader

    // TOD:
    // 1. split the giant header file into each unit(pipe line relates to pipelines),
    //    we are not a fixed API like vulkan api. It's hard to modify and recompile it
    // 2. Make each macro by values m x n (by deps) to do a validation(is it valid to do?)
    // struct Gate
    // {
    //     std::string              define;
    //     std::vector<std::string> values;
    //     std::vector<int>       dependGates;
    // };
    // std::vector<Gate> gates;
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
    R8_UNORM,
    R8G8_UNORM,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_UNORM,
    B8G8R8A8_SRGB,
    D32_SFLOAT,
    D32_SFLOAT_S8_UINT,
    D24_UNORM_S8_UINT,

    // Block Compression (BC) formats
    BC1_RGB_UNORM_BLOCK,
    BC1_RGBA_UNORM_BLOCK,
    BC1_RGB_SRGB_BLOCK,
    BC1_RGBA_SRGB_BLOCK,
    BC3_UNORM_BLOCK,
    BC3_SRGB_BLOCK,
    BC4_UNORM_BLOCK,
    BC4_SNORM_BLOCK,
    BC5_UNORM_BLOCK,
    BC5_SNORM_BLOCK,
    BC7_UNORM_BLOCK,
    BC7_SRGB_BLOCK,

    // ASTC formats
    ASTC_4x4_UNORM_BLOCK,
    ASTC_4x4_SRGB_BLOCK,
    ASTC_5x5_UNORM_BLOCK,
    ASTC_5x5_SRGB_BLOCK,
    ASTC_6x6_UNORM_BLOCK,
    ASTC_6x6_SRGB_BLOCK,
    ASTC_8x8_UNORM_BLOCK,
    ASTC_8x8_SRGB_BLOCK,
    ASTC_10x10_UNORM_BLOCK,
    ASTC_10x10_SRGB_BLOCK,

    // ETC2 formats
    ETC2_R8G8B8_UNORM_BLOCK,
    ETC2_R8G8B8_SRGB_BLOCK,
    ETC2_R8G8B8A1_UNORM_BLOCK,
    ETC2_R8G8B8A1_SRGB_BLOCK,
    ETC2_R8G8B8A8_UNORM_BLOCK,
    ETC2_R8G8B8A8_SRGB_BLOCK,

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
    TransferSrc /*Optimal*/,
    TransferDst /*Optimal*/,
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
    None                   = 0,
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
    uint32_t              index          = 0;
    EFormat::T            format         = EFormat::Undefined;
    ESampleCount::T       samples        = ESampleCount::Sample_1;
    EAttachmentLoadOp::T  loadOp         = EAttachmentLoadOp::Clear;
    EAttachmentStoreOp::T storeOp        = EAttachmentStoreOp::Store;
    EAttachmentLoadOp::T  stencilLoadOp  = EAttachmentLoadOp::DontCare;
    EAttachmentStoreOp::T stencilStoreOp = EAttachmentStoreOp::DontCare;
    EImageLayout::T       initialLayout  = EImageLayout::Undefined;
    EImageLayout::T       finalLayout    = EImageLayout::ColorAttachmentOptimal;

    // declare here for RT/framebuffer
    EImageUsage::T usage;
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
    EColorComponent::T colorWriteMask      = (EColorComponent::T)(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A);

    static ColorBlendAttachmentState defaultEnable(int index)
    {
        return ColorBlendAttachmentState{
            .index               = index,
            .bBlendEnable        = true,
            .srcColorBlendFactor = EBlendFactor::SrcAlpha,
            .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
            .colorBlendOp        = EBlendOp::Add,
            .srcAlphaBlendFactor = EBlendFactor::One,
            .dstAlphaBlendFactor = EBlendFactor::Zero,
            .alphaBlendOp        = EBlendOp::Add,
            .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
        };
    }
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
    bool          bDepthTestEnable       = false;
    bool          bDepthWriteEnable      = false;
    ECompareOp::T depthCompareOp         = ECompareOp::Less; // a 0.5, b 1.0, a < b pass write a to depth buffer
    bool          bDepthBoundsTestEnable = false;
    bool          bStencilTestEnable     = false;
    float         minDepthBounds         = 0.0f;
    float         maxDepthBounds         = 1.0f;

    // TODO
    // VkStencilOpState                          front;
    // VkStencilOpState                          back;
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

// 把NDC（-1，1） 转化为屏幕坐标
struct Viewport
{
    float           x        = 0.0f;
    float           y        = 0.0f;
    float           width    = 800.0f;
    float           height   = 600.0f;
    float           minDepth = 0.0f;
    float           maxDepth = 5.0f;
    static Viewport defaults() { return Viewport{}; }
};

// limit render in specific area, clip
struct Scissor
{
    int32_t  offsetX = 0;
    int32_t  offsetY = 0;
    uint32_t width   = 800;
    uint32_t height  = 600;

    static Scissor defaults() { return Scissor{}; }
};

struct Rect2D
{
    union
    {
        glm::vec2 pos;
        glm::vec2 offset;
    };
    glm::vec2 extent;

    Extent2D extent2D() { return Extent2D::fromVec2(extent); }
};

struct ViewportState
{
    std::vector<Viewport> viewports;
    std::vector<Scissor>  scissors;
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
    StorageBuffer,

    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,

    ENUM_MAX,
};
};

namespace EPipelineDynamicFeature
{
enum T
{
    DepthTest = 0,
    BlendConstants,
    Viewport,
    Scissor,
    CullMode,
    PolygonMode,
    Count
};
}

struct DescriptorSetLayoutBinding
{
    uint32_t                   binding         = 0;
    EPipelineDescriptorType::T descriptorType  = EPipelineDescriptorType::UniformBuffer;
    uint32_t                   descriptorCount = 1;
    EShaderStage::T            stageFlags      = EShaderStage::Vertex | EShaderStage::Fragment;
};

struct DescriptorSetLayoutDesc
{
    std::string                             label = "None";
    int32_t                                 set   = -1; // indicate pos only, no ref usage
    std::vector<DescriptorSetLayoutBinding> bindings;
};


struct PushConstantRange
{
    uint32_t        offset     = 0;
    uint32_t        size       = 0;
    EShaderStage::T stageFlags = static_cast<EShaderStage::T>(EShaderStage::Vertex | EShaderStage::Fragment); // Default to vertex and fragment stages
};

struct PipelineLayoutDesc
{
    std::string                          label = "None";
    std::vector<PushConstantRange>       pushConstants;
    std::vector<DescriptorSetLayoutDesc> descriptorSetLayouts;
};

struct DescriptorPoolSize
{
    EPipelineDescriptorType::T type;
    uint32_t                   descriptorCount;
};

struct DescriptorPoolCreateInfo
{
    std::string                     label   = "None";
    uint32_t                        maxSets = 0;
    std::vector<DescriptorPoolSize> poolSizes;
};


struct IImageView;
struct IRenderPass;
struct Texture; // Forward declaration for RenderingInfo::ImageSpec

struct IRenderTarget; // Forward declaration

/**
 * @brief Pure data structure for rendering info (supports both RenderPass and Dynamic Rendering modes)
 * Use RenderingInfoBuilder to construct instances
 */
struct RenderingInfo
{
    std::string label = "None";

    Rect2D   renderArea;     // Render area (offset + extent)
    uint32_t layerCount = 1; // For layered rendering

    std::vector<ClearValue> colorClearValues;
    ClearValue              depthClearValue = {};
    // ClearValue                stencilClearValue;

    struct ImageSpec
    {
        Texture* texture = nullptr; // ← 使用高层 Texture 抽象，替代 IImageView*
        // EResolveMode::T resolveMode      = EResolveMode::None;
        // IImageView     *resolveImageView = nullptr;

        ESampleCount::T sampleCount;
        // uint32_t        viewMask       = 0;
        // bool            bResolveTarget = false;

        EAttachmentLoadOp::T  loadOp  = EAttachmentLoadOp::Load;   // Load operation
        EAttachmentStoreOp::T storeOp = EAttachmentStoreOp::Store; // Store operation
    };

    // use render target spec
    IRenderTarget* renderTarget = nullptr;

    // or manual combined image spec
    std::vector<ImageSpec> colorAttachments = {};
    ImageSpec*             depthAttachment  = nullptr;
};
struct EndRenderingInfo
{
    IRenderTarget* renderTarget = nullptr;
};


struct PipelineRenderingInfo
{
    std::string label    = "None";
    uint32_t    viewMask = 0; // View mask for multiview rendering

    // Color attachments (can have multiple for MRT)
    std::vector<EFormat::T> colorAttachmentFormats;

    // Depth attachment (optional)
    EFormat::T depthAttachmentFormat = EFormat::Undefined;

    // Stencil attachment (optional, can be same as depth)
    EFormat::T stencilAttachmentFormat = EFormat::Undefined;
};

struct IPipelineLayout;

struct GraphicsPipelineCreateInfo
{
    // Subpass mode fields
    int32_t      subPassRef = -1;
    IRenderPass* renderPass;

    // Dynamic Rendering mode fields (ignored if rendering not nullptr)
    PipelineRenderingInfo pipelineRenderingInfo;

    // common payloads
    IPipelineLayout*                        pipelineLayout;
    ShaderDesc                              shaderDesc;
    std::vector<EPipelineDynamicFeature::T> dynamicFeatures = {};
    EPrimitiveType::T                       primitiveType   = EPrimitiveType::TriangleList;
    RasterizationState                      rasterizationState;
    MultisampleState                        multisampleState{};
    DepthStencilState                       depthStencilState;
    ColorBlendState                         colorBlendState;
    ViewportState                           viewportState;
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
        std ::vector<AttachmentRef> inputAttachments;       // Single color attachment for now
        std::vector<AttachmentRef>  colorAttachments;       // Single color attachment for now
        AttachmentRef               depthAttachment   = {}; // optional, Single depth attachment for now
        AttachmentRef               resolveAttachment = {}; // optional, Single resolve attachment for now
    };

    std::string       label         = "None";
    ERenderingMode::T renderingMode = ERenderingMode::Auto; // Rendering mode

    std::vector<AttachmentDescription> attachments; // all attachment
    std::vector<SubpassInfo>           subpasses;   // Multiple subpasses can be defined, but currently we use a single subpass
    std::vector<SubpassDependency>     dependencies;

    [[nodiscard]] uint32_t getSubpassCount() const { return static_cast<uint32_t>(subpasses.size()); }
    [[nodiscard]] bool     isValidSubpassIndex(uint32_t index) const { return index < subpasses.size(); }
};



/**
 * @brief Image subresource range for layout transitions
 */
struct ImageSubresourceRange
{
    uint32_t aspectMask     = 1; // Aspect mask (color, depth, stencil)
    uint32_t baseMipLevel   = 0; // Base mip level
    uint32_t levelCount     = 1; // Mip level count
    uint32_t baseArrayLayer = 0; // Base array layer
    uint32_t layerCount     = 1; // Layer count
};

/**
 * @brief Image subresource layers for copy operations
 */
struct ImageSubresourceLayers
{
    uint32_t aspectMask     = 1; // Aspect mask (color, depth, stencil)
    uint32_t mipLevel       = 0; // Mip level
    uint32_t baseArrayLayer = 0; // Base array layer
    uint32_t layerCount     = 1; // Layer count
};

/**
 * @brief Buffer to image copy region (RHI layer)
 */
struct BufferImageCopy
{
    uint64_t               bufferOffset      = 0;  // Offset in bytes from start of buffer
    uint32_t               bufferRowLength   = 0;  // Buffer row length (0 = tightly packed)
    uint32_t               bufferImageHeight = 0;  // Buffer image height (0 = tightly packed)
    ImageSubresourceLayers imageSubresource  = {}; // Image subresource to copy to
    int32_t                imageOffsetX      = 0;  // X offset in image
    int32_t                imageOffsetY      = 0;  // Y offset in image
    int32_t                imageOffsetZ      = 0;  // Z offset in image
    uint32_t               imageExtentWidth  = 0;  // Width of region to copy
    uint32_t               imageExtentHeight = 0;  // Height of region to copy
    uint32_t               imageExtentDepth  = 1;  // Depth of region to copy
};


struct SwapchainCreateInfo
{
    // Surface and format configuration
    EFormat::T      imageFormat = EFormat::R8G8B8A8_UNORM; // TODO: rename to surfaceFormat
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
    bool bClipped                = true;
    bool preferredDifferentQueue = true; // Use different queues for graphics and present if possible

    uint32_t width  = 800;
    uint32_t height = 600;
};


struct DeviceFeature
{
    std::string name;
    bool        bRequired;

    bool operator==(const DeviceFeature& other) const
    {
        return name == other.name;
    }

    bool operator<(const DeviceFeature& other) const
    {
        return name < other.name;
    }
};


struct RenderCreateInfo
{
    ERenderAPI::T       renderAPI = ERenderAPI::Vulkan;
    SwapchainCreateInfo swapchainCI;
};



struct ImageCreateInfo
{
    std::string label  = "None";
    EFormat::T  format = EFormat::Undefined;
    struct
    {
        uint32_t width  = 0;
        uint32_t height = 0;
        uint32_t depth  = 1;
    } extent;
    uint32_t        mipLevels             = 1;
    uint32_t        arrayLayers           = 1;
    ESampleCount::T samples               = ESampleCount::Sample_1;
    EImageUsage::T  usage                 = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst);
    uint32_t        queueFamilyIndexCount = 0;
    const uint32_t* pQueueFamilyIndices   = nullptr;
    EImageLayout::T initialLayout         = EImageLayout::Undefined;
    uint32_t        flags                 = 0;
    // TODO: manual conversion
    // EImageLayout::T finalLayout           = EImageLayout::ShaderReadOnlyOptimal;
};



namespace EFilter
{
enum T
{
    Nearest,
    Linear,
    CubicExt,
    CubicImg,
};
} // namespace EFilter

namespace ESamplerMipmapMode
{
enum T
{
    Nearest,
    Linear,
};
} // namespace ESamplerMipmapMode

namespace ESamplerAddressMode
{
enum T
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};


} // namespace ESamplerAddressMode


struct SamplerDesc
{
    std::string label; // (debug) name

    EFilter::T             minFilter               = EFilter::Linear;
    EFilter::T             magFilter               = EFilter::Linear;
    ESamplerMipmapMode::T  mipmapMode              = ESamplerMipmapMode::Linear;
    ESamplerAddressMode::T addressModeU            = ESamplerAddressMode::Repeat;
    ESamplerAddressMode::T addressModeV            = ESamplerAddressMode::Repeat;
    ESamplerAddressMode::T addressModeW            = ESamplerAddressMode::Repeat;
    float                  mipLodBias              = 0.0f;
    bool                   anisotropyEnable        = false;
    float                  maxAnisotropy           = 1.0f;
    bool                   compareEnable           = false;
    ECompareOp::T          compareOp               = ECompareOp::Always;
    float                  minLod                  = 0.0f;
    float                  maxLod                  = 1.0f;
    bool                   unnormalizedCoordinates = false;

    enum EBorderColor
    {
        FloatTransparentBlack,
        IntTransparentBlack,
        FloatOpaqueBlack,
        IntOpaqueBlack,
        FloatOpaqueWhite,
        IntOpaqueWhite,
        Custom,
    };
    struct BorderColor
    {
        EBorderColor type;
        glm::vec4    color = glm::vec4(0.0f); // Used if type == Custom
    };

    BorderColor borderColor = {.type = EBorderColor::IntOpaqueBlack, .color = glm::vec4(1.0f)};



    bool operator==(const SamplerDesc& other) const
    {
        return minFilter == other.minFilter &&
               magFilter == other.magFilter &&
               mipmapMode == other.mipmapMode &&
               addressModeU == other.addressModeU &&
               addressModeV == other.addressModeV &&
               addressModeW == other.addressModeW &&
               mipLodBias == other.mipLodBias &&
               anisotropyEnable == other.anisotropyEnable &&
               maxAnisotropy == other.maxAnisotropy &&
               compareEnable == other.compareEnable &&
               compareOp == other.compareOp &&
               minLod == other.minLod &&
               maxLod == other.maxLod &&
               unnormalizedCoordinates == other.unnormalizedCoordinates;
    }
};



struct RenderHelper
{
    static bool isDepthOnlyFormat(EFormat::T format) { return format == EFormat::D32_SFLOAT || format == EFormat::D24_UNORM_S8_UINT; }
    static bool isDepthStencilFormat(EFormat::T format) { return format == EFormat::D24_UNORM_S8_UINT || format == EFormat::D32_SFLOAT_S8_UINT; }
};



/**
 * @brief Type-safe command buffer handle
 */
struct CommandBufferHandleTag
{};
using CommandBufferHandle = Handle<CommandBufferHandleTag>;
} // namespace ya


YA_REFLECT_ENUM_BEGIN(ya::ECullMode::T)
YA_REFLECT_ENUM_VALUE(None)
YA_REFLECT_ENUM_VALUE(Front)
YA_REFLECT_ENUM_VALUE(Back)
YA_REFLECT_ENUM_VALUE(FrontAndBack)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::ESampleCount::T)
YA_REFLECT_ENUM_VALUE(Sample_1)
YA_REFLECT_ENUM_VALUE(Sample_2)
YA_REFLECT_ENUM_VALUE(Sample_4)
YA_REFLECT_ENUM_VALUE(Sample_8)
YA_REFLECT_ENUM_VALUE(Sample_16)
YA_REFLECT_ENUM_VALUE(Sample_32)
YA_REFLECT_ENUM_VALUE(Sample_64)
YA_REFLECT_ENUM_END()
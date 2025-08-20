#pragma once

#include <Core/Log.h>

#include <vulkan/vulkan.h>

#include "Render/Render.h"

struct VulkanCommandPool;

struct VulkanUtils
{
    static bool hasStencilComponent(VkFormat format);

    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                   uint32_t typeFilter, VkMemoryPropertyFlags properties);

    static bool createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                             VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                             VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory);

    static void createImage(VkDevice device, VkPhysicalDevice physicalDevice,
                            uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageBits,
                            VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory &imageMemory);

    static VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    static void transitionImageLayout(VulkanCommandPool *pool, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

    static void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                  VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    static VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
    static void            endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkCommandBuffer commandBuffer);

    static void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                           VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    static VkFormat findSupportedImageFormat(VkPhysicalDevice             physicalDevice,
                                             const std::vector<VkFormat> &candidates,
                                             VkImageTiling                tiling,
                                             VkFormatFeatureFlags         features);

    static void createTextureImage(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue,
                                   const char *path, VkImage &outImage, VkDeviceMemory &outImageMemory);

    static bool isDepthOnlyFormat(VkFormat format) { return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT; }
    static bool isDepthStencilFormat(VkFormat format) { return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT; }
};



namespace EPrimitiveType
{
inline VkPrimitiveTopology toVk(T primitiveType)
{
    switch (primitiveType) {
    case TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Line:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    default:
        UNIMPLEMENTED();
    }
    return {};
}
} // namespace EPrimitiveType

namespace EVertexAttributeFormat
{
inline auto toVk(T format) -> VkFormat
{
    switch (format) {
    case Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:
        UNIMPLEMENTED();
    }
    return {};
}
} // namespace EVertexAttributeFormat

namespace ESampleCount
{
inline VkSampleCountFlagBits toVk(T sampleCount)
{
    switch (sampleCount) {
    case Sample_1:
        return VK_SAMPLE_COUNT_1_BIT;
    case Sample_2:
        return VK_SAMPLE_COUNT_2_BIT;
    case Sample_4:
        return VK_SAMPLE_COUNT_4_BIT;
    case Sample_8:
        return VK_SAMPLE_COUNT_8_BIT;
    case Sample_16:
        return VK_SAMPLE_COUNT_16_BIT;
    case Sample_32:
        return VK_SAMPLE_COUNT_32_BIT;
    case Sample_64:
        return VK_SAMPLE_COUNT_64_BIT;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace ESampleCount

namespace EPolygonMode
{
inline auto toVk(T mode) -> VkPolygonMode
{
    switch (mode) {
    case Fill:
        return VK_POLYGON_MODE_FILL;
    case Line:
        return VK_POLYGON_MODE_LINE;
    case Point:
        return VK_POLYGON_MODE_POINT;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace EPolygonMode

namespace ECullMode
{
inline auto toVk(T mode) -> VkCullModeFlags
{
    switch (mode) {
    case Back:
        return VK_CULL_MODE_BACK_BIT;
    case Front:
        return VK_CULL_MODE_FRONT_BIT;
    case None:
        return VK_CULL_MODE_NONE;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace ECullMode

namespace EFrontFaceType
{
inline auto toVk(T type) -> VkFrontFace
{
    switch (type) {
    case CounterClockWise:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    case ClockWise:
        return VK_FRONT_FACE_CLOCKWISE;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace EFrontFaceType

namespace ECompareOp
{
inline auto toVk(T op) -> VkCompareOp
{
    switch (op) {
    case Never:
        return VK_COMPARE_OP_NEVER;
    case Less:
        return VK_COMPARE_OP_LESS;
    case Equal:
        return VK_COMPARE_OP_EQUAL;
    case LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case Greater:
        return VK_COMPARE_OP_GREATER;
    case NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case GreaterOrEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case Always:
        return VK_COMPARE_OP_ALWAYS;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace ECompareOp

namespace EBlendFactor
{
inline auto toVk(T factor) -> VkBlendFactor
{
    switch (factor) {
    case Zero:
        return VK_BLEND_FACTOR_ZERO;
    case One:
        return VK_BLEND_FACTOR_ONE;
    case SrcColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case OneMinusSrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case DstColor:
        return VK_BLEND_FACTOR_DST_COLOR;
    case OneMinusDstColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case SrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case OneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case DstAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case OneMinusDstAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace EBlendFactor

namespace EColorComponent
{
inline auto toVk(T mask) -> VkColorComponentFlags
{
    VkColorComponentFlags vkMask = 0;
    if (mask & R)
        vkMask |= VK_COLOR_COMPONENT_R_BIT;
    if (mask & G)
        vkMask |= VK_COLOR_COMPONENT_G_BIT;
    if (mask & B)
        vkMask |= VK_COLOR_COMPONENT_B_BIT;
    if (mask & A)
        vkMask |= VK_COLOR_COMPONENT_A_BIT;
    return vkMask;
}
} // namespace EColorComponent

namespace EImageUsage
{
inline auto toVk(T usage) -> VkImageUsageFlags
{
    VkImageUsageFlags vkUsage = 0;
    if (usage & TransferSrc)
        vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage & TransferDst)
        vkUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage & Sampled)
        vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage & Storage)
        vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usage & ColorAttachment)
        vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage & DepthStencilAttachment)
        vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (usage & TransientAttachment)
        vkUsage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    if (usage & InputAttachment)
        vkUsage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    return vkUsage;
}
} // namespace EImageUsage

namespace EBlendOp
{
inline auto toVk(T op) -> VkBlendOp
{
    switch (op) {
    case Add:
        return VK_BLEND_OP_ADD;
    case Subtract:
        return VK_BLEND_OP_SUBTRACT;
    case ReverseSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case Min:
        return VK_BLEND_OP_MIN;
    case Max:
        return VK_BLEND_OP_MAX;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace EBlendOp

namespace ELogicOp
{
inline auto toVk(T op) -> VkLogicOp
{
    switch (op) {
    case Clear:
        return VK_LOGIC_OP_CLEAR;
    case And:
        return VK_LOGIC_OP_AND;
    case AndReverse:
        return VK_LOGIC_OP_AND_REVERSE;
    case Copy:
        return VK_LOGIC_OP_COPY;
    case AndInverted:
        return VK_LOGIC_OP_AND_INVERTED;
    case NoOp:
        return VK_LOGIC_OP_NO_OP;
    case Xor:
        return VK_LOGIC_OP_XOR;
    case Or:
        return VK_LOGIC_OP_OR;
    case Nor:
        return VK_LOGIC_OP_NOR;
    case Equivalent:
        return VK_LOGIC_OP_EQUIVALENT;
    case Invert:
        return VK_LOGIC_OP_INVERT;
    case OrReverse:
        return VK_LOGIC_OP_OR_REVERSE;
    case CopyInverted:
        return VK_LOGIC_OP_COPY_INVERTED;
    case OrInverted:
        return VK_LOGIC_OP_OR_INVERTED;
    case Nand:
        return VK_LOGIC_OP_NAND;
    case Set:
        return VK_LOGIC_OP_SET;
    default:
        UNREACHABLE();
    }
    return {}; // Default fallback
}
} // namespace ELogicOp

namespace EFormat
{
inline auto toVk(T format) -> VkFormat
{
    switch (format) {
    case Undefined:
        return VK_FORMAT_UNDEFINED;
    case R8G8B8A8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case B8G8R8A8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case D32_SFLOAT:
        return VK_FORMAT_D32_SFLOAT;
    case D32_SFLOAT_S8_UINT: // with stencil?
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case D24_UNORM_S8_UINT:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case ENUM_MAX:
    default:
        UNREACHABLE();
        break;
    }
    return VK_FORMAT_UNDEFINED;
}
} // namespace EFormat

namespace EAttachmentLoadOp
{
inline auto toVk(T loadOp) -> VkAttachmentLoadOp
{
    switch (loadOp) {
    case Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default:
        UNREACHABLE();
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}
} // namespace EAttachmentLoadOp

namespace EAttachmentStoreOp
{
inline auto toVk(T storeOp) -> VkAttachmentStoreOp
{
    switch (storeOp) {
    case Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case DontCare:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    default:
        UNREACHABLE();
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}
} // namespace EAttachmentStoreOp

namespace EPresentMode
{
inline auto toVk(T presentMode) -> VkPresentModeKHR
{
    switch (presentMode) {
    case Immediate:
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case Mailbox:
        return VK_PRESENT_MODE_MAILBOX_KHR;
    case FIFO:
        return VK_PRESENT_MODE_FIFO_KHR;
    case FIFO_Relaxed:
        return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    default:
        UNREACHABLE();
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}
} // namespace EPresentMode

namespace EColorSpace
{
inline auto toVk(T colorSpace) -> VkColorSpaceKHR
{
    switch (colorSpace) {
    case SRGB_NONLINEAR:
        return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    case HDR10_ST2084:
        return VK_COLOR_SPACE_HDR10_ST2084_EXT;
    case HDR10_HLG:
        return VK_COLOR_SPACE_HDR10_HLG_EXT;
    default:
        UNREACHABLE();
    }
    return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
}
} // namespace EColorSpace

namespace ESharingMode
{
inline auto toVk(T sharingMode) -> VkSharingMode
{
    switch (sharingMode) {
    case Exclusive:
        return VK_SHARING_MODE_EXCLUSIVE;
    case Concurrent:
        return VK_SHARING_MODE_CONCURRENT;
    default:
        UNREACHABLE();
    }
    return VK_SHARING_MODE_EXCLUSIVE;
}
} // namespace ESharingMode

namespace EPipelineDescriptorType
{
inline auto toVk(T type) -> VkDescriptorType
{
    switch (type) {
    case UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case SampledImage:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    default:
        UNIMPLEMENTED();
    }
    return {};
}

} // namespace EPipelineDescriptorType

namespace EShaderStage
{
inline auto toVk(T stage) -> VkShaderStageFlags
{
    VkShaderStageFlags bits = {};
    if (stage & Vertex)
        bits |= VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    if (stage & Fragment)
        bits |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stage & Geometry)
        bits |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (stage & Compute)
        bits |= VK_SHADER_STAGE_COMPUTE_BIT;

    return bits;
}
}; // namespace EShaderStage

namespace std
{
std::string to_string(VkResult v);
std::string to_string(VkFormat v);
std::string to_string(VkColorSpaceKHR v);
std::string to_string(VkPresentModeKHR v);
std::string to_string(VkSharingMode v);
std::string to_string(VkObjectType v);
} // namespace std

template <>
struct std::formatter<VkResult> : std::formatter<std::string>
{
    auto format(const VkResult &type, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "{}({})", std::to_string(type), static_cast<int32_t>(type));
    }
};



#define VK_CALL(x)                                                              \
    do {                                                                        \
        VkResult result = (x);                                                  \
        if (result != VK_SUCCESS) {                                             \
            NE_CORE_ERROR("Vulkan call" #x ", failed with error: {} ", result); \
        }                                                                       \
    } while (0)
#define VK_CALL_RET(x)                                                         \
    {                                                                          \
        VkResult result = (x);                                                 \
        if (result != VK_SUCCESS) {                                            \
            NE_CORE_ERROR("Vulkan call " #x "failed with error: {} ", result); \
            return {};                                                         \
        }                                                                      \
    }                                                                          \
    while (0)
#define CALL(x) (VkResult ret = (x), x ? ret : (NE_CORE_ERROR("Vulkan call failed with error: {} ", ret), false))


#define VK_DESTROY(t, device, obj)          \
    if (obj != VK_NULL_HANDLE) {            \
        vkDestroy##t(device, obj, nullptr); \
        obj = VK_NULL_HANDLE;               \
    }

#define VK_FREE(t, device, obj)          \
    if (obj != VK_NULL_HANDLE) {         \
        vkFree##t(device, obj, nullptr); \
        obj = VK_NULL_HANDLE;            \
    }


namespace EImageLayout
{
inline auto toVk(T layout) -> VkImageLayout
{
    switch (layout) {
    case Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ColorAttachmentOptimal:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case DepthStencilAttachmentOptimal:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ShaderReadOnlyOptimal:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case TransferSrcOptimal:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case TransferDstOptimal:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case PresentSrcKHR:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        UNREACHABLE();
    }
}

}; // namespace EImageLayout



inline std::string std::to_string(VkResult result)
{
    switch (result) {
        CASE_ENUM_TO_STR(VK_SUCCESS);
        CASE_ENUM_TO_STR(VK_NOT_READY);
        CASE_ENUM_TO_STR(VK_TIMEOUT);
        CASE_ENUM_TO_STR(VK_EVENT_SET);
        CASE_ENUM_TO_STR(VK_EVENT_RESET);
        CASE_ENUM_TO_STR(VK_INCOMPLETE);
        CASE_ENUM_TO_STR(VK_ERROR_OUT_OF_HOST_MEMORY);
        CASE_ENUM_TO_STR(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        CASE_ENUM_TO_STR(VK_ERROR_INITIALIZATION_FAILED);
        CASE_ENUM_TO_STR(VK_ERROR_DEVICE_LOST);
        CASE_ENUM_TO_STR(VK_ERROR_MEMORY_MAP_FAILED);
        CASE_ENUM_TO_STR(VK_ERROR_LAYER_NOT_PRESENT);
        CASE_ENUM_TO_STR(VK_ERROR_EXTENSION_NOT_PRESENT);
        CASE_ENUM_TO_STR(VK_ERROR_INCOMPATIBLE_DRIVER);
        CASE_ENUM_TO_STR(VK_ERROR_TOO_MANY_OBJECTS);
        CASE_ENUM_TO_STR(VK_ERROR_FORMAT_NOT_SUPPORTED);
        CASE_ENUM_TO_STR(VK_ERROR_FRAGMENTED_POOL);
        CASE_ENUM_TO_STR(VK_ERROR_UNKNOWN);
        CASE_ENUM_TO_STR(VK_ERROR_OUT_OF_POOL_MEMORY);
        CASE_ENUM_TO_STR(VK_ERROR_INVALID_EXTERNAL_HANDLE);
        CASE_ENUM_TO_STR(VK_ERROR_FRAGMENTATION);
        CASE_ENUM_TO_STR(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
        CASE_ENUM_TO_STR(VK_PIPELINE_COMPILE_REQUIRED);
        CASE_ENUM_TO_STR(VK_ERROR_NOT_PERMITTED_EXT);
        CASE_ENUM_TO_STR(VK_ERROR_SURFACE_LOST_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        CASE_ENUM_TO_STR(VK_SUBOPTIMAL_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_OUT_OF_DATE_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_VALIDATION_FAILED_EXT);
        CASE_ENUM_TO_STR(VK_ERROR_INVALID_SHADER_NV);
        CASE_ENUM_TO_STR(VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
        CASE_ENUM_TO_STR(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
        CASE_ENUM_TO_STR(VK_THREAD_IDLE_KHR);
        CASE_ENUM_TO_STR(VK_THREAD_DONE_KHR);
        CASE_ENUM_TO_STR(VK_OPERATION_DEFERRED_KHR);
        CASE_ENUM_TO_STR(VK_OPERATION_NOT_DEFERRED_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR);
        CASE_ENUM_TO_STR(VK_ERROR_COMPRESSION_EXHAUSTED_EXT);
        CASE_ENUM_TO_STR(VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
        // CASE_ENUM_TO_STR(VK_PIPELINE_BINARY_MISSING_KHR);
        // CASE_ENUM_TO_STR(VK_ERROR_NOT_ENOUGH_SPACE_KHR);
        CASE_ENUM_TO_STR(VK_RESULT_MAX_ENUM);
        CASE_ENUM_TO_STR(VK_ERROR_FEATURE_NOT_PRESENT);
        break;
    }
    return "";
}
inline std::string std::to_string(VkFormat v)
{
    switch (v) {

        CASE_ENUM_TO_STR(VK_FORMAT_UNDEFINED);
        CASE_ENUM_TO_STR(VK_FORMAT_R4G4_UNORM_PACK8);
        CASE_ENUM_TO_STR(VK_FORMAT_R4G4B4A4_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_B4G4R4A4_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R5G6B5_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_B5G6R5_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R5G5B5A1_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_B5G5R5A1_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_A1R5G5B5_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R8_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8_SRGB);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8_SRGB);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8_SRGB);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8_SRGB);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8A8_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8A8_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8A8_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8A8_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8A8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8A8_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R8G8B8A8_SRGB);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8A8_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8A8_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8A8_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8A8_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8A8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8A8_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8A8_SRGB);
        CASE_ENUM_TO_STR(VK_FORMAT_A8B8G8R8_UNORM_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A8B8G8R8_SNORM_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A8B8G8R8_USCALED_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A8B8G8R8_SSCALED_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A8B8G8R8_UINT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A8B8G8R8_SINT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A8B8G8R8_SRGB_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2R10G10B10_UNORM_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2R10G10B10_SNORM_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2R10G10B10_USCALED_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2R10G10B10_SSCALED_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2R10G10B10_UINT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2R10G10B10_SINT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2B10G10R10_UNORM_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2B10G10R10_SNORM_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2B10G10R10_USCALED_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2B10G10R10_SSCALED_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2B10G10R10_UINT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_A2B10G10R10_SINT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_R16_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16A16_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16A16_SNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16A16_USCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16A16_SSCALED);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16A16_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16A16_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16B16A16_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32B32_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32B32_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32B32_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32B32A32_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32B32A32_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R32G32B32A32_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64B64_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64B64_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64B64_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64B64A64_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64B64A64_SINT);
        CASE_ENUM_TO_STR(VK_FORMAT_R64G64B64A64_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_B10G11R11_UFLOAT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_D16_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_X8_D24_UNORM_PACK32);
        CASE_ENUM_TO_STR(VK_FORMAT_D32_SFLOAT);
        CASE_ENUM_TO_STR(VK_FORMAT_S8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_D16_UNORM_S8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_D24_UNORM_S8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_D32_SFLOAT_S8_UINT);
        CASE_ENUM_TO_STR(VK_FORMAT_BC1_RGB_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC1_RGB_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC2_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC2_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC3_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC3_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC4_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC4_SNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC5_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC5_SNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC6H_UFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC6H_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC7_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_BC7_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_EAC_R11_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_EAC_R11_SNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_EAC_R11G11_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_EAC_R11G11_SNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_5x4_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_5x4_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_5x5_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_5x5_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_6x5_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_6x5_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_6x6_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_6x6_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x5_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x5_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x6_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x6_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x8_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x8_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x5_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x5_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x6_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x6_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x8_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x8_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x10_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x10_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_12x10_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_12x10_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_12x12_UNORM_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_12x12_SRGB_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_G8B8G8R8_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_B8G8R8G8_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G8_B8R8_2PLANE_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_R10X6_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R10X6G10X6_UNORM_2PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R12X4_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R12X4G12X4_UNORM_2PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G16B16G16R16_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_B16G16R16G16_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G16_B16R16_2PLANE_420_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G16_B16R16_2PLANE_422_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G8_B8R8_2PLANE_444_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_G16_B16R16_2PLANE_444_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_A4R4G4B4_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_A4B4G4R4_UNORM_PACK16);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK);
        CASE_ENUM_TO_STR(VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK);
        // CASE_ENUM_TO_STR(VK_FORMAT_A1B5G5R5_UNORM_PACK16);
        // CASE_ENUM_TO_STR(VK_FORMAT_A8_UNORM);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG);
        CASE_ENUM_TO_STR(VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG);
        // CASE_ENUM_TO_STR(VK_FORMAT_R16G16_SFIXED5_NV);
        CASE_ENUM_TO_STR(VK_FORMAT_MAX_ENUM);
        CASE_ENUM_TO_STR(VK_FORMAT_R16G16_S10_5_NV);
        CASE_ENUM_TO_STR(VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR);
        CASE_ENUM_TO_STR(VK_FORMAT_A8_UNORM_KHR);
        break;
    }
    return "";
}

inline std::string std::to_string(VkColorSpaceKHR v)
{
    switch (v) {
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_BT709_LINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_BT709_NONLINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_BT2020_LINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_HDR10_ST2084_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_DOLBYVISION_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_HDR10_HLG_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_PASS_THROUGH_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_DISPLAY_NATIVE_AMD);
        CASE_ENUM_TO_STR(VK_COLOR_SPACE_MAX_ENUM_KHR);
    }
    return "";
}

inline std::string std::to_string(VkPresentModeKHR v)
{
    switch (v) {
        CASE_ENUM_TO_STR(VK_PRESENT_MODE_IMMEDIATE_KHR);
        CASE_ENUM_TO_STR(VK_PRESENT_MODE_MAILBOX_KHR);
        CASE_ENUM_TO_STR(VK_PRESENT_MODE_FIFO_KHR);
        CASE_ENUM_TO_STR(VK_PRESENT_MODE_FIFO_RELAXED_KHR);
        CASE_ENUM_TO_STR(VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR);
        CASE_ENUM_TO_STR(VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR);
        // CASE_ENUM_TO_STR(VK_PRESENT_MODE_FIFO_LATEST_READY_EXT);
        CASE_ENUM_TO_STR(VK_PRESENT_MODE_MAX_ENUM_KHR);
        break;
    }
    return "";
}

inline std::string std::to_string(VkSharingMode v)
{
    switch (v) {
        CASE_ENUM_TO_STR(VK_SHARING_MODE_EXCLUSIVE);
        CASE_ENUM_TO_STR(VK_SHARING_MODE_CONCURRENT);
        CASE_ENUM_TO_STR(VK_SHARING_MODE_MAX_ENUM);
        break;
    }
    return "";
}

inline std::string std::to_string(VkObjectType v)
{
    switch (v) {

        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_UNKNOWN);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_INSTANCE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_PHYSICAL_DEVICE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DEVICE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_QUEUE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_SEMAPHORE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_COMMAND_BUFFER);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_FENCE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DEVICE_MEMORY);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_BUFFER);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_IMAGE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_EVENT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_QUERY_POOL);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_BUFFER_VIEW);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_IMAGE_VIEW);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_SHADER_MODULE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_PIPELINE_CACHE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_PIPELINE_LAYOUT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_RENDER_PASS);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_PIPELINE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_SAMPLER);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DESCRIPTOR_POOL);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DESCRIPTOR_SET);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_FRAMEBUFFER);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_COMMAND_POOL);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_PRIVATE_DATA_SLOT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_SURFACE_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_SWAPCHAIN_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DISPLAY_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DISPLAY_MODE_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_VIDEO_SESSION_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_CU_MODULE_NVX);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_CU_FUNCTION_NVX);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_VALIDATION_CACHE_EXT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV);
        // TODO: use macro to detect enum features
        // CASE_ENUM_TO_STR(VK_OBJECT_TYPE_CUDA_MODULE_NV);
        // CASE_ENUM_TO_STR(VK_OBJECT_TYPE_CUDA_FUNCTION_NV);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_MICROMAP_EXT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_OPTICAL_FLOW_SESSION_NV);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_SHADER_EXT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_PIPELINE_BINARY_KHR);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT);
        CASE_ENUM_TO_STR(VK_OBJECT_TYPE_MAX_ENUM);
        break;
    }
    return "";
}

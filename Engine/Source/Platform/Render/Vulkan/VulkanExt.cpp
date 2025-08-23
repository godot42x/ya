#include "VulkanExt.h"
#include "VulkanRender.h"

#define GET_VK_FUNC(value, name) \
    value = (PFN_##name)vkGetInstanceProcAddr(_renderer->getInstance(), #name);

void VulkanDebugUtils::init()
{
    GET_VK_FUNC(pfnCreateDebugUtilsMessengerEXT, vkCreateDebugUtilsMessengerEXT);
    GET_VK_FUNC(pfnDestroyDebugUtilsMessengerEXT, vkDestroyDebugUtilsMessengerEXT);

    // Load debug report callback functions (deprecated but still used)
    // pfnCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
    //     vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT");
    // pfnDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)
    //     vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT");

    GET_VK_FUNC(pfnSetDebugUtilsObjectNameEXT, vkSetDebugUtilsObjectNameEXT);
    YA_CORE_ASSERT(pfnSetDebugUtilsObjectNameEXT, "Failed to load vkSetDebugUtilsObjectNameEXT function!");
}

void VulkanDebugUtils::rewriteDebugUtils()
{
    if (!pfnCreateDebugUtilsMessengerEXT) {
        YA_CORE_WARN("Debug utils messenger creation function not available!");
        return;
    }

    // Create the debug utils messenger
    const VkDebugUtilsMessengerCreateInfoEXT &createInfo = getDebugMessengerCreateInfoExt();
    if (pfnCreateDebugUtilsMessengerEXT(_renderer->getInstance(), &createInfo, nullptr, &_debugUtilsMessenger) != VK_SUCCESS) {
        YA_CORE_ASSERT(false, "Failed to create debug utils messenger!");
    }
}

void VulkanDebugUtils::destroy()
{
    if (pfnDestroyDebugUtilsMessengerEXT && _debugUtilsMessenger) {
        pfnDestroyDebugUtilsMessengerEXT(_renderer->getInstance(), _debugUtilsMessenger, nullptr);
    }
}

const VkDebugUtilsMessengerCreateInfoEXT &VulkanDebugUtils::getDebugMessengerCreateInfoExt()
{

    static auto callback = [](VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
                              VkDebugUtilsMessageTypeFlagsEXT             type,
                              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                              void                                       *pUserData) -> VkBool32 {
        // Build type string
        std::string typeString;
        if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
            typeString += " Performance";
        }
        if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
            typeString += " Validation";
        }
        if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
            typeString += " General";
        }
        if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) {
            typeString += " Device Address Binding";
        }
        if (typeString.empty()) {
            typeString = " Unknown";
        }

        // Format message with all available information
        std::string formattedMessage = "--------------------------------------------------------\n";
        formattedMessage += std::format("[Vulkan {}] [ {} ] | MessageID = {:#x}\n",
                                        typeString,
                                        pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "Unknown",
                                        pCallbackData->messageIdNumber);

        formattedMessage += pCallbackData->pMessage;

        // Add object information if available
        if (pCallbackData->objectCount > 0) {
            formattedMessage += std::format("\nObjects: {}", pCallbackData->objectCount);
            for (uint32_t i = 0; i < pCallbackData->objectCount; ++i) {
                const auto &obj = pCallbackData->pObjects[i];
                formattedMessage += std::format("\n    [{}] {} {} {:#x}",
                                                i,
                                                std::to_string(obj.objectType),
                                                obj.pObjectName ? obj.pObjectName : "Unnamed",
                                                obj.objectHandle);
            }
        }

        // Add label information if available
        if (pCallbackData->cmdBufLabelCount > 0) {
            formattedMessage += std::format("\nCommand Buffer Labels: {}", pCallbackData->cmdBufLabelCount);
            for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; ++i) {
                formattedMessage += std::format("\n    [{}] {}", i, pCallbackData->pCmdBufLabels[i].pLabelName);
            }
        }

        if (pCallbackData->queueLabelCount > 0) {
            formattedMessage += std::format("\nQueue Labels: {}", pCallbackData->queueLabelCount);
            for (uint32_t i = 0; i < pCallbackData->queueLabelCount; ++i) {
                formattedMessage += std::format("\n    [{}] {}", i, pCallbackData->pQueueLabels[i].pLabelName);
            }
        }

        // Log based on severity
        switch (severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            YA_CORE_TRACE("{}", formattedMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            YA_CORE_INFO("{}", formattedMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            YA_CORE_WARN("{}", formattedMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            YA_CORE_ERROR("{}", formattedMessage);
            break;
        default:
            YA_CORE_ERROR("Unknown severity: {}", formattedMessage);
            break;
        }

        return VK_FALSE;
    };

    static VkDebugUtilsMessengerCreateInfoEXT ci{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = callback,
    };

    return ci;
}

void VulkanDebugUtils::setObjectName(VkObjectType objectType, uint64_t objectHandle, const char *name)
{
    VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext        = nullptr,
        .objectType   = objectType,
        .objectHandle = objectHandle,
        .pObjectName  = name,
    };

    if (pfnSetDebugUtilsObjectNameEXT) {
        VK_CALL(pfnSetDebugUtilsObjectNameEXT(_renderer->getLogicalDevice(), &nameInfo));
    }
}

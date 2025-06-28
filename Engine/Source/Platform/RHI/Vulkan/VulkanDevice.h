#pragma once
#include "Core/Base.h"

#include "Core/Delegate.h"


#include <string>
using namespace std::literals;
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>


#include "Render/Device.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"
#include "VulkanResourceManager.h"
#include "VulkanSwapChain.h"
#include "VulkanUtils.h"

#include "WindowProvider.h"
#include <Render/Shader.h>



#include <vulkan/vulkan.h>

struct GLFWState;

#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);



struct VulkanState
{
    friend struct VulkanUtils;
    friend struct VulkanRenderPass;

    const std::vector<const char *> m_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation",
    };

    const std::vector<const char *> m_DeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, // "VK_KHR_swapchain"
    };
    const bool m_EnableValidationLayers = false;

  private:

    VkInstance   m_Instance;
    VkSurfaceKHR m_Surface;

    VkDebugUtilsMessengerEXT m_DebugMessengerCallback;
    VkDebugReportCallbackEXT m_DebugReportCallback; // deprecated

    // Function pointers for debug extensions
    PFN_vkCreateDebugUtilsMessengerEXT  pfnCreateDebugUtilsMessengerEXT = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = nullptr;
    PFN_vkCreateDebugReportCallbackEXT  pfnCreateDebugReportCallbackEXT = nullptr;
    PFN_vkDestroyDebugReportCallbackEXT pfnDestroyDebugReportCallbackEXT = nullptr;

    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice         m_LogicalDevice  = VK_NULL_HANDLE;

    VkQueue m_PresentQueue  = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

    // Command pool belongs to device level
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    // Use separate classes for better organization
    VulkanSwapChain       m_swapChain;
    VulkanRenderPass      m_renderPass;
    VulkanPipeline        m_pipeline;
    VulkanResourceManager m_resourceManager;

    VkImage        m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView    m_depthImageView;

    std::vector<VkCommandBuffer> m_commandBuffers;

    VkSemaphore m_imageAvailableSemaphore;
    VkSemaphore m_renderFinishedSemaphore;


    struct QueueFamilyIndices
    {
        int graphicsFamilyIdx  = -1;
        int supportedFamilyIdx = -1;

        bool is_complete()
        {
            return graphicsFamilyIdx >= 0 && supportedFamilyIdx >= 0;
        }

        /**
         * @brief Queries and identifies suitable queue families for Vulkan operations
         *
         * This static method searches through available queue families on a physical device
         * to find ones that support the required graphics operations and surface presentation.
         * It evaluates each queue family against the specified flags and surface compatibility.
         *
         * @param surface The Vulkan surface handle to check for presentation support
         * @param device The physical device to query queue families from
         * @param flags The queue capability flags to match (defaults to VK_QUEUE_GRAPHICS_BIT)
         *
         * @return QueueFamilyIndices structure containing the indices of suitable queue families
         *         for graphics operations and surface presentation
         *
         * @note The function will return early if all required queue families are found
         *       before iterating through all available families for optimization
         */
        static QueueFamilyIndices query(VkSurfaceKHR surface, VkPhysicalDevice device, VkQueueFlagBits flags = VK_QUEUE_GRAPHICS_BIT)
        {
            QueueFamilyIndices indices;
            uint32_t           count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queue_families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queue_families.data());


            int familyIndex = 0;
            for (const auto &queue_family : queue_families) {
                if (queue_family.queueCount > 0)
                {
                    if (queue_family.queueFlags & flags) {

                        indices.graphicsFamilyIdx = familyIndex;
                    }
                    VkBool32 bSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, familyIndex, surface, &bSupport);
                    if (bSupport)
                    {
                        indices.supportedFamilyIdx = familyIndex;
                    }
                }

                if (indices.is_complete()) {
                    break;
                }
                ++familyIndex;
            }

            return indices;
        }
    };


  public:

    WindowProvider *_windowProvider = nullptr;
    void           *nativeWindow    = nullptr;
    template <typename T>
    void *getNativeWindow()
    {
        return static_cast<T *>(nativeWindow);
    }

    void init(WindowProvider *windowProvider)
    {
        _windowProvider = windowProvider;
        nativeWindow    = _windowProvider->getNativeWindowPtr();

        create_instance();

        if (m_EnableValidationLayers)
        {
            setupDebugMessengerExt();
            setupReportCallbackExt();
        }

        bool ok = onCreateSurface.ExecuteIfBound(&(*m_Instance), &m_Surface);
        NE_CORE_ASSERT(ok, "Failed to create surface!");

        //  find a suitable physical device
        {
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

            // Enumerate physical devices
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
            if (deviceCount == 0) {
                NE_CORE_ASSERT(false, "Failed to find GPUs with Vulkan support!");
            }

            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

            // Select the first suitable device
            for (const auto &device : devices) {
                if (is_device_suitable(device)) {
                    physicalDevice = device;
                    break;
                }
            }

            NE_CORE_ASSERT(physicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU!");

            m_PhysicalDevice = physicalDevice;
        }

        createLogicDevice();
        createCommandPool(); // CommandPool moved to device level

        // Initialize separate components
        m_swapChain.initialize(m_LogicalDevice, m_PhysicalDevice, m_Surface, _windowProvider);
        m_swapChain.create();


        // Initialize resource manager
        m_resourceManager.initialize(m_LogicalDevice, m_PhysicalDevice, m_commandPool, m_GraphicsQueue);

        m_renderPass.initialize(m_LogicalDevice, m_PhysicalDevice, m_swapChain.getImageFormat());
        m_renderPass.createRenderPass();

        createDepthResources();
        m_renderPass.createFramebuffers(m_swapChain.getImageViews(), m_depthImageView, m_swapChain.getExtent());

        // Pipeline now gets resource manager for descriptor layout
        m_pipeline.initialize(m_LogicalDevice, m_PhysicalDevice);
        m_pipeline.createGraphicsPipeline("VulkanTest.glsl", m_renderPass.getRenderPass(), m_swapChain.getExtent());

        createCommandBuffers();
        createSemaphores();
    }



    void OnPostUpdate()
    {
        vkDeviceWaitIdle(m_LogicalDevice);
    };

    void Uninit()
    {
        vkDeviceWaitIdle(m_LogicalDevice);


        // Cleanup resource manager
        m_resourceManager.cleanup();

        vkDestroySemaphore(m_LogicalDevice, m_renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(m_LogicalDevice, m_imageAvailableSemaphore, nullptr);

        vkDestroyCommandPool(m_LogicalDevice, m_commandPool, nullptr); // CommandPool cleanup moved here
        vkDestroyDevice(m_LogicalDevice, nullptr);

        if (m_EnableValidationLayers)
        {
            destroyDebugCallBackExt();
            destroyDebugReportCallbackExt();
        }

        onReleaseSurface.ExecuteIfBound(&(*m_Instance), &m_Surface);
        vkDestroyInstance(m_Instance, nullptr);
    }

  public:
    Delegate<bool(VkInstance, VkSurfaceKHR *inSurface)> onCreateSurface;
    Delegate<void(VkInstance, VkSurfaceKHR *inSurface)> onReleaseSurface;
    Delegate<std::vector<const char *>()>               onGetRequiredExtensions;



  private:

    void create_instance();
    void createLogicDevice();
    void createCommandPool();
    void createDepthResources();
    void createCommandBuffers();
    void createSemaphores();
    void recreateSwapChain();

    // Missing method declarations
    const VkDebugUtilsMessengerCreateInfoEXT &getDebugMessengerCreateInfoExt()
    {
        static VkDebugUtilsMessengerCreateInfoEXT info{
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = // nullptr,
            [](VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
               VkDebugUtilsMessageTypeFlagsEXT             type,
               const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
               void                                       *pUserData) -> VkBool32 {
                NE_CORE_DEBUG("[ Validation Layer ] severity: {}, type: {} --> {}",
                              std::to_string(severity),
                              type,
                              pCallbackData->pMessage);
                return VK_FALSE;
            },
        };

        return info;
    }
    void setupDebugMessengerExt();
    void setupReportCallbackExt();
    void loadDebugFunctionPointers();  // Add this new function
    void destroyDebugCallBackExt();
    void destroyDebugReportCallbackExt();

    bool is_device_suitable(VkPhysicalDevice device);
    bool is_validation_layers_supported();
    bool is_device_extension_support(VkPhysicalDevice device);


    VkShaderModule create_shader_module(const std::vector<uint32_t> &spv_binary)
    {
        VkShaderModuleCreateInfo shaderModuleCreateInfo = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = spv_binary.size() * 4, // VUID-VkShaderModuleCreateInfo-codeSize-08735
            .pCode    = spv_binary.data(),
        };

        VkShaderModule shaderModule;
        if (VK_SUCCESS != vkCreateShaderModule(m_LogicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule))
        {
            panic("failed to create shader module");
        }
        return shaderModule;
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if (typeFilter & (1 << i) &&                                                 // vertexbuffer
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) // ����
            {
                return i;
            }
        }
        panic("failed to find suitable memory type!");
        return -1;
    }
};

struct VulkanDevice : public LogicalDevice
{
    VulkanState     _vulkanState;
    WindowProvider *windowProvider = nullptr;

    virtual bool init(const InitParams &params) override
    {
        windowProvider = &params.windowProvider;

#if USE_SDL

        auto sdlWindow = static_cast<SDLWindowProvider *>(windowProvider);
        _vulkanState.onCreateSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            return sdlWindow->onCreateVkSurface(instance, surface);
        });
        _vulkanState.onReleaseSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            sdlWindow->onDestroyVkSurface(instance, surface);
        });
        _vulkanState.onGetRequiredExtensions.set([sdlWindow]() {
            return sdlWindow->onGetVkInstanceExtensions();
        });

#endif

        _vulkanState.init(windowProvider);

        // UNIMPLEMENTED();
        return false;
    }

    void destroy() override
    {
        _vulkanState.Uninit();
        windowProvider = nullptr;
    }
};


#undef panic
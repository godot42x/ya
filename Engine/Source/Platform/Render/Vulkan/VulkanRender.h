#pragma once
#include "Core/Base.h"

#include "Core/Delegate.h"

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


using namespace std::literals;
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>



#include "Render/Render.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"
#include "VulkanResourceManager.h"
#include "VulkanSwapChain.h"
#include "VulkanUtils.h"


#include "WindowProvider.h"
#include <Render/Shader.h>



#include <vulkan/vulkan.h>



#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);

struct VulkanRender : public IRender
{
    friend struct VulkanUtils;
    friend struct VulkanRenderPass;

    const std::vector<DeviceFeature> _instanceValidationLayers = {
        {"VK_LAYER_KHRONOS_validation", true}, // "VK_LAYER_KHRONOS_validation"
    };
    const std::vector<DeviceFeature> _instanceExtensions = {
        {VK_KHR_SURFACE_EXTENSION_NAME, true}, // "VK_KHR_surface"
    };

    const std::vector<DeviceFeature> _deviceLayers = {
        {"VK_LAYER_KHRONOS_validation", false}, // Make validation layer optional
    };

    const std::vector<DeviceFeature> _deviceExtensions = {
        {VK_KHR_SWAPCHAIN_EXTENSION_NAME, true}, // "VK_KHR_swapchain"
    };
    const bool m_EnableValidationLayers = true; // Will be disabled automatically if OBS is detected

  private:

    VkInstance   m_Instance;
    VkSurfaceKHR m_Surface;

    VkDebugUtilsMessengerEXT m_DebugMessengerCallback;
    VkDebugReportCallbackEXT m_DebugReportCallback; // deprecated

    // Function pointers for debug extensions
    // PFN_vkCreateDebugUtilsMessengerEXT  pfnCreateDebugUtilsMessengerEXT  = nullptr;
    // PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = nullptr;
    // PFN_vkCreateDebugReportCallbackEXT  pfnCreateDebugReportCallbackEXT  = nullptr;
    // PFN_vkDestroyDebugReportCallbackEXT pfnDestroyDebugReportCallbackEXT = nullptr;

    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice         m_LogicalDevice  = VK_NULL_HANDLE;

    VkQueue m_PresentQueue  = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

    // Command pool belongs to device level
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    // Configuration parameters
    SwapchainCreateInfo m_swapchainCI;
    // RenderPassCreateInfo m_renderPassCI;
    bool m_bVsyncEnabled = true;

    // Use separate classes for better organization
    VulkanSwapChain       m_swapChain;
    VulkanPipelineManager m_pipelineManager;
    VulkanResourceManager m_resourceManager;

    // VkImage        m_depthImage;
    // VkDeviceMemory m_depthImageMemory;
    // VkImageView    m_depthImageView;

    std::vector<VkCommandBuffer> m_commandBuffers;

    VkSemaphore m_imageAvailableSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    VkFence     m_inFlightFence;

    struct QueueFamilyIndices
    {
        int32_t queueFamilyIndex = -1; // Graphics queue family index
        int32_t queueCount       = -1;
    };

    QueueFamilyIndices _graphicsQueueFamily;
    QueueFamilyIndices _presentQueueFamily;



  public:

    WindowProvider *_windowProvider = nullptr;
    void           *nativeWindow    = nullptr;
    template <typename T>
    void *getNativeWindow()
    {
        return static_cast<T *>(nativeWindow);
    }

    virtual bool init(const InitParams &params) override
    {
#if USE_SDL
        auto sdlWindow = static_cast<SDLWindowProvider *>(params.windowProvider);

        onCreateSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            return sdlWindow->onCreateVkSurface(instance, surface);
        });
        onReleaseSurface.set([sdlWindow](VkInstance instance, VkSurfaceKHR *surface) {
            sdlWindow->onDestroyVkSurface(instance, surface);
        });
        onGetRequiredInstanceExtensions.set([sdlWindow]() {
            std::vector<DeviceFeature> extensions;
            for (const char *ext : sdlWindow->onGetVkInstanceExtensions()) {
                extensions.push_back({ext, true});
            }
            return extensions;
        });
#endif

        bool success = initInternal(params);
        NE_CORE_ASSERT(success, "Failed to initialize Vulkan render!");

        return true;
    }
    virtual void destroy() override
    {
        destroyInternal();
        std::exit(-1);
    }


    bool initInternal(const InitParams &params)
    {

        _windowProvider = params.windowProvider;
        nativeWindow    = _windowProvider->getNativeWindowPtr();

        // Store configurations
        m_swapchainCI   = params.swapchainCI;
        m_bVsyncEnabled = params.bVsync;

        createInstance();

        if (m_EnableValidationLayers)
        {
            // setupDebugMessengerExt();
            // setupReportCallbackExt();
        }

        createSurface();

        //  find a suitable physical device
        findPhysicalDevice();
        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            destroy();
        }

        destroy();

        // createLogicDevice();
        createCommandPool();

        // Initialize separate components with configuration
        m_swapChain.initialize(m_LogicalDevice, m_PhysicalDevice, m_Surface, _windowProvider, m_swapchainCI);
        m_swapChain.create();

        // Initialize resource manager
        m_resourceManager.initialize(m_LogicalDevice, m_PhysicalDevice, m_commandPool, m_GraphicsQueue);

        // Initialize render pass with custom configuration
        // m_renderPass.initialize(m_LogicalDevice, m_PhysicalDevice, m_swapChain.getImageFormat(), m_renderPassCI);
        // m_renderPass.create();

        // createDepthResources();
        // m_renderPass.createFramebuffers(m_swapChain.getImageViews(), m_depthImageView, m_swapChain.getExtent());

        // Pipeline creation with custom configuration from render pass
        m_pipelineManager.initialize(m_LogicalDevice, m_PhysicalDevice);

        // Create multiple pipelines if provided in render pass config

        // NE_CORE_ASSERT(!m_renderPassCI.subpasses.empty() && !m_renderPassCI.pipelineCIs.empty() && m_renderPassCI.pipelineCIs.size() == m_renderPassCI.subpasses.size(),
        //                "Render pass configuration must have at least one subpass and matching pipeline configurations!");

        // Create each pipeline from the configuration
        // for (size_t i = 0; i < m_renderPassCI.pipelineCIs.size(); ++i) {

        //     const auto &ci           = m_renderPassCI.pipelineCIs[i];
        //     std::string pipelineName = "Pipeline_" + std::to_string(i);

        //     // Use shader name as pipeline name if available
        //     if (!ci.shaderCreateInfo.shaderName.empty()) {
        //         pipelineName = ci.shaderCreateInfo.shaderName;
        //     }

        //     bool success = m_pipelineManager.createPipeline(pipelineName, ci, m_renderPass.getRenderPass(), m_swapChain.getExtent());
        //     if (!success) {
        //         NE_CORE_ERROR("Failed to create pipeline: {}", pipelineName);
        //     }
        // }

        createCommandBuffers();
        createSemaphores();
        createFences();

        // Create triangle rendering resources
        // createVertexBuffer();
        // createUniformBuffer();

        return true;
    }

    void destroyInternal()
    {
        if (m_LogicalDevice) {
            vkDeviceWaitIdle(m_LogicalDevice);
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }

        // if (m_depthImage) {
        //     vkDestroyImage(m_LogicalDevice, m_depthImage, nullptr);
        //     vkDestroyImageView(m_LogicalDevice, m_depthImageView, nullptr);
        //     vkFreeMemory(m_LogicalDevice, m_depthImageMemory, nullptr);
        // }

        // Cleanup resource manager
        m_resourceManager.cleanup();

        m_pipelineManager.cleanup();
        m_swapChain.cleanup();
        // m_renderPass.cleanup();

        if (m_inFlightFence)
        {
            vkDestroyFence(m_LogicalDevice, m_inFlightFence, nullptr);
        }
        if (m_renderFinishedSemaphore)
        {
            vkDestroySemaphore(m_LogicalDevice, m_renderFinishedSemaphore, nullptr);
        }
        if (m_imageAvailableSemaphore)
        {
            vkDestroySemaphore(m_LogicalDevice, m_imageAvailableSemaphore, nullptr);
        }
        if (m_commandPool) {
            vkDestroyCommandPool(m_LogicalDevice, m_commandPool, nullptr);
        }
        if (m_LogicalDevice)
        {
            vkDestroyDevice(m_LogicalDevice, nullptr);
        }

        if (m_EnableValidationLayers)
        {
            // destroyDebugCallBackExt();
            // destroyDebugReportCallbackExt();
        }

        onReleaseSurface.ExecuteIfBound(&(*m_Instance), &m_Surface);
        vkDestroyInstance(m_Instance, nullptr);
    }



    void OnPostUpdate()
    {
        vkDeviceWaitIdle(m_LogicalDevice);
    };

    void DrawFrame()
    {
        // drawTriangle();
    }



  public:
    Delegate<bool(VkInstance, VkSurfaceKHR *inSurface)> onCreateSurface;
    Delegate<void(VkInstance, VkSurfaceKHR *inSurface)> onReleaseSurface;
    Delegate<std::vector<DeviceFeature>()>              onGetRequiredInstanceExtensions;

    // Getter methods for 2D renderer access
    VkDevice         getLogicalDevice() const { return m_LogicalDevice; }
    VkPhysicalDevice getPhysicalDevice() const { return m_PhysicalDevice; }
    VkCommandPool    getCommandPool() const { return m_commandPool; }
    VkQueue          getGraphicsQueue() const { return m_GraphicsQueue; }

  private:

    void createInstance();
    void findPhysicalDevice();

    void createSurface();
    void createLogicDevice();
    void createCommandPool();
    // void createDepthResources();
    void createCommandBuffers();
    void createSemaphores();
    void createFences();
    void recreateSwapChain();

    // Triangle rendering functions
    // void createVertexBuffer();
    // void createUniformBuffer();
    // void updateUniformBuffer();
    // void drawTriangle();

    // Missing method declarations
    const VkDebugUtilsMessengerCreateInfoEXT &getDebugMessengerCreateInfoExt()
    {
        static VkDebugUtilsMessengerCreateInfoEXT info{
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext           = nullptr,
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
                switch (severity) {
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                    NE_CORE_INFO("[ValidationLayer] {}", pCallbackData->pMessage);
                    break;
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                    NE_CORE_WARN("[ValidationLayer] {}", pCallbackData->pMessage);
                    break;
                case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                    NE_CORE_ERROR("[ValidationLayer] {}", pCallbackData->pMessage);
                    break;
                default:
                    NE_CORE_ASSERT(false, "Unknown validation layer severity!");
                }
                return VK_FALSE;
            },
        };

        return info;
    }


    // may be deprecated, not the debug was created in the create_instance()
    // void loadDebugFunctionPointers();
    // void setupDebugMessengerExt();
    // void setupReportCallbackExt();
    // void destroyDebugCallBackExt();
    // void destroyDebugReportCallbackExt();

    bool isDeviceSuitable(VkPhysicalDevice device);
    bool isInstanceSuitable(const std::set<DeviceFeature> &extensions,
                            const std::set<DeviceFeature> &layers,
                            std::vector<const char *>     &extensionNames,
                            std::vector<const char *>     &layerNames);



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



#undef panic

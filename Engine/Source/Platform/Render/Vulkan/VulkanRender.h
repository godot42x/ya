#pragma once

#include "Core/Base.h"

#include "Core/Delegate.h"

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>



#include "Render/Render.h"
#include "VulkanCommandBuffer.h"
#include "VulkanExt.h"
#include "VulkanPipeline.h"
#include "VulkanQueue.h"
#include "VulkanRenderPass.h"
#include "VulkanSwapChain.h"
#include "VulkanUtils.h"



#include "WindowProvider.h"
#include <Render/Shader.h>



#define panic(...) NE_CORE_ASSERT(false, __VA_ARGS__);

struct QueueFamilyIndices
{
    int32_t queueFamilyIndex = -1; // Graphics queue family index
    int32_t queueCount       = -1;
};

struct VulkanRender : public IRender
{
    friend struct VulkanUtils;
    friend struct VulkanRenderPass;

    const std::vector<DeviceFeature> _instanceLayers           = {};
    const std::vector<DeviceFeature> _instanceValidationLayers = {
        {.name = "VK_LAYER_KHRONOS_validation", .bRequired = true}, // "VK_LAYER_KHRONOS_validation"
    };
    const std::vector<DeviceFeature> _instanceExtensions = {
        {.name = VK_KHR_SURFACE_EXTENSION_NAME, .bRequired = true}, // "VK_KHR_surface"
    };

    const std::vector<DeviceFeature> _deviceLayers = {
        // {"VK_LAYER_KHRONOS_validation", false}, // Make validation layer optional
    };
    const std::vector<DeviceFeature> _deviceExtensions = {
        {.name = VK_KHR_SWAPCHAIN_EXTENSION_NAME, .bRequired = true}, // "VK_KHR_swapchain"
    };
    const bool m_EnableValidationLayers = true; // Will be disabled automatically if OBS is detected

    bool bSupportDebugUtils = false; // Whether VK_EXT_DEBUG_UTILS_EXTENSION_NAME is supported

  private:

    uint32_t apiVersion = 0;

    VkInstance   _instance;
    VkSurfaceKHR _surface;


    QueueFamilyIndices _graphicsQueueFamily;
    QueueFamilyIndices _presentQueueFamily;



    VkPhysicalDevice                 m_PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties _physicalMemoryProperties;

    VkDevice m_LogicalDevice = VK_NULL_HANDLE;



    VulkanSwapChain         *m_swapChain;
    std::vector<VulkanQueue> _presentQueues;
    std::vector<VulkanQueue> _graphicsQueues;

    // owning to logical device
    std::unique_ptr<VulkanCommandPool> _graphicsCommandPool = nullptr;
    std::unique_ptr<VulkanCommandPool> _presentCommandPool  = nullptr;
    VkPipelineCache                    _pipelineCache       = VK_NULL_HANDLE;

    VkSemaphore m_imageAvailableSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    VkFence     m_inFlightFence;

    std::unique_ptr<VulkanDebugUtils> _debugUtils = nullptr;



    void *nativeWindow = nullptr;



  public:
    IWindowProvider *_windowProvider = nullptr;

    Delegate<bool(VkInstance, VkSurfaceKHR *inSurface)> onCreateSurface;
    Delegate<void(VkInstance, VkSurfaceKHR *inSurface)> onReleaseSurface;
    Delegate<std::vector<DeviceFeature>()>              onGetRequiredInstanceExtensions;

  public:

    VulkanRender() = default;

    template <typename T>
    T *getNativeWindow()
    {
        return static_cast<T *>(nativeWindow);
    }

    bool init(const RenderCreateInfo &ci) override
    {

        bool success = initInternal(ci);
        NE_CORE_ASSERT(success, "Failed to initialize Vulkan render!");

        return true;
    }
    virtual void destroy() override
    {
        destroyInternal();
    }


    bool                      isGraphicsPresentSameQueueFamily() const { return _graphicsQueueFamily.queueFamilyIndex == _presentQueueFamily.queueFamilyIndex; }
    const QueueFamilyIndices &getGraphicsQueueFamilyInfo() const { return _graphicsQueueFamily; }
    const QueueFamilyIndices &getPresentQueueFamilyInfo() const { return _presentQueueFamily; }

  private:
    void terminate()
    {
        destroy();
        std::exit(-1);
    }

    bool initInternal(const RenderCreateInfo &ci)
    {
        initWindow(ci);
        nativeWindow = _windowProvider->getNativeWindowPtr<SDL_Window>();

        createInstance();


        createSurface();

        //  find a suitable physical device
        findPhysicalDevice();
        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            terminate();
        }

        if (m_EnableValidationLayers && bSupportDebugUtils)
        {
            _debugUtils = std::make_unique<VulkanDebugUtils>(this);
            _debugUtils->init();
            // preferred default validation layers callback
            // _debugUtils->create();
        }

        if (!createLogicDevice(1, 1)) {
            terminate();
        }


        m_swapChain = new VulkanSwapChain(this);
        m_swapChain->recreate(ci.swapchainCI);

        if (!createCommandPool()) {
            terminate();
        }

        createPipelineCache();
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

        VK_DESTROY(PipelineCache, m_LogicalDevice, _pipelineCache);

        if (m_swapChain)
        {
            // Cleanup swap chain resources
            m_swapChain->cleanup();
            delete m_swapChain;
        }
        // m_renderPass.cleanup();

        _graphicsCommandPool->cleanup();
        if (_presentCommandPool) {
            _presentCommandPool->cleanup();
        }
        if (m_LogicalDevice)
        {
            vkDestroyDevice(m_LogicalDevice, nullptr);
        }

        if (m_EnableValidationLayers && bSupportDebugUtils)
        {
            _debugUtils->destroy();
        }

        onReleaseSurface.executeIfBound(&(*_instance), &_surface);
        vkDestroyInstance(_instance, nullptr);
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
    void recreateSwapChain();

    [[nodiscard]] uint32_t         getApiVersion() const { return apiVersion; }
    [[nodiscard]] VkInstance       getInstance() const { return _instance; }
    [[nodiscard]] VkSurfaceKHR     getSurface() const { return _surface; }
    [[nodiscard]] VkDevice         getLogicalDevice() const { return m_LogicalDevice; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_PhysicalDevice; }
    [[nodiscard]] VulkanSwapChain *getSwapChain() const { return m_swapChain; }

    [[nodiscard]] VkPipelineCache getPipelineCache() const { return _pipelineCache; }

    std::vector<VulkanQueue> &getGraphicsQueues() { return _graphicsQueues; }
    std::vector<VulkanQueue> &getPresentQueues() { return _presentQueues; }

    [[nodiscard]] VulkanDebugUtils *getDebugUtils() const { return _debugUtils.get(); }

    void setDebugObjectName(VkObjectType objectType, void *objectHandle, const std::string &name)
    {
        if (getDebugUtils()) {
            getDebugUtils()->setObjectName(objectType, (uint64_t)objectHandle, name.c_str());
        }
    }

    [[nodiscard]] int32_t getMemoryIndex(VkMemoryPropertyFlags properties, uint32_t memoryTypeBits) const;

    std::unique_ptr<VulkanCommandPool>::pointer getGraphicsCommandPool() const { return _graphicsCommandPool.get(); }

    VkCommandBuffer beginIsolateCommands()
    {
        VkCommandBuffer ret = VK_NULL_HANDLE;
        _graphicsCommandPool->allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, ret);
        begin(ret, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        return ret;
    }
    void endIsolateCommands(VkCommandBuffer commandBuffer)
    {
        end(commandBuffer);
        getGraphicsQueues()[0].submit({commandBuffer});
        getGraphicsQueues()[0].waitIdle();
        vkFreeCommandBuffers(m_LogicalDevice, _graphicsCommandPool->_handle, 1, &commandBuffer);
    }

    void allocateCommandBuffers(uint32_t size, std::vector<VkCommandBuffer> &outCommandBuffers);



  private:

    void initWindow(const RenderCreateInfo &ci);
    void createInstance();
    void findPhysicalDevice();

    void createSurface();


    bool createLogicDevice(uint32_t graphicsQueueCount, uint32_t presentQueueCount);
    bool createCommandPool();

    void createPipelineCache();
    // void createDepthResources();

    bool isDeviceSuitable(const std::set<DeviceFeature> &extensions,
                          const std::set<DeviceFeature> &layers,
                          std::vector<const char *>     &extensionNames,
                          std::vector<const char *>     &layerNames);
    bool isInstanceSuitable(const std::set<DeviceFeature> &extensions,
                            const std::set<DeviceFeature> &layers,
                            std::vector<const char *>     &extensionNames,
                            std::vector<const char *>     &layerNames);


    bool isFeatureSupported(
        std::string_view                          contextStr,
        const std::vector<VkExtensionProperties> &availableExtensions,
        const std::vector<VkLayerProperties>     &availableLayers,
        const std::vector<DeviceFeature>         &requestExtensions,
        const std::vector<DeviceFeature>         &requestLayers,
        std::vector<const char *>                &outExtensionNames,
        std::vector<const char *>                &outLayerNames,
        bool                                      bDebug = false);
};



#undef panic

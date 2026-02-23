#pragma once

#include "Core/Base.h"

#include "Core/Delegate.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>



#include "Render/Core/Swapchain.h"
#include "Render/Core/TextureFactory.h"
#include "Render/Render.h"
#include "VulkanCommandBuffer.h"
#include "VulkanDescriptorSet.h"
#include "VulkanExt.h"
#include "VulkanPipeline.h"
#include "VulkanQueue.h"
#include "VulkanRenderPass.h"
#include "VulkanSwapChain.h"
#include "VulkanTextureFactory.h"
#include "VulkanUtils.h"



#include "WindowProvider.h"
#include <Render/Shader.h>



#define panic(...) YA_CORE_ASSERT(false, __VA_ARGS__);


namespace ya
{

// Forward declarations

extern VkObjectType toVk(ERenderObject type);

struct QueueFamilyIndices
{
    int32_t queueFamilyIndex = -1; // Graphics queue family index
    int32_t queueCount       = -1;
};

struct PhysicalDeviceCandidate
{
    VkPhysicalDevice           device           = VK_NULL_HANDLE;
    QueueFamilyIndices         graphicsQueue    = {};
    QueueFamilyIndices         presentQueue     = {};
    VkPhysicalDeviceProperties properties       = {};
    uint32_t                   queueFamilyCount = 0;
    int                        score            = 0;
};

struct VulkanRender : public IRender
{
    friend struct VulkanUtils;
    friend struct VulkanRenderPass;

    const std::vector<ya::DeviceFeature> _instanceLayers           = {};
    const std::vector<ya::DeviceFeature> _instanceValidationLayers = {
        {.name = "VK_LAYER_KHRONOS_validation", .bRequired = true}, // "VK_LAYER_KHRONOS_validation"
    };
    const std::vector<ya::DeviceFeature> _instanceExtensions = {
        {.name = VK_KHR_SURFACE_EXTENSION_NAME, .bRequired = true}, // "VK_KHR_surface"
    };

    const std::vector<ya::DeviceFeature> _deviceLayers = {
        // {"VK_LAYER_KHRONOS_validation", false}, // Make validation layer optional
    };
    const std::vector<ya::DeviceFeature> _deviceExtensions = {
        {.name = VK_KHR_SWAPCHAIN_EXTENSION_NAME, .bRequired = true},                 // "VK_KHR_swapchain"
        {.name = VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME, .bRequired = false}, // "VK_EXT_extended_dynamic_state3" for polygon mode
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

    std::vector<PhysicalDeviceCandidate> _deviceCandidates;

    VkDevice m_LogicalDevice = VK_NULL_HANDLE;



    ISwapchain *_swapChain;

    bool                     bOnlyOnePresentQueue = false;
    std::vector<VulkanQueue> _presentQueues;
    std::vector<VulkanQueue> _graphicsQueues;

    // owning to logical device
    std::unique_ptr<VulkanCommandPool> _graphicsCommandPool = nullptr;
    std::unique_ptr<VulkanCommandPool> _presentCommandPool  = nullptr;
    VkPipelineCache                    _pipelineCache       = VK_NULL_HANDLE;
    VkDescriptorPool                   _descriptorPool      = VK_NULL_HANDLE;

    VkSemaphore m_imageAvailableSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    VkFence     m_inFlightFence;

    std::unique_ptr<VulkanDebugUtils>     _debugUtils       = nullptr;
    VulkanDescriptorHelper               *_descriptorHelper = nullptr; // Raw pointer to avoid incomplete type issue
    std::unique_ptr<VulkanTextureFactory> _textureFactory   = nullptr; // 纹理工厂

    PFN_vkQueueBeginDebugUtilsLabelEXT _pfnQueueBeginDebugUtilsLabelEXT = nullptr;
    PFN_vkQueueEndDebugUtilsLabelEXT   _pfnQueueEndDebugUtilsLabelEXT   = nullptr;


    // std::unordered_map<std::string, VkSampler> _samplers; // sampler name -> sampler

    void *nativeWindow = nullptr;


    // used for GPU-CPU(fame), GPU internal(image) sync
    static constexpr uint32_t flightFrameSize = 1;
    uint32_t                  currentFrameIdx = 0;
    std::vector<VkSemaphore>  frameImageAvailableSemaphores;  // 每个飞行帧的图像可用信号量
    std::vector<VkFence>      frameFences;                    // 每个飞行帧的fence
    std::vector<VkSemaphore>  imageSubmittedSignalSemaphores; // 渲染完成信号量（每张swapchain image）


  public:
    IWindowProvider *_windowProvider = nullptr;

    Delegate<bool(VkInstance, VkSurfaceKHR *inSurface)> onCreateSurface;
    Delegate<void(VkInstance, VkSurfaceKHR *inSurface)> onReleaseSurface;
    Delegate<std::vector<DeviceFeature>()>              onGetRequiredInstanceExtensions;

  public:

    VulkanRender() = default;
    ~VulkanRender(); // Need definition in .cpp to properly destroy unique_ptr<VulkanDescriptor>

    template <typename T>
    T *getNativeWindow()
    {
        return static_cast<T *>(nativeWindow);
    }

    bool init(const ya::RenderCreateInfo &ci) override
    {
        IRender::init(ci);
        YA_PROFILE_FUNCTION_LOG();

        bool success = initInternal(ci);
        YA_CORE_ASSERT(success, "Failed to initialize Vulkan render!");

        // 初始化纹理工厂
        _textureFactory = std::make_unique<VulkanTextureFactory>(this);

        return true;
    }
    void destroy() override
    {
        destroyInternal();
    }

    bool begin(int32_t *imageIndex) override;
    bool end(int32_t imageIndex, std::vector<void *> CommandBuffers) override;

    // IRender interface implementations
    void getWindowSize(int &width, int &height) const override
    {
        if (_windowProvider) {
            _windowProvider->getWindowSize(width, height);
        }
    }

    void setVsync(bool enabled) override
    {
        if (_swapChain) {
            // Cast to VulkanSwapChain for Vulkan-specific functionality
            auto *vkSwapchain = static_cast<VulkanSwapChain *>(_swapChain);
            vkSwapchain->setVsync(enabled);
        }
    }

    uint32_t getSwapchainWidth() const override
    {
        if (!_swapChain) return 0;
        auto extent = _swapChain->getExtent();
        return extent.width;
    }

    uint32_t getSwapchainHeight() const override
    {
        if (!_swapChain) return 0;
        auto extent = _swapChain->getExtent();
        return extent.height;
    }

    uint32_t getSwapchainImageCount() const override
    {
        return _swapChain ? _swapChain->getImageCount() : 0;
    }

    void allocateCommandBuffers(uint32_t count, std::vector<std::shared_ptr<ICommandBuffer>> &outBuffers) override;

    void submitToQueue(
        const std::vector<void *> &cmdBufs,
        const std::vector<void *> &waitSemaphores,
        const std::vector<void *> &signalSemaphores,
        void                      *fence = nullptr) override;

    int presentImage(int32_t imageIndex, const std::vector<void *> &waitSemaphores) override;

    void    *getCurrentImageAvailableSemaphore() override { return frameImageAvailableSemaphores[currentFrameIdx]; }
    void    *getCurrentFrameFence() override { return frameFences[currentFrameIdx]; }
    uint32_t getCurrentFrameIndex() const override { return currentFrameIdx; }
    void    *getRenderFinishedSemaphore(uint32_t imageIndex) override { return imageSubmittedSignalSemaphores[imageIndex]; }

    void *createSemaphore(const char *debugName = nullptr) override;
    void  destroySemaphore(void *semaphore) override;
    void  advanceFrame() override { currentFrameIdx = (currentFrameIdx + 1) % flightFrameSize; }
    void  queueBeginLabel(const char* labelName, const float* colorRGBA = nullptr) override;
    void  queueEndLabel() override;

    // IRender interface: get texture factory
    ITextureFactory *getTextureFactory() override { return _textureFactory.get(); }

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
            _debugUtils->initInstanceLevel();
            // preferred default validation layers callback
            // _debugUtils->create();
        }

        if (!createLogicDevice(1, 1)) {
            terminate();
        }

        // Initialize VK_EXT_extended_dynamic_state3 function pointer
        initExtensionFunctions();
        if (m_EnableValidationLayers && bSupportDebugUtils)
        {
            _debugUtils->initDeviceLevel();
        }

        _swapChain = new VulkanSwapChain(this);
        _swapChain->as<VulkanSwapChain>()->recreate(ci.swapchainCI);

        if (!createCommandPool()) {
            terminate();
        }
        createPipelineCache();
        createSyncResources(static_cast<int32_t>(_swapChain->getImageCount()));
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

        releaseSyncResources();
        VK_DESTROY(PipelineCache, m_LogicalDevice, _pipelineCache);

        if (_swapChain)
        {
            // Cleanup swap chain resources
            // Cast to VulkanSwapChain for Vulkan-specific cleanup
            auto *vkSwapchain = static_cast<VulkanSwapChain *>(_swapChain);
            vkSwapchain->cleanup();
            delete _swapChain;
        }
        // m_renderPass.cleanup();

        _graphicsCommandPool->cleanup();
        if (_presentCommandPool) {
            _presentCommandPool->cleanup();
        }
        // for (auto &[name, sampler] : _samplers) {
        //     VK_DESTROY_A(Sampler, m_LogicalDevice, sampler, getAllocator());
        // }

        // MARK: destroy device

        if (m_LogicalDevice)
        {
            vkDestroyDevice(m_LogicalDevice, nullptr);
        }

        if (m_EnableValidationLayers && bSupportDebugUtils)
        {
            _debugUtils->destroy();
        }
        onReleaseSurface.executeIfBound(&(*_instance), &_surface);
        vkDestroyInstance(_instance, getAllocator());
    }


  public:

    [[nodiscard]] uint32_t         getApiVersion() const { return apiVersion; }
    [[nodiscard]] IWindowProvider *getWindowProvider() const { return _windowProvider; }
    [[nodiscard]] VkInstance       getInstance() const { return _instance; }
    [[nodiscard]] VkSurfaceKHR     getSurface() const { return _surface; }
    [[nodiscard]] VkDevice         getDevice() const { return m_LogicalDevice; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_PhysicalDevice; }
    [[nodiscard]] ISwapchain      *getSwapchain() const { return _swapChain; }
    template <typename T>
    [[nodiscard]] T *getSwapchain() const { return static_cast<T *>(_swapChain); }

    [[nodiscard]] VkPipelineCache getPipelineCache() const { return _pipelineCache; }

    [[nodiscard]] bool                      isGraphicsPresentSameQueueFamily() const { return _graphicsQueueFamily.queueFamilyIndex == _presentQueueFamily.queueFamilyIndex; }
    [[nodiscard]] const QueueFamilyIndices &getGraphicsQueueFamilyInfo() const { return _graphicsQueueFamily; }
    [[nodiscard]] const QueueFamilyIndices &getPresentQueueFamilyInfo() const { return _presentQueueFamily; }

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
    const ::VkAllocationCallbacks              *getAllocator();


    void waitIdle() override { VK_CALL(vkDeviceWaitIdle(m_LogicalDevice)); }

    // IRender interface: isolated commands
    ICommandBuffer *beginIsolateCommands(const std::string &context = "") override
    {
        VkCommandBuffer vkCmdBuf = VK_NULL_HANDLE;
        _graphicsCommandPool->allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, vkCmdBuf);
        VulkanCommandPool::begin(vkCmdBuf, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        setDebugObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, vkCmdBuf, "IsolatedCommandBuffer_" + context);
        // Create a temporary VulkanCommandBuffer wrapper
        // Note: This is managed manually and will be deleted in endIsolateCommands
        return new VulkanCommandBuffer(this, vkCmdBuf);
    }

    void endIsolateCommands(ICommandBuffer *commandBuffer) override
    {
        auto vkCmdBuf = commandBuffer->getHandleAs<VkCommandBuffer>();

        VulkanCommandPool::end(vkCmdBuf);
        getGraphicsQueues()[0].submit({vkCmdBuf});
        getGraphicsQueues()[0].waitIdle();
        vkFreeCommandBuffers(m_LogicalDevice, _graphicsCommandPool->_handle, 1, &vkCmdBuf);

        // Delete the wrapper
        delete commandBuffer;
    }

    // IRender interface: get swapchain
    ISwapchain *getSwapchain() override { return _swapChain; }

    // IRender interface: get descriptor helper
    IDescriptorSetHelper *getDescriptorHelper() override;

  protected:
    // IRender interface: get native window handle
    void *getNativeWindowHandle() const override
    {
        return nativeWindow;
    }

  public:
    void allocateCommandBuffers(uint32_t size, std::vector<::VkCommandBuffer> &outCommandBuffers);


  private:

    void initWindow(const RenderCreateInfo &ci);
    void createInstance();
    void findPhysicalDevice();

    void createSurface();


    bool createLogicDevice(uint32_t graphicsQueueCount, uint32_t presentQueueCount);
    void initExtensionFunctions();
    bool createCommandPool();

    void createPipelineCache();
    // void createDepthResources();

    bool isDeviceSuitable(const std::set<ya::DeviceFeature> &extensions,
                          const std::set<ya::DeviceFeature> &layers,
                          std::vector<const char *>         &extensionNames,
                          std::vector<const char *>         &layerNames);
    bool isInstanceSuitable(const std::set<ya::DeviceFeature> &extensions,
                            const std::set<ya::DeviceFeature> &layers,
                            std::vector<const char *>         &extensionNames,
                            std::vector<const char *>         &layerNames);


    bool isFeatureSupported(
        std::string_view                          contextStr,
        const std::vector<VkExtensionProperties> &availableExtensions,
        const std::vector<VkLayerProperties>     &availableLayers,
        const std::vector<ya::DeviceFeature>     &requestExtensions,
        const std::vector<ya::DeviceFeature>     &requestLayers,
        std::vector<const char *>                &outExtensionNames,
        std::vector<const char *>                &outLayerNames,
        bool                                      bDebug = false);

    void createSyncResources(int32_t swapchainImageSize);
    void releaseSyncResources();
};


} // namespace ya

#undef panic

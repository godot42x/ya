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



#define panic(...) YA_CORE_ASSERT(false, __VA_ARGS__);

struct QueueFamilyIndices
{
    int32_t queueFamilyIndex = -1; // Graphics queue family index
    int32_t queueCount       = -1;
};

namespace ya
{

namespace EFilter
{
enum T
{
    Nearest,
    Linear,
    CubicExt,
    CubicImg,
};
inline auto toVk(T filter) -> VkFilter
{
    switch (filter)
    {
    case Nearest:
        return VkFilter::VK_FILTER_NEAREST;
    case Linear:
        return VkFilter::VK_FILTER_LINEAR;
    case CubicExt:
        return VkFilter::VK_FILTER_CUBIC_EXT;
    case CubicImg:
        return VkFilter::VK_FILTER_CUBIC_IMG;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace EFilter

namespace ESamplerMipmapMode
{
enum T
{
    Nearest,
    Linear,
};
inline auto toVk(T mode) -> VkSamplerMipmapMode
{
    switch (mode)
    {
    case Nearest:
        return VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case Linear:
        return VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR;
    default:
        UNREACHABLE();
    }
    return {};
}
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

inline auto toVk(T mode) -> VkSamplerAddressMode
{
    switch (mode)
    {
    case Repeat:
        return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case MirroredRepeat:
        return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case ClampToEdge:
        return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case ClampToBorder:
        return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default:
        UNREACHABLE();
    }
    return {};
}
} // namespace ESamplerAddressMode


struct SamplerCreateInfo
{
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

    bool operator==(const SamplerCreateInfo &other) const
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

} // namespace ya

struct VulkanRender : public ya::IRender
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



    VulkanSwapChain         *_swapChain;
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

    std::unique_ptr<VulkanDebugUtils> _debugUtils = nullptr;


    std::unordered_map<std::string, VkSampler> _samplers; // sampler name -> sampler

    void *nativeWindow = nullptr;


    // used for GPU-CPU(fame), GPU internal(image) sync
    static constexpr uint32_t flightFrameSize = 1;
    uint32_t                  currentFrameIdx = 0;
    std::vector<VkSemaphore>  frameImageAvailableSemaphores;
    std::vector<VkFence>      frameFences;
    std::vector<VkSemaphore>  imageSubmittedSignalSemaphores; // 每张swapchain image渲染完成信号量


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
        ya::IRender::init(ci);
        YA_PROFILE_FUNCTION();

        bool success = initInternal(ci);
        YA_CORE_ASSERT(success, "Failed to initialize Vulkan render!");

        return true;
    }
    void destroy() override
    {
        destroyInternal();
    }

    bool begin(int32_t *imageIndex) override;
    bool end(int32_t imageIndex, std::vector<void *> CommandBuffers) override;



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


        _swapChain = new VulkanSwapChain(this);
        _swapChain->recreate(ci.swapchainCI);

        if (!createCommandPool()) {
            terminate();
        }
        createPipelineCache();
        createSyncResources(_swapChain->getImages().size());
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
            _swapChain->cleanup();
            delete _swapChain;
        }
        // m_renderPass.cleanup();

        _graphicsCommandPool->cleanup();
        if (_presentCommandPool) {
            _presentCommandPool->cleanup();
        }
        for (auto &[name, sampler] : _samplers) {
            VK_DESTROY_A(Sampler, m_LogicalDevice, sampler, getAllocator());
        }

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



    void OnPostUpdate()
    {
        vkDeviceWaitIdle(m_LogicalDevice);
    };

    void DrawFrame()
    {
        // drawTriangle();
    }



  public:

    [[nodiscard]] uint32_t         getApiVersion() const { return apiVersion; }
    [[nodiscard]] IWindowProvider *getWindowProvider() const { return _windowProvider; }
    [[nodiscard]] VkInstance       getInstance() const { return _instance; }
    [[nodiscard]] VkSurfaceKHR     getSurface() const { return _surface; }
    [[nodiscard]] VkDevice         getLogicalDevice() const { return m_LogicalDevice; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_PhysicalDevice; }
    [[nodiscard]] VulkanSwapChain *getSwapChain() const { return _swapChain; }

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
    const VkAllocationCallbacks                *getAllocator();


    bool      createSampler(const std::string &name, const ya::SamplerCreateInfo &ci, VkSampler &outSampler);
    VkSampler getSampler(const std::string &name)
    {
        auto it = _samplers.find(name);
        if (it != _samplers.end()) {
            return it->second;
        }
        YA_CORE_WARN("Sampler not found: {}", name);
        return VK_NULL_HANDLE;
    }

  public:
    VkCommandBuffer beginIsolateCommands()
    {
        VkCommandBuffer ret = VK_NULL_HANDLE;
        _graphicsCommandPool->allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, ret);
        VulkanCommandPool::begin(ret, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        return ret;
    }
    void endIsolateCommands(VkCommandBuffer commandBuffer)
    {
        VulkanCommandPool::end(commandBuffer);
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

    void createSyncResources(int32_t swapchainImageSize);
    void releaseSyncResources();
};



#undef panic

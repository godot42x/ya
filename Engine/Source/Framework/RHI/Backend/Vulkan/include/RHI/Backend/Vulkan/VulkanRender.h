#pragma once

#include "Core/Base.h"

#include "Core/Delegate.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>



#include "RHI/Core/Swapchain.h"
#include "RHI/Core/BuiltinTextureSource.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanCommandBuffer.h"
#include "RHI/Backend/Vulkan/VulkanDescriptorSet.h"
#include "RHI/Backend/Vulkan/VulkanExt.h"
#include "RHI/Backend/Vulkan/VulkanPipeline.h"
#include "RHI/Backend/Vulkan/VulkanQueue.h"
#include "RHI/Backend/Vulkan/VulkanRenderPass.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/Backend/Vulkan/VulkanRenderSurfaceContext.h"
#include "RHI/Backend/Vulkan/VulkanRenderResourceFactory.h"
#include "RHI/Backend/Vulkan/VulkanUtils.h"



#include <RHI/Shader.h>

#include "RHI/Backend/Vulkan/VulkanMemoryAllocator.h"


#define panic(...) YA_CORE_ASSERT(false, __VA_ARGS__);

namespace ya
{

struct VulkanSampler;

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

struct YA_RHI_BACKEND_API VulkanRender : public IRender
{
    friend struct VulkanUtils;
    friend struct VulkanRenderPass;
    friend struct VulkanRenderSurfaceContext;

    const std::vector<ya::DeviceFeature> _instanceLayers           = {};
    const std::vector<ya::DeviceFeature> _instanceValidationLayers = {
        {.name = "VK_LAYER_KHRONOS_validation", .bRequired = true}, // "VK_LAYER_KHRONOS_validation"
    };
    const std::vector<ya::DeviceFeature> _instanceExtensions = {
        {.name = VK_KHR_SURFACE_EXTENSION_NAME, .bRequired = true}, // "VK_KHR_surface"
#ifdef __APPLE__
        {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, true},
#endif
    };

    const std::vector<ya::DeviceFeature> _deviceLayers = {
        // {"VK_LAYER_KHRONOS_validation", false}, // Make validation layer optional
    };
    
    const std::vector<ya::DeviceFeature> _deviceExtensions = {
        {.name = VK_KHR_SWAPCHAIN_EXTENSION_NAME, .bRequired = true},                 // "VK_KHR_swapchain"
        {.name = VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME, .bRequired = false},   // "VK_EXT_extended_dynamic_state" for cull mode (pre-1.3 devices)
        {.name = VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME, .bRequired = false}, // "VK_EXT_extended_dynamic_state3" for polygon mode
#ifdef VK_EXT_MESH_SHADER_EXTENSION_NAME
        {.name = VK_EXT_MESH_SHADER_EXTENSION_NAME, .bRequired = false},
#endif
    };
    const bool m_EnableValidationLayers = true; // Will be disabled automatically if OBS is detected

    bool bSupportDebugUtils       = false; // Whether VK_EXT_DEBUG_UTILS_EXTENSION_NAME is supported
    bool bSupportsGeometryShader  = false;
    bool bSupportsExtendedDynamicState = false; // CULL_MODE dynamic state legal (1.3 core or extension enabled)
    RenderCapabilities _capabilities{};

  private:

    uint32_t apiVersion = 0;

    VkInstance   _instance;

    /// One window whose present requirements were known before this device
    /// existed (see `RenderCreateInfo::startupSurfaces`). The device creates
    /// the VkSurfaceKHR here -- before device pick, because present support is
    /// what the queue plan is chosen from -- and owns it until teardown; the
    /// surface context that presents through it does not.
    struct StartupSurface
    {
        INativeWindow*      window = nullptr;
        SwapchainCreateInfo swapchainCI{};
        VkSurfaceKHR        surface = VK_NULL_HANDLE;
    };

    /// One registry slot of this device's surface set. `generation == 0` marks
    /// the slot free, so an id held across a destroy fails to resolve instead
    /// of naming whatever registered into the slot next.
    struct SurfaceSlot
    {
        std::unique_ptr<VulkanRenderSurfaceContext> context;
        uint32_t                                    generation = 0;
    };

    std::vector<StartupSurface> _startupSurfaces;
    std::vector<SurfaceSlot>    _surfaces;
    uint32_t                    _nextSurfaceGeneration = 1;

    /// When `startupSurfaces` is empty the device exists to produce offscreen
    /// work only: no window, no swapchain, no present family.
    [[nodiscard]] bool hasStartupSurfaces() const { return !_startupSurfaces.empty(); }
    /// Register an already-built context and hand back its id. Appends to a free
    /// slot when there is one, so a window opened and closed repeatedly does not
    /// grow the registry without bound.
    SurfaceId registerSurface(std::unique_ptr<VulkanRenderSurfaceContext> context);

    QueueFamilyIndices _graphicsQueueFamily;
    QueueFamilyIndices _presentQueueFamily;



    VkPhysicalDevice                 m_PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties       _physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties _physicalMemoryProperties;

    std::vector<PhysicalDeviceCandidate> _deviceCandidates;

  public:
    struct PhysicalDeviceInfo
    {
        std::string deviceName;

    } _selectedDeviceInfo;

  private:

    VkDevice     m_LogicalDevice = VK_NULL_HANDLE;
    VmaAllocator _vmaAllocator   = VK_NULL_HANDLE;



    bool                     bOnlyOnePresentQueue = false;
    std::vector<VulkanQueue> _presentQueues;
    std::vector<VulkanQueue> _graphicsQueues;

    // owning to logical device
    std::unique_ptr<VulkanCommandPool> _graphicsCommandPool = nullptr;
    std::unique_ptr<VulkanCommandPool> _presentCommandPool  = nullptr;
    VkPipelineCache                    _pipelineCache       = VK_NULL_HANDLE;
    VkDescriptorPool                   _descriptorPool      = VK_NULL_HANDLE;

    std::unique_ptr<VulkanDebugUtils>     _debugUtils       = nullptr;
    VulkanDescriptorHelper*               _descriptorHelper = nullptr; // Raw pointer to avoid incomplete type issue
    std::unique_ptr<VulkanRenderResourceFactory> _resourceFactory = nullptr;

    PFN_vkQueueBeginDebugUtilsLabelEXT _pfnQueueBeginDebugUtilsLabelEXT = nullptr;
    PFN_vkQueueEndDebugUtilsLabelEXT   _pfnQueueEndDebugUtilsLabelEXT   = nullptr;
    PFN_vkCmdResetQueryPool            _pfnCmdResetQueryPool            = nullptr;


    // std::unordered_map<std::string, VkSampler> _samplers; // sampler name -> sampler

    // Host-injected shader compile/cache service (see IRender::setShaderStorage).
    std::shared_ptr<ShaderStorage> _shaderStorage = nullptr;
    // Graphics cards excluded from device selection (host-provided via RenderCreateInfo).
    std::vector<std::string> _disabledGraphicsCards;
    // Monotonic recorded-frame generation. Advanced once per frame by
    // `beginRecordedFrame()`, and the only clock deferred deletion and the GPU
    // timing ring use (see IRender::recordedFrameIndex).
    uint64_t _frameIndex = 0;
    /// One fence per flight slot: this frame's GPU work is done when they all
    /// are. It lives on the DEVICE because "has this frame finished" is a fact
    /// about the frame, not about whichever window happened to present -- a
    /// frame that presents two windows submits work for both under this fence,
    /// and a frame that presents nothing still has one.
    std::vector<VkFence> _frameFences;
    /// Whether the current frame has already re-armed its slot's fence. The
    /// first `submitFrame` of a frame resets it; a frame that never submits
    /// leaves it signaled, so the next frame's wait passes instead of hanging.
    bool _bFrameFenceArmed = false;
    // Every sampler created through this render's factory, kept alive until
    // device teardown so handles are released before the device dies (some
    // owners may outlive the device via static destruction).
    std::vector<std::weak_ptr<VulkanSampler>> _trackedSamplers;

    VkQueryPool               _frameGpuTimestampQueryPool  = VK_NULL_HANDLE;
    float                     _gpuTimestampPeriodNs        = 0.0f;
    float                     _lastCompletedFrameGpuTimeMs = 0.0f;
    bool                      _bFrameGpuTimingSupported    = false;
    std::vector<uint8_t>      _frameGpuTimingValid;

    /// Which slot of the GPU-timing query ring the current frame owns. Indexed
    /// by the recorded-frame generation, so the readback at the start of frame
    /// N+1 reads what frame N wrote without asking any window which frame it is
    /// on (it used to ask the primary surface's acquire-slot counter, which is
    /// a swapchain detail and only ever 0 while one frame is in flight).
    [[nodiscard]] uint32_t frameTimingSlot() const
    {
        return static_cast<uint32_t>(_frameIndex % kFramesInFlight);
    }


  public:
    /// Extensions every startup window needs at instance level (unioned: there
    /// is no single window whose requirements the instance may satisfy alone).
    Delegate<std::vector<DeviceFeature>()> onGetRequiredInstanceExtensions;

  public:

    VulkanRender() = default;
    ~VulkanRender(); // Need definition in .cpp to properly destroy unique_ptr<VulkanDescriptor>

    bool init(const ya::RenderCreateInfo& ci) override
    {
        IRender::init(ci);
        YA_PROFILE_FUNCTION_LOG();
        _disabledGraphicsCards = ci.disabledGraphicsCards;

        bool success = initInternal(ci);
        YA_CORE_ASSERT(success, "Failed to initialize Vulkan render!");

        _resourceFactory = std::make_unique<VulkanRenderResourceFactory>(this);

        return true;
    }
    void destroy() override
    {
        destroyInternal();
    }

    void setShaderStorage(std::shared_ptr<ShaderStorage> shaderStorage) override { _shaderStorage = std::move(shaderStorage); }
    std::shared_ptr<ShaderStorage> getShaderStorage() override { return _shaderStorage; }

    /// The physical device this renderer picked. Public accessor so a host that
    /// only wants to label a window does not have to reach into
    /// `_selectedDeviceInfo` through a backend downcast.
    [[nodiscard]] std::string getDeviceName() const override { return _selectedDeviceInfo.deviceName; }

    void trackSampler(const std::shared_ptr<VulkanSampler>& sampler) { _trackedSamplers.emplace_back(sampler); }
    void releaseTrackedSamplers();

    SurfaceId createSurfaceContext(INativeWindow& window, const SwapchainCreateInfo& swapchainCI) override;
    [[nodiscard]] IRenderSurfaceContext* findSurface(SurfaceId id) const override;
    [[nodiscard]] IRenderSurfaceContext* findSurface(INativeWindow& window) const override;
    [[nodiscard]] SurfaceId              findSurfaceId(INativeWindow& window) const override;
    [[nodiscard]] SurfaceId              findSurfaceId(const IRenderSurfaceContext& surface) const override;
    bool                                 destroySurfaceContext(SurfaceId id) override;

    /// Frame bookkeeping for one recorded frame (see IRender::beginRecordedFrame).
    /// Was `onPrimaryPresentFenceWaited`, driven from the primary surface's
    /// begin() -- which made "how many frames have we rendered" a property of
    /// one particular window.
    void beginRecordedFrame() override;
    [[nodiscard]] uint64_t recordedFrameIndex() const override { return _frameIndex; }
    [[nodiscard]] uint32_t framesInFlight() const override { return kFramesInFlight; }

    bool submitFrame(const std::vector<void*>& cmdBufs,
                     const std::vector<void*>& waitSemaphores,
                     const std::vector<void*>& signalSemaphores) override;

    const RenderCapabilities& getCapabilities() const override { return _capabilities; }
    uint32_t getUniformBufferOffsetAlignment() const override
    {
        const auto alignment = _physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
        return alignment > 0 ? static_cast<uint32_t>(alignment) : 1u;
    }
    bool supportsGeometryShader() const override { return _capabilities.geometryShader; }

    void allocateCommandBuffers(uint32_t count, std::vector<std::shared_ptr<ICommandBuffer>>& outBuffers) override;

    void submitToQueue(
        const std::vector<void*>& cmdBufs,
        const std::vector<void*>& waitSemaphores,
        const std::vector<void*>& signalSemaphores,
        void*                     fence = nullptr) override;

    float getLastCompletedFrameGpuTimeMs() const override { return _lastCompletedFrameGpuTimeMs; }

    void beginFrameGpuTiming(ICommandBuffer* commandBuffer) override;
    void endFrameGpuTiming(ICommandBuffer* commandBuffer) override;

    void* createSemaphore(const char* debugName = nullptr) override;
    void  destroySemaphore(void* semaphore) override;
    void  queueBeginLabel(const char* labelName, const float* colorRGBA = nullptr) override;
    void  queueEndLabel() override;

    IRenderResourceFactory*  getResourceFactory() override { return _resourceFactory.get(); }
    TextureFormatSupportInfo queryTextureFormatSupport(EFormat::T format) const override;
    bool                     isTextureFormatSupported(EFormat::T format, EImageUsage::T usage) const override;
    bool                     isImageFormatSupported(EFormat::T format,
                                                    EImageUsage::T usage,
                                                    EImageCreateFlag::T flags = EImageCreateFlag::None,
                                                    ESampleCount::T samples = ESampleCount::Sample_1) const override;
    bool                     supportsMipGeneration(EFormat::T format) const override;

  private:
    void terminate()
    {
        destroy();
        std::exit(-1);
    }

    bool initInternal(const RenderCreateInfo& ci)
    {
        initStartupSurfaces(ci);
        if (!hasStartupSurfaces()) {
            return false;
        }

        createInstance();

        createStartupSurfaceHandles();

        //  find a suitable physical device
        findPhysicalDevice();
        if (m_PhysicalDevice == VK_NULL_HANDLE) {
            terminate();
        }

        if (m_EnableValidationLayers && bSupportDebugUtils) {
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
        if (m_EnableValidationLayers && bSupportDebugUtils) {
            _debugUtils->initDeviceLevel();
        }

        if (!createCommandPool()) {
            terminate();
        }
        if (!createFrameFences()) {
            terminate();
        }
        createPipelineCache();
        if (!createStartupSurfaces()) {
            terminate();
        }
        createFrameGpuTimingResources();
        return true;
    }

    // Defined in VulkanRender.cpp. Kept out of the header: the body calls
    // vmaDestroyAllocator, and an inline body here would make every TU that
    // includes VulkanRender.h reference the VMA symbol (LNK2001 across DLLs).
    void destroyInternal();


  public:

    [[nodiscard]] uint32_t         getApiVersion() const { return apiVersion; }
    [[nodiscard]] VkInstance       getInstance() const { return _instance; }
    [[nodiscard]] VkDevice         getDevice() const { return m_LogicalDevice; }
    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_PhysicalDevice; }
    [[nodiscard]] VmaAllocator     getVmaAllocator() const { return _vmaAllocator; }

    [[nodiscard]] VkPipelineCache getPipelineCache() const { return _pipelineCache; }

    [[nodiscard]] bool                      isGraphicsPresentSameQueueFamily() const { return _graphicsQueueFamily.queueFamilyIndex == _presentQueueFamily.queueFamilyIndex; }
    [[nodiscard]] const QueueFamilyIndices& getGraphicsQueueFamilyInfo() const { return _graphicsQueueFamily; }
    [[nodiscard]] const QueueFamilyIndices& getPresentQueueFamilyInfo() const { return _presentQueueFamily; }

    std::vector<VulkanQueue>& getGraphicsQueues() { return _graphicsQueues; }
    std::vector<VulkanQueue>& getPresentQueues() { return _presentQueues; }

    [[nodiscard]] VulkanDebugUtils* getDebugUtils() const { return _debugUtils.get(); }

    void setDebugObjectName(VkObjectType objectType, void* objectHandle, const std::string& name)
    {
        if (getDebugUtils()) {
            getDebugUtils()->setObjectName(objectType, (uint64_t)objectHandle, name.c_str());
        }
    }

    void setDebugObjectSummary(VkObjectType objectType, void* objectHandle, std::string summary)
    {
        if (getDebugUtils()) {
            getDebugUtils()->setObjectSummary(objectType, reinterpret_cast<uint64_t>(objectHandle), std::move(summary));
        }
    }

    [[nodiscard]] int32_t getMemoryIndex(VkMemoryPropertyFlags properties, uint32_t memoryTypeBits) const;

    std::unique_ptr<VulkanCommandPool>::pointer getGraphicsCommandPool() const { return _graphicsCommandPool.get(); }
    const ::VkAllocationCallbacks*              getAllocator();


    void waitIdle() override { VK_CALL(vkDeviceWaitIdle(m_LogicalDevice)); }

    // IRender interface: isolated commands
    ICommandBuffer* beginIsolateCommands(const std::string& context = "") override
    {
        VkCommandBuffer vkCmdBuf = VK_NULL_HANDLE;
        _graphicsCommandPool->allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, vkCmdBuf);
        const std::string debugName = "IsolatedCommandBuffer_" + context;
        setDebugObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, vkCmdBuf, debugName);
        setDebugObjectSummary(VK_OBJECT_TYPE_COMMAND_BUFFER,
                              vkCmdBuf,
                              std::format("isolated command buffer context='{}' queue=graphics oneTimeSubmit=true", context));
        VulkanCommandPool::begin(vkCmdBuf, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        // Create a temporary VulkanCommandBuffer wrapper
        // Note: This is managed manually and will be deleted in endIsolateCommands
        auto* cmdBuf = new VulkanCommandBuffer(this, vkCmdBuf);
        cmdBuf->setDebugName(debugName);
        return cmdBuf;
    }

    void endIsolateCommands(ICommandBuffer* commandBuffer) override
    {
        auto vkCmdBuf = commandBuffer->getHandleAs<VkCommandBuffer>();

        VulkanCommandPool::end(vkCmdBuf);
        getGraphicsQueues()[0].submit({vkCmdBuf});
        getGraphicsQueues()[0].waitIdle();
        vkFreeCommandBuffers(m_LogicalDevice, _graphicsCommandPool->_handle, 1, &vkCmdBuf);

        // Delete the wrapper
        delete commandBuffer;
    }

    IDescriptorSetHelper* getDescriptorHelper() override;

  public:
    void allocateCommandBuffers(uint32_t size, std::vector<::VkCommandBuffer>& outCommandBuffers);


  private:

    /// Build the startup window list (the surfaces the device must be able to
    /// present to) and the instance-level requirements they share.
    void initStartupSurfaces(const RenderCreateInfo& ci);
    void createInstance();
    void findPhysicalDevice();

    /// One VkSurfaceKHR per startup window, before the device is picked: present
    /// support is what the queue plan is derived from.
    void createStartupSurfaceHandles();


    bool createLogicDevice(uint32_t graphicsQueueCount, uint32_t presentQueueCount);
    void initExtensionFunctions();
    bool createCommandPool();

    /// One signaled fence per flight slot. Signaled rather than not, so the
    /// first frame's wait passes without a "has any frame run yet" special case.
    bool createFrameFences();

    void createPipelineCache();
    // void createDepthResources();

    bool isDeviceSuitable(const std::set<ya::DeviceFeature>& extensions,
                          const std::set<ya::DeviceFeature>& layers,
                          std::vector<const char*>&          extensionNames,
                          std::vector<const char*>&          layerNames);
    bool isInstanceSuitable(const std::set<ya::DeviceFeature>& extensions,
                            const std::set<ya::DeviceFeature>& layers,
                            std::vector<const char*>&          extensionNames,
                            std::vector<const char*>&          layerNames);


    bool isFeatureSupported(
        std::string_view                          contextStr,
        const std::vector<VkExtensionProperties>& availableExtensions,
        const std::vector<VkLayerProperties>&     availableLayers,
        const std::vector<ya::DeviceFeature>&     requestExtensions,
        const std::vector<ya::DeviceFeature>&     requestLayers,
        std::vector<const char*>&                 outExtensionNames,
        std::vector<const char*>&                 outLayerNames,
        bool                                      bDebug = false);

    /// Register one surface context per startup window (swapchain + sync),
    /// after the device and its queues exist.
    bool createStartupSurfaces();
    void createFrameGpuTimingResources();
    void releaseFrameGpuTimingResources();
    void updateCompletedFrameGpuTiming();
};


} // namespace ya

#undef panic

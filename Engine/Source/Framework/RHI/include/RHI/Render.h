#pragma once

#include "Core/Base.h"
#include "RHI/RenderDefines.h"
#include "RHI/Core/RenderSurfaceContext.h"

#include <memory>
#include <string>

namespace ya
{

// Forward declarations
struct ICommandBuffer;
struct ISwapchain;
struct IDescriptorSetHelper;
struct IRenderResourceFactory;
struct RenderTargetCreateInfo;
struct INativeWindow;
struct ShaderStorage;


enum class ERenderObject : uint32_t
{
    DeviceMemory,
    Image,
    ImageView,
    // Add more as needed
};

struct TextureFormatSupportInfo
{
    bool sampled            = false;
    bool sampledTransferDst = false;
    bool linearSampled      = false;
};

struct RenderCapabilities
{
    bool geometryShader      = false;
    bool computeShader       = false;
    bool storageBuffer       = false;
    bool drawIndirect             = false;
    bool drawIndexedIndirect      = false;
    bool drawIndexedIndirectCount = false;
    bool meshShader          = false;
    bool taskShader          = false;
    bool dynamicRendering    = false;
    bool portabilitySubset   = false;
    /// CULL_MODE as a dynamic state: core since Vulkan 1.3; on older devices
    /// requires the enabled VK_EXT_extended_dynamic_state extension. False
    /// means callers must bake the cull mode into the pipeline statically.
    bool dynamicCullMode     = false;
};

struct YA_RHI_API IRender : public plat_base<IRender>
{
    RenderCreateInfo _ci;
    ERenderAPI::T    _renderAPI = ERenderAPI::None;

    IRender() = default;
    virtual ~IRender() { YA_CORE_TRACE("IRender::~IRender()"); }

    // Delete copy operations
    IRender(const IRender&)            = delete;
    IRender& operator=(const IRender&) = delete;

    // Default move operations
    IRender(IRender&&)            = default;
    IRender& operator=(IRender&&) = default;

    static IRender* create(const RenderCreateInfo& ci);

    virtual bool init(const RenderCreateInfo& ci)
    {
        YA_CORE_TRACE("IRender::init()");
        _ci = ci;
        return true;
    }
    virtual void destroy() = 0;

    /// Shader compile/cache service consumed by the backend when building
    /// pipelines (created and injected by the render-runtime layer).
    virtual void                                 setShaderStorage(std::shared_ptr<ShaderStorage> shaderStorage) = 0;
    [[nodiscard]] virtual std::shared_ptr<ShaderStorage> getShaderStorage()                                   = 0;

    /// Bootstrap present surface used to pick the physical device.
    /// Not the viewport. World rendering targets offscreen RenderTextures;
    /// this surface only supplies swapchain images at present/compose time.
    /// Extra windows use `createSurfaceContext`.
    [[nodiscard]] virtual IRenderSurfaceContext* getPrimarySurfaceContext() const { return nullptr; }

    /// Convenience for the bootstrap present surface. Do not use as viewport
    /// extent, world format, or recording-flight index.
    [[nodiscard]] ISwapchain* primarySwapchain() const
    {
        auto* surface = getPrimarySurfaceContext();
        return surface ? surface->getSwapchain() : nullptr;
    }

    [[nodiscard]] INativeWindow* primaryWindow() const
    {
        auto* surface = getPrimarySurfaceContext();
        return surface ? surface->getNativeWindow() : nullptr;
    }

    /// Frame-level bookkeeping for ONE recorded frame.
    ///
    /// The application calls this once per frame it records, after every
    /// presentation surface taking part in that frame has waited its own
    /// in-flight fences (`acquirePresentFrame` is where that wait happens) and
    /// before the frame is recorded. It reads back the previous frame's GPU
    /// timestamps, advances the frame generation, and flushes the deferred
    /// deletion queue for the resources the GPU can no longer be using.
    ///
    /// Deliberately NOT a surface property: no window is "the" window whose
    /// present advances the frame, so a frame that presents two windows
    /// advances the generation once, and a frame that presents none (every
    /// surface minimized, or no window) still retires what is safe.
    virtual void beginRecordedFrame() {}

    /// Monotonic generation of `beginRecordedFrame()`, starting at 0 for the
    /// first frame. Deferred retirement and any per-frame ring key off this
    /// number, never off a swapchain's image index.
    [[nodiscard]] virtual uint64_t recordedFrameIndex() const { return 0; }

    /// How many frames this device keeps in flight (see `kFramesInFlight`).
    /// Which slot of a per-frame ring a recording may use is bounded by this,
    /// not by any window.
    [[nodiscard]] virtual uint32_t framesInFlight() const { return kFramesInFlight; }

    /// Extra presentation surface sharing this device. Does not create a
    /// second backend. Returns null when the backend cannot present to `window`.
    /// Destroy the context before `destroy()` on this device.
    [[nodiscard]] virtual std::unique_ptr<IRenderSurfaceContext> createSurfaceContext(INativeWindow& window);

    [[nodiscard]] ERenderAPI::T getAPI() const { return _renderAPI; }

    /// Human-readable name of the device this backend selected, or empty when
    /// the API has none. A window/tooling label: callers that only want to
    /// print which GPU is running should not have to downcast to a concrete
    /// backend to read its device info.
    [[nodiscard]] virtual std::string getDeviceName() const { return {}; }

    /**
     * @brief Allocate command buffers (returns generic ICommandBuffer interface)
     */
    virtual void allocateCommandBuffers(uint32_t count, std::vector<std::shared_ptr<ICommandBuffer>>& outBuffers) = 0;

    /**
     * @brief Wait for the device to become idle
     */
    virtual void waitIdle() = 0;

    struct RAIICommandBuffer
    {
        IRender*        _render;
        ICommandBuffer* _cmdBuf;
        ICommandBuffer* operator->() const { return _cmdBuf; }

        ~RAIICommandBuffer() { _render->endIsolateCommands(_cmdBuf); }
    };

    /**
     * @brief Begin recording commands for isolated/immediate execution
     * @return Command buffer for recording
     */
    virtual ICommandBuffer* beginIsolateCommands(const std::string& context = "") = 0;

    /**
     * @brief End and submit isolated commands
     * @param commandBuffer The command buffer to submit
     */
    virtual void endIsolateCommands(ICommandBuffer* commandBuffer) = 0;

    /**
     * @brief Get the descriptor set helper for updating descriptor sets
     * @return Pointer to descriptor set helper interface
     */
    virtual IDescriptorSetHelper* getDescriptorHelper() = 0;

    virtual IRenderResourceFactory* getResourceFactory() { return nullptr; }

    virtual TextureFormatSupportInfo queryTextureFormatSupport(EFormat::T format) const
    {
        (void)format;
        return {};
    }

    virtual bool isTextureFormatSupported(EFormat::T format, EImageUsage::T usage) const
    {
        const auto support = queryTextureFormatSupport(format);
        if ((usage & static_cast<EImageUsage::T>(EImageUsage::TransferDst)) != 0) {
            return support.sampledTransferDst;
        }
        if ((usage & static_cast<EImageUsage::T>(EImageUsage::Sampled)) != 0) {
            return support.sampled;
        }
        return false;
    }

    virtual bool isImageFormatSupported(EFormat::T format,
                                        EImageUsage::T usage,
                                        EImageCreateFlag::T flags = EImageCreateFlag::None,
                                        ESampleCount::T samples = ESampleCount::Sample_1) const
    {
        (void)format;
        (void)usage;
        (void)flags;
        (void)samples;
        return false;
    }

    virtual bool supportsMipGeneration(EFormat::T format) const
    {
        (void)format;
        return false;
    }

    virtual const RenderCapabilities& getCapabilities() const
    {
        static const RenderCapabilities caps{};
        return caps;
    }

    /**
     * Minimum offset alignment for dynamic uniform-buffer descriptors.
     *
     * Frame upload allocators use this value when several uniform slices share
     * one backing buffer. Backends must report their native limit; the default
     * keeps lightweight/mock render implementations valid until they opt in.
     */
    virtual uint32_t getUniformBufferOffsetAlignment() const { return 1; }

    virtual bool supportsGeometryShader() const { return getCapabilities().geometryShader; }
    virtual bool supportsMeshShader() const { return getCapabilities().meshShader; }
    virtual bool supportsComputeIndirect() const
    {
        const auto& caps = getCapabilities();
        return caps.computeShader && caps.storageBuffer && caps.drawIndirect;
    }

    /**
     * @brief Submit command buffers to graphics queue with synchronization
     * @param cmdBufs Command buffers to submit
     * @param waitSemaphores Semaphores to wait on before execution
     * @param signalSemaphores Semaphores to signal after execution
     * @param fence Optional fence to signal when complete
     */
    virtual void submitToQueue(
        const std::vector<void*>& cmdBufs,
        const std::vector<void*>& waitSemaphores,
        const std::vector<void*>& signalSemaphores,
        void*                     fence = nullptr) = 0;

    virtual void beginFrameGpuTiming(ICommandBuffer* commandBuffer)
    {
        (void)commandBuffer;
    }

    virtual void endFrameGpuTiming(ICommandBuffer* commandBuffer)
    {
        (void)commandBuffer;
    }

    virtual float getLastCompletedFrameGpuTimeMs() const
    {
        return 0.0f;
    }

    /**
     * @brief Create a semaphore (for App-managed synchronization)
     * @param debugName Optional debug name for the semaphore
     * @return Opaque handle to semaphore
     */
    virtual void* createSemaphore(const char* debugName = nullptr) = 0;

    /**
     * @brief Destroy a semaphore created by createSemaphore
     * @param semaphore Semaphore to destroy
     */
    virtual void destroySemaphore(void* semaphore) = 0;

    virtual void queueBeginLabel(const char* labelName, const float* colorRGBA = nullptr)
    {
        (void)labelName;
        (void)colorRGBA;
    }

    virtual void queueEndLabel() {}
};

} // namespace ya

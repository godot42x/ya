#pragma once

#include "Core/Base.h"
#include "RenderDefines.h"

namespace ya
{

// Forward declarations
struct ICommandBuffer;
struct ISwapchain;
struct IDescriptorSetHelper;
struct ITextureFactory; // 添加TextureFactory前向声明


enum class ERenderObject : uint32_t
{
    DeviceMemory,
    Image,
    ImageView,
    // Add more as needed
};

struct IRender : public plat_base<IRender>
{
    RenderCreateInfo _ci;
    ERenderAPI::T    _renderAPI = ERenderAPI::None;

    IRender() = default;
    virtual ~IRender() { YA_CORE_TRACE("IRender::~IRender()"); }

    // Delete copy operations
    IRender(const IRender &)            = delete;
    IRender &operator=(const IRender &) = delete;

    // Default move operations
    IRender(IRender &&)            = default;
    IRender &operator=(IRender &&) = default;

    static IRender *create(const RenderCreateInfo &ci);

    virtual bool init(const RenderCreateInfo &ci)
    {
        YA_CORE_TRACE("IRender::init()");
        _ci = ci;
        return true;
    }
    virtual void destroy() = 0;

    virtual bool begin(int32_t *imageIndex)                                  = 0;
    virtual bool end(int32_t imageIndex, std::vector<void *> CommandBuffers) = 0;

    [[nodiscard]] ERenderAPI::T getAPI() const { return _renderAPI; }

    /**
     * @brief Get window size from the render's window provider
     */
    virtual void getWindowSize(int &width, int &height) const = 0;

    /**
     * @brief Set VSync on the swapchain
     */
    virtual void setVsync(bool enabled) = 0;

    /**
     * @brief Get the swapchain width
     */
    virtual uint32_t getSwapchainWidth() const = 0;

    /**
     * @brief Get the swapchain height
     */
    virtual uint32_t getSwapchainHeight() const = 0;

    /**
     * @brief Get the number of swapchain images
     */
    virtual uint32_t getSwapchainImageCount() const = 0;

    /**
     * @brief Allocate command buffers (returns generic ICommandBuffer interface)
     */
    virtual void allocateCommandBuffers(uint32_t count, std::vector<std::shared_ptr<ICommandBuffer>> &outBuffers) = 0;

    /**
     * @brief Wait for the device to become idle
     */
    virtual void waitIdle() = 0;

    /**
     * @brief Get the native window handle as a specific type
     * @tparam T The window type (e.g., SDL_Window*)
     */
    template <typename T>
    T getNativeWindow() const
    {
        return static_cast<T>(getNativeWindowHandle());
    }

    /**
     * @brief Begin recording commands for isolated/immediate execution
     * @return Command buffer for recording
     */
    virtual ICommandBuffer *beginIsolateCommands(const std::string& context = "") = 0;

    /**
     * @brief End and submit isolated commands
     * @param commandBuffer The command buffer to submit
     */
    virtual void endIsolateCommands(ICommandBuffer *commandBuffer) = 0;

    /**
     * @brief Get the swapchain interface
     */
    virtual ISwapchain *getSwapchain() = 0;

    /**
     * @brief Get the descriptor set helper for updating descriptor sets
     * @return Pointer to descriptor set helper interface
     */
    virtual IDescriptorSetHelper *getDescriptorHelper() = 0;

    /**
     * @brief Get the texture factory for creating image resources
     * @return Pointer to texture factory interface
     */
    virtual ITextureFactory *getTextureFactory() = 0;

    /**
     * @brief Submit command buffers to graphics queue with synchronization
     * @param cmdBufs Command buffers to submit
     * @param waitSemaphores Semaphores to wait on before execution
     * @param signalSemaphores Semaphores to signal after execution
     * @param fence Optional fence to signal when complete
     */
    virtual void submitToQueue(
        const std::vector<void *> &cmdBufs,
        const std::vector<void *> &waitSemaphores,
        const std::vector<void *> &signalSemaphores,
        void                      *fence = nullptr) = 0;

    /**
     * @brief Present swapchain image
     * @param imageIndex Swapchain image index to present
     * @param waitSemaphores Semaphores to wait on before presenting
     * @return VK_SUCCESS or error code
     */
    virtual int presentImage(int32_t imageIndex, const std::vector<void *> &waitSemaphores) = 0;

    /**
     * @brief Get current frame's image available semaphore
     */
    virtual void *getCurrentImageAvailableSemaphore() = 0;

    /**
     * @brief Get current frame's fence
     */
    virtual void *getCurrentFrameFence() = 0;

    /**
     * @brief Get current frame index
     */
    virtual uint32_t getCurrentFrameIndex() const = 0;

    /**
     * @brief Get render finished semaphore for given swapchain image index
     * @param imageIndex Swapchain image index
     * @return Semaphore that will be signaled when rendering to this image completes
     */
    virtual void *getRenderFinishedSemaphore(uint32_t imageIndex) = 0;

    /**
     * @brief Create a semaphore (for App-managed synchronization)
     * @param debugName Optional debug name for the semaphore
     * @return Opaque handle to semaphore
     */
    virtual void *createSemaphore(const char *debugName = nullptr) = 0;

    /**
     * @brief Destroy a semaphore created by createSemaphore
     * @param semaphore Semaphore to destroy
     */
    virtual void destroySemaphore(void *semaphore) = 0;

    /**
     * @brief Advance to next frame (increment frame index)
     */
    virtual void advanceFrame() = 0;

  protected:
    /**
     * @brief Get the native window handle (backend-specific)
     */
    virtual void *getNativeWindowHandle() const = 0;
};

} // namespace ya

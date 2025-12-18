#pragma once

#include "Core/Base.h"
#include "Core/PlatBase.h"
#include "RenderDefines.h"

namespace ya
{

// Forward declarations
struct ICommandBuffer;
struct ISwapchain;
struct IDescriptorSetHelper;


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
    virtual ICommandBuffer *beginIsolateCommands() = 0;

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

  protected:
    /**
     * @brief Get the native window handle (backend-specific)
     */
    virtual void *getNativeWindowHandle() const = 0;
};

} // namespace ya

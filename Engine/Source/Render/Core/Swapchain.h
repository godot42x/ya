
#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Render/Core/PlatBase.h"
#include "Render/RenderDefines.h"


namespace ya
{

// Forward declarations
struct IRender;

/**
 * @brief Abstract interface for swap chains
 * Swapchain manages a queue of images for presentation to the screen
 *
 * Different APIs:
 * - Vulkan: VkSwapchainKHR
 * - DirectX 12: IDXGISwapChain
 * - wgpu: wgpu::SwapChain
 * - Metal: CAMetalLayer (similar concept)
 */
struct ISwapchain : public ya::plat_base<ISwapchain>
{
    ISwapchain()          = default;
    virtual ~ISwapchain() = default;

    // Delete copy operations (interface should not be copied)
    ISwapchain(const ISwapchain &)            = delete;
    ISwapchain &operator=(const ISwapchain &) = delete;

    // Default move operations
    ISwapchain(ISwapchain &&)            = default;
    ISwapchain &operator=(ISwapchain &&) = default;

    /**
     * @brief Get the native handle (e.g., VkSwapchainKHR for Vulkan)
     */
    virtual void *getHandle() const = 0;

    /**
     * @brief Get the handle as a specific type
     * @tparam T The type to cast to (e.g., VkSwapchainKHR)
     */
    template <typename T>
    T getHandleAs() const
    {
        return static_cast<T>(getHandle());
    }

    /**
     * @brief Get the extent (width, height) of the swapchain
     */
    virtual Extent2D getExtent() const = 0;

    /**
     * @brief Get the image format of the swapchain
     */
    virtual EFormat::T getFormat() const = 0;

    /**
     * @brief Get the number of images in the swapchain
     */
    virtual uint32_t getImageCount() const = 0;

    [[nodiscard]] virtual uint32_t getCurImageIndex() const = 0;


    // virtual bool acquireNextImage(uint32_t *imageIndex) = 0;
    // virtual bool present(uint32_t imageIndex) = 0;
    virtual bool recreate(const SwapchainCreateInfo &ci) = 0;

    /**
     * @brief Get VSync enabled status
     */
    virtual bool getVsync() const = 0;

    /**
     * @brief Set VSync enabled/disabled
     */
    virtual void                                       setVsync(bool enabled)                      = 0;
    virtual void                                       setPresentMode(EPresentMode::T presentMode) = 0;
    [[nodiscard]] virtual EPresentMode::T              getPresentMode() const                      = 0;
    [[nodiscard]] virtual std::vector<EPresentMode::T> getAvailablePresentModes() const            = 0;

    /**
     * @brief Create a swapchain for the given API
     */
    static stdptr<ISwapchain> create(IRender *render, const SwapchainCreateInfo &createInfo);


    struct DiffInfo
    {
        Extent2D        extent;
        EPresentMode::T presentMode;
    };
    MulticastDelegate<void(const DiffInfo &old, const DiffInfo &now, bool bImageRecreated)> onRecreate;
};

} // namespace ya

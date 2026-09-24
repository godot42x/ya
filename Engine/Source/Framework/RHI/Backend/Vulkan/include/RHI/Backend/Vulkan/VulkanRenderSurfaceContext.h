#pragma once

#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"
#include "RHI/RenderDefines.h"

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace ya
{

struct ICommandBuffer;
struct VulkanRender;

struct YA_RHI_BACKEND_API VulkanRenderSurfaceContext final : IRenderSurfaceContext
{
    VulkanRender*  _render       = nullptr;
    INativeWindow* _window       = nullptr;
    VkSurfaceKHR   _surface      = VK_NULL_HANDLE;
    bool           _bOwnsSurface = true;
    std::string    _debugLabel        = "Surface";

    std::unique_ptr<VulkanSwapChain> _swapChain;

    /// This surface's acquire ring depth == the device's frames in flight: the
    /// device owns how many frames may overlap, a window does not.
    static constexpr uint32_t flightFrameSize = kFramesInFlight;
    /// Empty submit after present, so recreate/destroy can wait THIS
    /// surface's last present without `vkQueueWaitIdle` on the shared queue.
    static constexpr uint32_t presentCompleteFenceCount = 2;
    uint32_t                  currentFrameIdx = 0;
    uint32_t                  presentCompleteFenceIdx = 0;
    std::vector<VkSemaphore>  frameImageAvailableSemaphores;
    std::vector<VkFence>      presentCompleteFences;
    std::vector<VkSemaphore>  imageSubmittedSignalSemaphores;
    std::shared_ptr<ICommandBuffer> _scratchPresentCmd;

    ~VulkanRenderSurfaceContext() override;

    /// Window registered after device creation: create and own a
    /// `VkSurfaceKHR`, then swapchain + sync.
    [[nodiscard]] bool init(VulkanRender* render, INativeWindow& window, const SwapchainCreateInfo& swapchainCI);

    /// Startup window: the device already created `surface` (before device
    /// pick, because present support is what the queue plan came from), so the
    /// context presents through it without owning it.
    [[nodiscard]] bool adoptStartupSurface(VulkanRender*           render,
                                           INativeWindow&          window,
                                           VkSurfaceKHR            surface,
                                           const SwapchainCreateInfo& swapchainCI);

    void waitInFlight() override;
    void waitAllPresentCompleteFences();
    void signalPresentComplete();
    [[nodiscard]] bool prepareSwapchainForAcquire();
    [[nodiscard]] bool acquire(int32_t* imageIndex);
    void               advanceFrame() { currentFrameIdx = (currentFrameIdx + 1) % flightFrameSize; }

    [[nodiscard]] INativeWindow* getNativeWindow() const override { return _window; }
    [[nodiscard]] ISwapchain*    getSwapchain() const override { return _swapChain.get(); }
    bool buildPresentationImages(
        IRenderResourceFactory& factory,
        const char* labelPrefix,
        std::vector<std::shared_ptr<RenderTexture>>& outImages) override;
    [[nodiscard]] bool           isPresentable() const override;

    bool begin(int32_t* imageIndex) override;
    bool present(int32_t imageIndex) override;
    [[nodiscard]] ICommandBuffer* presentFallbackCommand(uint32_t imageIndex) override;
    void requestRecreate() override;

    [[nodiscard]] void*    getCurrentImageAvailableSemaphore() override;
    [[nodiscard]] void*    getRenderFinishedSemaphore(uint32_t imageIndex) override;

  private:
    [[nodiscard]] bool queryPresentSupport() const;
    [[nodiscard]] bool createSwapchainAndSync(const SwapchainCreateInfo& swapchainCI, bool bRequireImages);
    void               createSyncResources(uint32_t swapchainImageCount);
    void               releaseSyncResources();
    void               recordPresentBarrier(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;
};

} // namespace ya

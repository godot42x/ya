#pragma once

#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"

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
    bool           _bOwnsSurface      = true;
    bool           _bDeviceFrameOwner = false;
    const char*    _debugLabel        = "Surface";

    std::unique_ptr<VulkanSwapChain> _swapChain;

    static constexpr uint32_t flightFrameSize = 1;
    /// Empty submit after present, so recreate/destroy can wait THIS
    /// surface's last present without `vkQueueWaitIdle` on the shared queue.
    static constexpr uint32_t presentCompleteFenceCount = 2;
    uint32_t                  currentFrameIdx = 0;
    uint32_t                  presentCompleteFenceIdx = 0;
    std::vector<VkSemaphore>  frameImageAvailableSemaphores;
    std::vector<VkFence>      frameFences;
    std::vector<VkFence>      presentCompleteFences;
    std::vector<VkSemaphore>  imageSubmittedSignalSemaphores;
    std::shared_ptr<ICommandBuffer> _scratchPresentCmd;

    ~VulkanRenderSurfaceContext() override;

    /// Extra window: create and own a `VkSurfaceKHR`, then swapchain + sync.
    [[nodiscard]] bool init(VulkanRender* render, INativeWindow& window, const SwapchainCreateInfo& swapchainCI);

    /// Primary window: device pick already created `surface`. Context does not destroy it.
    [[nodiscard]] bool attachExistingSurface(VulkanRender*           render,
                                             INativeWindow&          window,
                                             VkSurfaceKHR            surface,
                                             const SwapchainCreateInfo& swapchainCI);

    void waitInFlight() override;
    void waitInFlightFence();
    void waitAllGraphicsFences();
    void waitAllPresentCompleteFences();
    void resetInFlightFence();
    void signalPresentComplete();
    void resignalCurrentFence();
    [[nodiscard]] bool prepareSwapchainForAcquire();
    [[nodiscard]] bool acquire(int32_t* imageIndex);
    [[nodiscard]] bool submitAndPresent(int32_t imageIndex, std::vector<void*> commandBuffers, bool bScratchIfEmpty);
    void               advanceFrame() { currentFrameIdx = (currentFrameIdx + 1) % flightFrameSize; }

    [[nodiscard]] INativeWindow* getNativeWindow() const override { return _window; }
    [[nodiscard]] ISwapchain*    getSwapchain() const override { return _swapChain.get(); }
    bool buildPresentationImages(
        IRenderResourceFactory& factory,
        const char* labelPrefix,
        std::vector<std::shared_ptr<RenderTexture>>& outImages) override;
    [[nodiscard]] bool           isPresentable() const override;

    bool begin(int32_t* imageIndex) override;
    bool end(int32_t imageIndex, std::vector<void*> commandBuffers) override;
    void requestRecreate() override;

    [[nodiscard]] void*    getCurrentImageAvailableSemaphore() override;
    [[nodiscard]] void*    getCurrentFrameFence() override;
    [[nodiscard]] void*    getRenderFinishedSemaphore(uint32_t imageIndex) override;
    [[nodiscard]] uint32_t getCurrentFrameIndex() const override { return currentFrameIdx; }

  private:
    [[nodiscard]] bool queryPresentSupport() const;
    [[nodiscard]] bool createSwapchainAndSync(const SwapchainCreateInfo& swapchainCI, bool bRequireImages);
    void               createSyncResources(uint32_t swapchainImageCount);
    void               releaseSyncResources();
    void               recordPresentBarrier(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;
};

} // namespace ya

#pragma once

#include "RHI/Core/RenderSurfaceContext.h"
#include "VulkanSwapChain.h"

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace ya
{

struct ICommandBuffer;
struct VulkanRender;

struct YA_RHI_BACKEND_API VulkanRenderSurfaceContext final : IRenderSurfaceContext
{
    VulkanRender*  _render  = nullptr;
    INativeWindow* _window  = nullptr;
    VkSurfaceKHR   _surface = VK_NULL_HANDLE;

    std::unique_ptr<VulkanSwapChain> _swapChain;

    static constexpr uint32_t flightFrameSize = 1;
    uint32_t                  currentFrameIdx = 0;
    std::vector<VkSemaphore>  frameImageAvailableSemaphores;
    std::vector<VkFence>      frameFences;
    std::vector<VkSemaphore>  imageSubmittedSignalSemaphores;
    std::shared_ptr<ICommandBuffer> _scratchPresentCmd;

    ~VulkanRenderSurfaceContext() override;

    [[nodiscard]] bool init(VulkanRender* render, INativeWindow& window, const SwapchainCreateInfo& swapchainCI);

    [[nodiscard]] INativeWindow* getNativeWindow() const override { return _window; }
    [[nodiscard]] ISwapchain*    getSwapchain() override { return _swapChain.get(); }

    bool begin(int32_t* imageIndex) override;
    bool end(int32_t imageIndex, std::vector<void*> commandBuffers) override;

    [[nodiscard]] void*    getCurrentImageAvailableSemaphore() override;
    [[nodiscard]] void*    getCurrentFrameFence() override;
    [[nodiscard]] void*    getRenderFinishedSemaphore(uint32_t imageIndex) override;
    [[nodiscard]] uint32_t getCurrentFrameIndex() const override { return currentFrameIdx; }

  private:
    [[nodiscard]] bool queryPresentSupport() const;
    void               createSyncResources(uint32_t swapchainImageCount);
    void               releaseSyncResources();
    void               recordPresentBarrier(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;
};

} // namespace ya

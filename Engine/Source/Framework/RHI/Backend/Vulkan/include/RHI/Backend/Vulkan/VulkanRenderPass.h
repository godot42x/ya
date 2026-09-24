#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "RHI/Backend/Vulkan/VulkanPipeline.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderPass.h"
#include <vector>
#include <vulkan/vulkan.h>


namespace ya
{
struct VulkanRender;
struct VulkanSwapChain;
struct ISwapchain;


/*
Render Pass主导资源声明
Pipeline 需要兼容/依赖 Render Pass 的资源声明
*/
struct VulkanRenderPass : public ya::IRenderPass
{
  private:
    VulkanRender *_render      = nullptr;
    VkRenderPass  m_renderPass = VK_NULL_HANDLE;

    VkFormat         m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkFormat         m_depthFormat          = VK_FORMAT_UNDEFINED;
    VulkanSwapChain *_swapChain             = nullptr;


  public:
    /// `swapchain` is the surface's present target this pass describes; see
    /// `IRenderPass::create`. Null is accepted because nothing constructs a
    /// render pass today, and the two callers that would read it guard for it.
    VulkanRenderPass(VulkanRender *render, ISwapchain *swapchain);
    ~VulkanRenderPass() override { cleanup(); }

    VulkanRenderPass(const VulkanRenderPass &)            = delete;
    VulkanRenderPass &operator=(const VulkanRenderPass &) = delete;
    VulkanRenderPass(VulkanRenderPass &&)                 = default;
    VulkanRenderPass &operator=(VulkanRenderPass &&)      = default;

    // Initialize the render pass with device and format information
    bool recreate(const ya::RenderPassCreateInfo &ci) override;
    void cleanup();

    // IRenderPass interface
    void       begin(ya::ICommandBuffer            *commandBuffer,
                     IFrameBuffer                  *framebuffer,
                     const Extent2D                &extent,
                     const std::vector<ClearValue> &clearValues) override;
    void       end(ya::ICommandBuffer *commandBuffer) override;
    void      *getHandle() const override { return (void *)(uintptr_t)m_renderPass; }
    EFormat::T getDepthFormat() const override;


    // Vulkan-specific methods
    [[nodiscard]] VkRenderPass getVkHandle() const { return m_renderPass; }
    [[nodiscard]] VkFormat     getVkDepthFormat() const { return m_depthFormat; }


  private:
    // Find supported format from candidates
    VkFormat findSupportedImageFormat(const std::vector<VkFormat> &candidates,
                                      VkImageTiling                tiling,
                                      VkFormatFeatureFlags         features);

    bool createDefaultRenderPass();
};
} // namespace ya

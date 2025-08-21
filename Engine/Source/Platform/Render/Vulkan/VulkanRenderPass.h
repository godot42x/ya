#pragma once


#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Render/RenderManager.h"
#include <vector>
#include <vulkan/vulkan.h>

struct VulkanRender;
struct VulkanSwapChain;


/*
Render Pass主导资源声明
Pipeline 需要兼容/依赖 Render Pass 的资源声明
*/
class VulkanRenderPass
{
  private:
    VulkanRender *_render      = nullptr;
    VkRenderPass  m_renderPass = VK_NULL_HANDLE;

    VkFormat         m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkFormat         m_depthFormat          = VK_FORMAT_UNDEFINED;
    VulkanSwapChain *_swapChain             = nullptr;

    RenderPassCreateInfo _ci;

  public:
    VulkanRenderPass(VulkanRender *render);
    ~VulkanRenderPass() { cleanup(); }

    // Initialize the render pass with device and format information

    bool recreate(const RenderPassCreateInfo &ci);
    void cleanup();


    // Getters
    [[nodiscard]] VkRenderPass getHandle() const { return m_renderPass; }
    [[nodiscard]] VkFormat     getDepthFormat() const { return m_depthFormat; }


    void begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, VkExtent2D extent, const std::vector<VkClearValue> &clearValues);
    void end(VkCommandBuffer commandBuffer);

    [[nodiscard]] RenderPassCreateInfo getCI() const { return _ci; }
    [[nodiscard]] auto                 getAttachmentCount() const { return _ci.attachments.size(); }

    // span is 16 bytes, but can aware of the copy if use not const auto& = getxxx()
    // but performance?
    [[nodiscard]] const std::vector<AttachmentDescription> &getAttachments() const { return _ci.attachments; }


  private:

    // Find supported format from candidates
    VkFormat findSupportedImageFormat(const std::vector<VkFormat> &candidates,
                                      VkImageTiling                tiling,
                                      VkFormatFeatureFlags         features);

    bool createDefaultRenderPass();
};
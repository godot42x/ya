#pragma once


#include "Core/Delegate.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Render/RenderManager.h"
#include <vector>
#include <vulkan/vulkan.h>

struct VulkanRender;


/*
Render Pass主导资源声明
Pipeline 需要兼容/依赖 Render Pass 的资源声明
*/
class VulkanRenderPass
{
  private:
    VulkanRender *_render      = nullptr;
    VkRenderPass  m_renderPass = VK_NULL_HANDLE;

    VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_depthFormat          = VK_FORMAT_UNDEFINED;


    VulkanPipeline m_pipelineManager;

    RenderPassCreateInfo _ci;

  public:
    VulkanRenderPass(VulkanRender *render);
    ~VulkanRenderPass() { cleanup(); }

    // Initialize the render pass with device and format information

    // Create the render pass with custom configuration
    bool create(const RenderPassCreateInfo &ci);

    // Cleanup resources
    void cleanup();

    // Recreate render pass and framebuffers (for swap chain recreation)
    void recreate(const RenderPassCreateInfo &ci);

    // Getters
    VkRenderPass getHandle() const { return m_renderPass; }
    VkFormat     getDepthFormat() const { return m_depthFormat; }

    // Begin render pass
    void beginRenderPass(VkCommandBuffer commandBuffer, uint32_t frameBufferIndex, VkExtent2D extent);

    // End render pass
    void endRenderPass(VkCommandBuffer commandBuffer);


  private:
    // Find suitable depth format
    VkFormat findDepthFormat();

    // Find supported format from candidates
    VkFormat findSupportedImageFormat(const std::vector<VkFormat> &candidates,
                                      VkImageTiling                tiling,
                                      VkFormatFeatureFlags         features);

    bool createDefaultRenderPass();
};
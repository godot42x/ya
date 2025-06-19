#pragma once
// AI GENERATED FILE - by Claude AI


#include <vector>
#include <vulkan/vulkan.h>


/**
 * @brief VulkanRenderPass - Encapsulates Vulkan render pass and framebuffer functionality
 *
 * This class abstracts the render pass creation, framebuffer management, and related operations
 * that were previously part of the VulkanState class. It provides a clean interface for:
 *
 * - Creating render passes with color and depth attachments
 * - Managing framebuffers for swap chain images
 * - Handling render pass begin/end operations
 * - Supporting swap chain recreation
 *
 * Usage Example:
 * ```cpp
 * VulkanRenderPass renderPass;
 * renderPass.initialize(logicalDevice, physicalDevice, swapChainFormat);
 * renderPass.createRenderPass();
 * renderPass.createFramebuffers(swapChainImageViews, depthImageView, swapChainExtent);
 *
 * // In command buffer recording:
 * renderPass.beginRenderPass(commandBuffer, frameIndex, extent);
 * // ... record rendering commands ...
 * renderPass.endRenderPass(commandBuffer);
 * ```
 */
class VulkanRenderPass
{
  public:
    VulkanRenderPass()  = default;
    ~VulkanRenderPass() = default;

    // Initialize the render pass with device and format information
    void initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice, VkFormat swapChainImageFormat);

    // Create the render pass
    void createRenderPass();

    // Create framebuffers for the render pass
    void createFramebuffers(const std::vector<VkImageView> &swapChainImageViews,
                            VkImageView                     depthImageView,
                            VkExtent2D                      swapChainExtent);

    // Cleanup resources
    void cleanup();

    // Recreate render pass and framebuffers (for swap chain recreation)
    void recreate(const std::vector<VkImageView> &swapChainImageViews,
                  VkImageView                     depthImageView,
                  VkExtent2D                      swapChainExtent);

    // Getters
    VkRenderPass                      getRenderPass() const { return m_renderPass; }
    const std::vector<VkFramebuffer> &getFramebuffers() const { return m_framebuffers; }
    VkFormat                          getDepthFormat() const { return m_depthFormat; }

    // Begin render pass
    void beginRenderPass(VkCommandBuffer commandBuffer, uint32_t frameBufferIndex, VkExtent2D extent);

    // End render pass
    void endRenderPass(VkCommandBuffer commandBuffer);

  private:
    // Find suitable depth format
    VkFormat findDepthFormat();

    // Find supported format from candidates
    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates,
                                 VkImageTiling                tiling,
                                 VkFormatFeatureFlags         features);

  private:
    VkDevice         m_logicalDevice        = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice       = VK_NULL_HANDLE;
    VkFormat         m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkFormat         m_depthFormat          = VK_FORMAT_UNDEFINED;

    VkRenderPass               m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    // Render pass configuration
    struct RenderPassConfig
    {
        VkSampleCountFlagBits samples      = VK_SAMPLE_COUNT_1_BIT;
        VkAttachmentLoadOp    colorLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp   colorStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkAttachmentLoadOp    depthLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp   depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    } m_config;
};
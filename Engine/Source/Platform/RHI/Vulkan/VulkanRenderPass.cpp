#include "VulkanRenderPass.h"
#include "Core/Log.h"
#include "VulkanUtils.h"
#include <array>


void VulkanRenderPass::initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice, VkFormat swapChainImageFormat)
{
    m_logicalDevice        = logicalDevice;
    m_physicalDevice       = physicalDevice;
    m_swapChainImageFormat = swapChainImageFormat;
    m_depthFormat          = findDepthFormat();
}

void VulkanRenderPass::createRenderPass()
{
    VkAttachmentDescription colorAttachment{
        .format         = m_swapChainImageFormat,
        .samples        = m_config.samples,
        .loadOp         = m_config.colorLoadOp,
        .storeOp        = m_config.colorStoreOp,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference colorAttachmentRef{
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription depthAttachment{
        .format         = m_depthFormat,
        .samples        = m_config.samples,
        .loadOp         = m_config.depthLoadOp,
        .storeOp        = m_config.depthStoreOp,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depthAttachmentRef{
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass{
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &colorAttachmentRef,
        .pDepthStencilAttachment = &depthAttachmentRef,
    };

    VkSubpassDependency dependency{
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments    = attachments.data(),
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    VkResult result = vkCreateRenderPass(m_logicalDevice, &createInfo, nullptr, &m_renderPass);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create render pass!");

}

void VulkanRenderPass::createFramebuffers(const std::vector<VkImageView> &swapChainImageViews,
                                          VkImageView                     depthImageView,
                                          VkExtent2D                      swapChainExtent)
{
    m_framebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); ++i)
    {
        std::array<VkImageView, 2> attachments = {
            swapChainImageViews[i],
            depthImageView};

        VkFramebufferCreateInfo framebufferInfo = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = m_renderPass,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments    = attachments.data(),
            .width           = swapChainExtent.width,
            .height          = swapChainExtent.height,
            .layers          = 1,
        };

        if (vkCreateFramebuffer(m_logicalDevice, &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
        {
            NE_CORE_ASSERT(false, "Failed to create framebuffer!");
        }
    }
}

void VulkanRenderPass::cleanup()
{
    for (auto framebuffer : m_framebuffers)
    {
        vkDestroyFramebuffer(m_logicalDevice, framebuffer, nullptr);
    }
    m_framebuffers.clear();

    if (m_renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(m_logicalDevice, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

void VulkanRenderPass::recreate(const std::vector<VkImageView> &swapChainImageViews,
                                VkImageView                     depthImageView,
                                VkExtent2D                      swapChainExtent)
{
    // Clean up existing framebuffers
    for (auto framebuffer : m_framebuffers)
    {
        vkDestroyFramebuffer(m_logicalDevice, framebuffer, nullptr);
    }
    m_framebuffers.clear();

    // Recreate framebuffers with new swap chain
    createFramebuffers(swapChainImageViews, depthImageView, swapChainExtent);
}

void VulkanRenderPass::beginRenderPass(VkCommandBuffer commandBuffer, uint32_t frameBufferIndex, VkExtent2D extent)
{
    VkRenderPassBeginInfo renderPassInfo{
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = m_renderPass,
        .framebuffer = m_framebuffers[frameBufferIndex],
        .renderArea  = {
             .offset = {0, 0},
             .extent = extent,
        },
    };

    std::array<VkClearValue, 2> clearValues = {};
    clearValues[0].color                    = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil             = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanRenderPass::endRenderPass(VkCommandBuffer commandBuffer)
{
    vkCmdEndRenderPass(commandBuffer);
}

VkFormat VulkanRenderPass::findDepthFormat()
{
    return findSupportedImageFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat VulkanRenderPass::findSupportedImageFormat(const std::vector<VkFormat> &candidates,
                                                    VkImageTiling                tiling,
                                                    VkFormatFeatureFlags         features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    NE_CORE_ASSERT(false, "Failed to find supported format!");
    return VK_FORMAT_UNDEFINED;
}

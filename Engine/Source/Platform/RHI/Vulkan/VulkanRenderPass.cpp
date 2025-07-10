#include "VulkanRenderPass.h"
#include "Core/Log.h"
#include "VulkanUtils.h"
#include <array>


void VulkanRenderPass::initialize(VkDevice logicalDevice, VkPhysicalDevice physicalDevice, VkFormat swapChainImageFormat, RenderPassCreateInfo renderPassCI)
{
    m_logicalDevice        = logicalDevice;
    m_physicalDevice       = physicalDevice;
    m_swapChainImageFormat = swapChainImageFormat;
    m_depthFormat          = findDepthFormat();
    _ci                    = renderPassCI;
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

void VulkanRenderPass::create()
{
    // Convert abstract configuration to Vulkan-specific values
    std::vector<VkAttachmentDescription> vkAttachments;


    NE_CORE_ASSERT(!_ci.attachments.empty(), "Render pass must have at least one attachment defined!");
    // Convert attachments from config
    for (const auto &attachment : _ci.attachments) {
        VkAttachmentDescription vkAttachment{};

        // Convert format
        switch (attachment.format) {
        case EFormat::R8G8B8A8_UNORM:
            vkAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case EFormat::B8G8R8A8_UNORM:
            vkAttachment.format = VK_FORMAT_B8G8R8A8_UNORM;
            break;
        case EFormat::D32_SFLOAT:
            vkAttachment.format = VK_FORMAT_D32_SFLOAT;
            break;
        case EFormat::D24_UNORM_S8_UINT:
            vkAttachment.format = VK_FORMAT_D24_UNORM_S8_UINT;
            break;
        default:
            vkAttachment.format = m_swapChainImageFormat; // Fallback to swap chain format
            NE_CORE_WARN("Unsupported attachment format: {}, using swap chain format instead", (int)attachment.format);
            break;
        }

        // Convert sample count
        switch (attachment.samples) {
        case ESampleCount::Sample_1:
            vkAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            break;
        case ESampleCount::Sample_2:
            vkAttachment.samples = VK_SAMPLE_COUNT_2_BIT;
            break;
        case ESampleCount::Sample_4:
            vkAttachment.samples = VK_SAMPLE_COUNT_4_BIT;
            break;
        case ESampleCount::Sample_8:
            vkAttachment.samples = VK_SAMPLE_COUNT_8_BIT;
            break;
        case ESampleCount::Sample_16:
            vkAttachment.samples = VK_SAMPLE_COUNT_16_BIT;
            break;
        case ESampleCount::Sample_32:
            vkAttachment.samples = VK_SAMPLE_COUNT_32_BIT;
            break;
        case ESampleCount::Sample_64:
            vkAttachment.samples = VK_SAMPLE_COUNT_64_BIT;
            break;
        default:
            NE_CORE_ASSERT(false, "Unsupported sample count: {}", (int)attachment.samples);
        }

        // Convert load/store ops
        switch (attachment.loadOp) {
        case EAttachmentLoadOp::Load:
            vkAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            break;
        case EAttachmentLoadOp::Clear:
            vkAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            break;
        case EAttachmentLoadOp::DontCare:
            vkAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            break;
        }

        switch (attachment.storeOp) {
        case EAttachmentStoreOp::Store:
            vkAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            break;
        case EAttachmentStoreOp::DontCare:
            vkAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            break;
        default:
            NE_CORE_ASSERT(false, "Unsupported store operation: {}", (int)attachment.storeOp);
        }

        // Set stencil ops (simplified)
        switch (attachment.stencilLoadOp) {
        case EAttachmentLoadOp::Load:
            vkAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            break;
        case EAttachmentLoadOp::Clear:
            vkAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            break;
        case EAttachmentLoadOp::DontCare:
            vkAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            break;
        }

        switch (attachment.stencilStoreOp) {
        case EAttachmentStoreOp::Store:
            vkAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            break;
        case EAttachmentStoreOp::DontCare:
            vkAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            break;
        }

        // Set layouts
        vkAttachment.initialLayout = attachment.bInitialLayoutUndefined ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        vkAttachment.finalLayout   = attachment.bFinalLayoutPresentSrc ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        vkAttachments.push_back(vkAttachment);
    }

    // If no attachments specified, create default color + depth
    // if (vkAttachments.empty()) {
    //     // Default color attachment
    //     VkAttachmentDescription colorAttachment{
    //         .format         = m_swapChainImageFormat,
    //         .samples        = VK_SAMPLE_COUNT_1_BIT,
    //         .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
    //         .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
    //         .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    //         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    //         .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
    //         .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    //     };

    //     // Default depth attachment
    //     VkAttachmentDescription depthAttachment{
    //         .format         = findDepthFormat(),
    //         .samples        = VK_SAMPLE_COUNT_1_BIT,
    //         .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
    //         .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    //         .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    //         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    //         .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
    //         .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    //     };

    //     vkAttachments = {colorAttachment, depthAttachment};
    //     m_depthFormat = depthAttachment.format;
    // }

    // Create subpass configuration
    std::vector<VkSubpassDescription>  vkSubpasses;
    std::vector<VkAttachmentReference> colorAttachmentRefs;
    std::vector<VkAttachmentReference> depthAttachmentRefs;

    // Process subpasses from config or create default
    for (const auto &subpass : _ci.subpasses) {
        VkSubpassDescription vkSubpass{};
        vkSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

        // Add color attachment references
        for (uint32_t colorAttachmentIndex : subpass.colorAttachmentIndices) {
            colorAttachmentRefs.push_back({.attachment = colorAttachmentIndex,
                                           .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        }

        vkSubpass.colorAttachmentCount = static_cast<uint32_t>(subpass.colorAttachmentIndices.size());
        vkSubpass.pColorAttachments    = colorAttachmentRefs.data() + (colorAttachmentRefs.size() - subpass.colorAttachmentIndices.size());

        // Add depth attachment if specified
        if (subpass.depthStencilAttachmentIndex != UINT32_MAX) {
            depthAttachmentRefs.push_back({
                .attachment = subpass.depthStencilAttachmentIndex,
                .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            });
            vkSubpass.pDepthStencilAttachment = &depthAttachmentRefs.back();
        }

        vkSubpasses.push_back(vkSubpass);
    }


    // Create subpass dependencies
    std::vector<VkSubpassDependency> vkDependencies;
    NE_CORE_ASSERT(!_ci.dependencies.empty(), "Render pass must have at least one subpass dependency defined!");
    for (const auto &dependency : _ci.dependencies) {
        VkSubpassDependency vkDependency{
            .srcSubpass    = dependency.srcSubpass,
            .dstSubpass    = dependency.dstSubpass,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };
        vkDependencies.push_back(vkDependency);
    }

    // Create the render pass
    VkRenderPassCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(vkAttachments.size()),
        .pAttachments    = vkAttachments.data(),
        .subpassCount    = static_cast<uint32_t>(vkSubpasses.size()),
        .pSubpasses      = vkSubpasses.data(),
        .dependencyCount = static_cast<uint32_t>(vkDependencies.size()),
        .pDependencies   = vkDependencies.data(),
    };

    VkResult result = vkCreateRenderPass(m_logicalDevice, &createInfo, nullptr, &m_renderPass);
    NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create render pass with config!");

    NE_CORE_INFO("Created render pass with {} attachments, {} subpasses", vkAttachments.size(), vkSubpasses.size());
}

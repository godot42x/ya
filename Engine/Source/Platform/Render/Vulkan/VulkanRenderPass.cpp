#include "VulkanRenderPass.h"
#include "Core/Log.h"
#include "VulkanUtils.h"
#include <array>

#include <ranges>

#include "VulkanRender.h"

VulkanRenderPass::VulkanRenderPass(VulkanRender *render)
{
    _render = render;
}



void VulkanRenderPass::cleanup()
{

    if (m_renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(_render->getLogicalDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

void VulkanRenderPass::recreate(const RenderPassCreateInfo &ci)
{
    create(ci);
}

void VulkanRenderPass::beginRenderPass(VkCommandBuffer commandBuffer, uint32_t frameBufferIndex, VkExtent2D extent)
{
    VkRenderPassBeginInfo renderPassInfo{
        .sType      = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = m_renderPass,
        // .framebuffer = m_framebuffers[frameBufferIndex],
        .renderArea = {
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
    return {};
    // return findSupportedImageFormat(
    //     {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
    //     VK_IMAGE_TILING_OPTIMAL,
    //     VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkFormat VulkanRenderPass::findSupportedImageFormat(const std::vector<VkFormat> &candidates,
                                                    VkImageTiling                tiling,
                                                    VkFormatFeatureFlags         features)
{
    // for (VkFormat format : candidates)
    // {
    //     VkFormatProperties props;
    //     vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

    //     if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
    //     {
    //         return format;
    //     }
    //     else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
    //     {
    //         return format;
    //     }
    // }

    // NE_CORE_ASSERT(false, "Failed to find supported format!");
    return VK_FORMAT_UNDEFINED;
}


void convertToVkAttachmentDescription(const AttachmentDescription &desc, VkAttachmentDescription &outVkDesc)
{
    // Convert format
    switch (desc.format) {
    case EFormat::R8G8B8A8_UNORM:
        outVkDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
        break;
    case EFormat::B8G8R8A8_UNORM:
        outVkDesc.format = VK_FORMAT_B8G8R8A8_UNORM;
        break;
    case EFormat::D32_SFLOAT:
        outVkDesc.format = VK_FORMAT_D32_SFLOAT;
        break;
    case EFormat::D24_UNORM_S8_UINT:
        outVkDesc.format = VK_FORMAT_D24_UNORM_S8_UINT;
        break;
    default:
        UNIMPLEMENTED();
        break;
    }

    // Convert sample count
    switch (desc.samples) {
    case ESampleCount::Sample_1:
        outVkDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        break;
    case ESampleCount::Sample_2:
        outVkDesc.samples = VK_SAMPLE_COUNT_2_BIT;
        break;
    case ESampleCount::Sample_4:
        outVkDesc.samples = VK_SAMPLE_COUNT_4_BIT;
        break;
    case ESampleCount::Sample_8:
        outVkDesc.samples = VK_SAMPLE_COUNT_8_BIT;
        break;
    case ESampleCount::Sample_16:
        outVkDesc.samples = VK_SAMPLE_COUNT_16_BIT;
        break;
    case ESampleCount::Sample_32:
        outVkDesc.samples = VK_SAMPLE_COUNT_32_BIT;
        break;
    case ESampleCount::Sample_64:
        outVkDesc.samples = VK_SAMPLE_COUNT_64_BIT;
        break;
    default:
        UNREACHABLE();
    }

    // Convert load/store ops
    switch (desc.loadOp) {
    case EAttachmentLoadOp::Load:
        outVkDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        break;
    case EAttachmentLoadOp::Clear:
        outVkDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        break;
    case EAttachmentLoadOp::DontCare:
        outVkDesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        break;
    default:
        UNREACHABLE();
    }

    switch (desc.storeOp) {
    case EAttachmentStoreOp::Store:
        outVkDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        break;
    case EAttachmentStoreOp::DontCare:
        outVkDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        break;
    default:
        UNREACHABLE();
    }

    // Set stencil ops (simplified)
    switch (desc.stencilLoadOp) {
    case EAttachmentLoadOp::Load:
        outVkDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        break;
    case EAttachmentLoadOp::Clear:
        outVkDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        break;
    case EAttachmentLoadOp::DontCare:
        outVkDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        break;
    default:
        UNREACHABLE();
    }

    switch (desc.stencilStoreOp) {
    case EAttachmentStoreOp::Store:
        outVkDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        break;
    case EAttachmentStoreOp::DontCare:
        outVkDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        break;
    default:
        UNREACHABLE();
    }

    // Set layouts
    outVkDesc.initialLayout = desc.bInitialLayoutUndefined ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    outVkDesc.finalLayout   = desc.bFinalLayoutPresentSrc ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

bool VulkanRenderPass::createDefaultRenderPass()
{
    NE_CORE_INFO("no attachments defined, using default attachments preset");

    std::vector<VkAttachmentDescription> vkAttachments;

    // default color attachment
    VkAttachmentDescription defaultColorAttachment({
        .flags   = 0,
        .format  = _render->getSwapChain()->_surfaceFormat,
        .loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // ignore depth/stencil for now
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, // present for final drawing
    });

    vkAttachments.push_back(defaultColorAttachment);



    std::vector<VkSubpassDescription> vkSubpasses;

    VkAttachmentReference defaultColorAttachmentRef = {
        .attachment = 0, // index of the color attachment
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    vkSubpasses.push_back(VkSubpassDescription{
        .flags                   = 0,
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount    = 0,
        .pInputAttachments       = nullptr,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &defaultColorAttachmentRef,
        .pResolveAttachments     = nullptr, // No resolve for now
        .pDepthStencilAttachment = nullptr, // No depth/stencil for now
    });

    VkRenderPassCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(vkAttachments.size()),
        .pAttachments    = vkAttachments.data(),
        .subpassCount    = static_cast<uint32_t>(vkSubpasses.size()), // Update subpass count
        .pSubpasses      = vkSubpasses.data(),                        // Use the created subpasses
        .dependencyCount = 0,                                         // No dependencies for now
    };

    vkCreateRenderPass(_render->getLogicalDevice(), &createInfo, nullptr, &m_renderPass);
    if (m_renderPass == VK_NULL_HANDLE) {
        NE_CORE_ERROR("Failed to create default render pass!");
        return false;
    }
    NE_CORE_INFO("Default render pass created successfully");

    return false;
}

bool VulkanRenderPass::create(const RenderPassCreateInfo &ci)
{
    _ci = ci;

    // NE_CORE_ASSERT(!_ci.attachments.empty(), "Render pass must have at least one attachment defined!");
    if (_ci.attachments.empty() && _ci.subpasses.empty()) {
        return createDefaultRenderPass();
    }

    // Convert abstract configuration to Vulkan-specific values
    std::vector<VkAttachmentDescription> attachmentDescs;

    VkFormat surfaceFormat = _render->getSwapChain()->getSurfaceFormat();
    // Convert attachments from config
    int i = 0;
    for (const auto &attachmentDesc : _ci.attachments) {
        VkAttachmentDescription vkAttachmentDesc{
            .flags = 0,
        };
        convertToVkAttachmentDescription(attachmentDesc, vkAttachmentDesc);
        if (vkAttachmentDesc.format != surfaceFormat) {
            NE_CORE_WARN("RenderPassCI.attachments[{}]  Attachment format {} does not match surface format {}",
                         i,
                         std::to_string(vkAttachmentDesc.format),
                         std::to_string(surfaceFormat));
        }
        attachmentDescs.push_back(vkAttachmentDesc);
        ++i;
    }



    // Create subpass configuration
    std::vector<VkSubpassDescription> vkSubpasses;

    // the VkAttachmentReference  create in loop region will be destroyed after the loop
    // so we store them in outer region
    struct VulkanSubPassAttachmentReferenceCache
    {
        std::vector<VkAttachmentReference> inputAttachments;
        std::vector<VkAttachmentReference> colorAttachments;
        VkAttachmentReference              depthAttachment;
        VkAttachmentReference              resolveAttachment;
    };
    std::vector<VulkanSubPassAttachmentReferenceCache> referenceHandle(_ci.subpasses.size());

    // vkSubpasses.reserve(_ci.subpasses.size());

    for (int i = 0; i < _ci.subpasses.size(); i++)
    {
        const auto &subpass = _ci.subpasses[i];

        for (const auto &colorAttachment : subpass.colorAttachments)
        {
            referenceHandle[i].colorAttachments.push_back(VkAttachmentReference{
                .attachment = static_cast<uint32_t>(colorAttachment.ref),
                .layout     = toVkImageLayout(colorAttachment.layout),
            });
        }

        for (const auto &inputAttachment : subpass.inputAttachments) {
            referenceHandle[i].inputAttachments.push_back(VkAttachmentReference{
                .attachment = static_cast<uint32_t>(inputAttachment.ref),
                .layout     = toVkImageLayout(inputAttachment.layout),
            });
        }

        bool hasDepthAttachment   = subpass.depthAttachment.ref >= 0;
        bool hasResolveAttachment = subpass.resolveAttachment.ref >= 0;

        if (hasDepthAttachment) {
            referenceHandle[i].depthAttachment = VkAttachmentReference{
                .attachment = static_cast<uint32_t>(subpass.depthAttachment.ref),
                .layout     = toVkImageLayout(subpass.depthAttachment.layout),
            };
        }
        if (hasResolveAttachment) {
            referenceHandle[i].resolveAttachment = VkAttachmentReference{
                .attachment = static_cast<uint32_t>(subpass.resolveAttachment.ref),
                .layout     = toVkImageLayout(subpass.resolveAttachment.layout),
            };
        }


        VkSubpassDescription desc{
            .flags                   = 0,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount    = static_cast<uint32_t>(referenceHandle[i].inputAttachments.size()),
            .pInputAttachments       = referenceHandle[i].inputAttachments.empty() ? nullptr : referenceHandle[i].inputAttachments.data(),
            .colorAttachmentCount    = static_cast<uint32_t>(referenceHandle[i].colorAttachments.size()),
            .pColorAttachments       = referenceHandle[i].colorAttachments.empty() ? nullptr : referenceHandle[i].colorAttachments.data(),
            .pResolveAttachments     = hasResolveAttachment ? &referenceHandle[i].resolveAttachment : nullptr,
            .pDepthStencilAttachment = hasDepthAttachment ? &referenceHandle[i].depthAttachment : nullptr,
            // No preserve attachments for now
            .preserveAttachmentCount = 0,
            .pPreserveAttachments    = nullptr,
        };

        vkSubpasses.push_back(desc);
    }


    // Create subpass dependencies
    std::vector<VkSubpassDependency> vkDependencies;
    NE_CORE_ASSERT(!_ci.dependencies.empty(), "Render pass must have at least one subpass dependency defined!");
    for (const auto &dependency : _ci.dependencies) {
        VkSubpassDependency vkDependency{
            .srcSubpass   = dependency.bSrcExternal ? VK_SUBPASS_EXTERNAL : dependency.srcSubpass,
            .dstSubpass   = dependency.dstSubpass,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            // for next subpass to do a fragment shading?
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // from write to read?
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
        };

        vkDependencies.push_back(vkDependency);
    }

    // Create the render pass
    VkRenderPassCreateInfo createInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
        .pAttachments    = attachmentDescs.data(),
        .subpassCount    = static_cast<uint32_t>(vkSubpasses.size()),
        .pSubpasses      = vkSubpasses.data(),
        .dependencyCount = static_cast<uint32_t>(vkDependencies.size()),
        .pDependencies   = vkDependencies.data(),
    };

    VkResult result = vkCreateRenderPass(_render->getLogicalDevice(), &createInfo, nullptr, &m_renderPass);
    if (result != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to create Vulkan render pass: {}", result);
        return false;
    }
    // NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create render pass with config!");

    NE_CORE_INFO("Created render pass with {} attachments, {} subpasses", attachmentDescs.size(), vkSubpasses.size());

    return true;
}

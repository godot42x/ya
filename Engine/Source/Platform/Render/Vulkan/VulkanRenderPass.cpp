#include "VulkanRenderPass.h"
#include "Core/Log.h"
#include "VulkanUtils.h"
#include <array>

#include <ranges>

#include "VulkanRender.h"

VulkanRenderPass::VulkanRenderPass(VulkanRender *render)
{
    _render    = render;
    _swapChain = _render->getSwapChain();

    _swapChain->onRecreate.addLambda([this]() {
        // this->recreate(this->getCI());
    });
}


void VulkanRenderPass::cleanup()
{

    if (m_renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(_render->getLogicalDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}


void VulkanRenderPass::begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, VkExtent2D extent, const std::vector<VkClearValue> &clearValues)
{

    VkRenderPassBeginInfo renderPassBI{
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext       = nullptr,
        .renderPass  = getHandle(),
        .framebuffer = framebuffer,
        .renderArea  = {
             .offset = {0, 0},
             .extent = extent,
        },
        .clearValueCount = static_cast<uint32_t>(clearValues.size()),
        .pClearValues    = clearValues.data(),
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassBI, VK_SUBPASS_CONTENTS_INLINE); // ? contents
}

void VulkanRenderPass::end(VkCommandBuffer commandBuffer)
{
    vkCmdEndRenderPass(commandBuffer);
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

bool VulkanRenderPass::recreate(const RenderPassCreateInfo &ci)
{
    NE_CORE_INFO("Recreating render pass...");
    _ci = ci;

    // NE_CORE_ASSERT(!_ci.attachments.empty(), "Render pass must have at least one attachment defined!");
    if (_ci.attachments.empty() && _ci.subpasses.empty()) {
        return createDefaultRenderPass();
    }

    // Convert abstract configuration to Vulkan-specific values
    std::vector<VkAttachmentDescription> attachmentDescs;

    VkFormat surfaceFormat = _render->getSwapChain()->getSurfaceFormat();
    // Convert attachments from config
    for (const AttachmentDescription &attachmentDesc : _ci.attachments) {
        VkAttachmentDescription vkAttachmentDesc{
            .flags          = 0,
            .format         = toVk(attachmentDesc.format),
            .samples        = toVk(attachmentDesc.samples),
            .loadOp         = toVk(attachmentDesc.loadOp),
            .storeOp        = toVk(attachmentDesc.storeOp),
            .stencilLoadOp  = toVk(attachmentDesc.stencilLoadOp),
            .stencilStoreOp = toVk(attachmentDesc.stencilStoreOp),
            .initialLayout  = toVk(attachmentDesc.initialLayout),
            .finalLayout    = toVk(attachmentDesc.finalLayout),
        };
        attachmentDescs.push_back(vkAttachmentDesc);
    }

    if (attachmentDescs[0].format != surfaceFormat) {
        NE_CORE_ERROR("RenderPassCI.attachments[{}]  Attachment format {} does not match surface format {}",
                      0,
                      std::to_string(attachmentDescs[0].format),
                      std::to_string(surfaceFormat));
    }


    // Create subpass configuration
    std::vector<VkSubpassDescription> vkSubpassDescs;

    // Also do translation, but we define it outside for lifetime in {}
    struct VulkanSubPassAttachmentReferenceCache
    {
        std::vector<VkAttachmentReference> inputAttachments;
        std::vector<VkAttachmentReference> colorAttachments;
        VkAttachmentReference              depthAttachment{};
        VkAttachmentReference              resolveAttachment{};
    };
    std::vector<VulkanSubPassAttachmentReferenceCache> subPassAttachments(_ci.subpasses.size());

    // vkSubpasses.reserve(_ci.subpasses.size());

    for (std::size_t subPassIdx = 0; subPassIdx < _ci.subpasses.size(); subPassIdx++)
    {
        const auto &subpass = _ci.subpasses[subPassIdx];
        NE_ASSERT(subpass.subpassIndex == subPassIdx, "Subpass index mismatch: expected {}, got {}", subPassIdx, subpass.subpassIndex);

        for (const auto &colorAttachment : subpass.colorAttachments)
        {
            subPassAttachments[subPassIdx].colorAttachments.push_back(VkAttachmentReference{
                .attachment = static_cast<uint32_t>(colorAttachment.ref),
                .layout     = toVk(colorAttachment.layout),
            });
        }

        for (const auto &inputAttachment : subpass.inputAttachments) {
            subPassAttachments[subPassIdx].inputAttachments.push_back(VkAttachmentReference{
                .attachment = static_cast<uint32_t>(inputAttachment.ref),
                .layout     = toVk(inputAttachment.layout),
            });
        }

        bool hasDepthAttachment   = subpass.depthAttachment.ref >= 0;
        bool hasResolveAttachment = subpass.resolveAttachment.ref >= 0;

        if (hasDepthAttachment) {
            subPassAttachments[subPassIdx].depthAttachment = VkAttachmentReference{
                .attachment = static_cast<uint32_t>(subpass.depthAttachment.ref),
                .layout     = toVk(subpass.depthAttachment.layout),
            };
        }
        if (hasResolveAttachment) {
            subPassAttachments[subPassIdx].resolveAttachment = VkAttachmentReference{
                .attachment = static_cast<uint32_t>(subpass.resolveAttachment.ref),
                .layout     = toVk(subpass.resolveAttachment.layout),
            };
        }


        VkSubpassDescription desc{
            .flags                   = 0,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount    = static_cast<uint32_t>(subPassAttachments[subPassIdx].inputAttachments.size()),
            .pInputAttachments       = subPassAttachments[subPassIdx].inputAttachments.empty() ? nullptr : subPassAttachments[subPassIdx].inputAttachments.data(),
            .colorAttachmentCount    = static_cast<uint32_t>(subPassAttachments[subPassIdx].colorAttachments.size()),
            .pColorAttachments       = subPassAttachments[subPassIdx].colorAttachments.empty() ? nullptr : subPassAttachments[subPassIdx].colorAttachments.data(),
            .pResolveAttachments     = hasResolveAttachment ? &subPassAttachments[subPassIdx].resolveAttachment : nullptr,
            .pDepthStencilAttachment = hasDepthAttachment ? &subPassAttachments[subPassIdx].depthAttachment : nullptr,
            // No preserve attachments for now
            .preserveAttachmentCount = 0,
            .pPreserveAttachments    = nullptr,
        };

        vkSubpassDescs.push_back(desc);
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
        .subpassCount    = static_cast<uint32_t>(vkSubpassDescs.size()),
        .pSubpasses      = vkSubpassDescs.data(),
        .dependencyCount = static_cast<uint32_t>(vkDependencies.size()),
        .pDependencies   = vkDependencies.data(),
    };

    VkResult result = vkCreateRenderPass(_render->getLogicalDevice(), &createInfo, nullptr, &m_renderPass);
    if (result != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to create Vulkan render pass: {}", result);
        return false;
    }
    // NE_CORE_ASSERT(result == VK_SUCCESS, "Failed to create render pass with config!");

    NE_CORE_INFO("Created render pass with {} attachments, {} subpasses", attachmentDescs.size(), vkSubpassDescs.size());

    return true;
}

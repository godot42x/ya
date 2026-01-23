#include "VulkanRenderTarget.h"
#include "Core/App/App.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/Image.h"
#include "imgui.h"

// Vulkan-specific includes
#include "VulkanCommandBuffer.h"
#include "VulkanImage.h"
#include "VulkanRender.h"

namespace ya
{


VulkanRenderTarget::VulkanRenderTarget(const RenderTargetCreateInfo &ci)
{
    _renderPass       = ci.renderPass;
    _frameBufferCount = ci.frameBufferCount;
    _extent           = {.width = static_cast<uint32_t>(ci.extent.x), .height = static_cast<uint32_t>(ci.extent.y)};
    label             = ci.label;
    bSwapChainTarget  = ci.bSwapChainTarget;
    if (bSwapChainTarget) {
        _extent = {
            .width  = App::get()->getRender()->getSwapchainWidth(),
            .height = App::get()->getRender()->getSwapchainHeight(),
        };
        _frameBufferCount = App::get()->getRender()->getSwapchain()->getImageCount();
    }

    init();
    recreate();
}

VulkanRenderTarget::~VulkanRenderTarget()
{
    destroy();
}

void VulkanRenderTarget::init()
{
    _clearValues.resize(_renderPass->getAttachmentCount());
    setColorClearValue(ClearValue(0.0f, 0.0f, 0.0f, 1.0f));
    setDepthStencilClearValue(ClearValue(1.0f, 0));
}


void VulkanRenderTarget::recreate()
{
    YA_CORE_INFO("Recreating VulkanRenderTarget {} with extent: {}x{}, frameBufferCount: {}", label, _extent.width, _extent.height, _frameBufferCount);
    if (_extent.width <= 0 || _extent.height <= 0)
    {
        return;
    }

    // Notify listeners before clearing framebuffers (so they can cleanup old ImageView references)
    onFrameBufferRecreated.broadcast();

    _frameBuffers.clear();
    _frameBuffers.resize(_frameBufferCount);

    auto render      = App::get()->getRender();
    auto swapchain   = render->getSwapchain();
    auto attachments = _renderPass->getAttachments();
    if (attachments.empty()) {
        return;
    }

    auto vkRender    = static_cast<VulkanRender *>(render);
    auto vkSwapChain = static_cast<VulkanSwapChain *>(swapchain);

    for (size_t i = 0; i < _frameBufferCount; i++)
    {
        std::vector<std::shared_ptr<IImage>> fbAttachments;
        int                                  j = 0;
        for (const AttachmentDescription &attachment : attachments)
        {
            {
                // created by swapchain
                if (bSwapChainTarget &&
                    attachment.finalLayout == EImageLayout::PresentSrcKHR &&
                    attachment.samples == ESampleCount::Sample_1)
                {
                    // Use Vulkan-specific swapchain to get VkImage handles
                    auto ptr = VulkanImage::from(vkRender,
                                                 vkSwapChain->getVkImages()[i],
                                                 toVk(attachment.format),
                                                 toVk(attachment.usage));
                    fbAttachments.push_back(ptr);
                }
                else {
                    auto ptr = VulkanImage::create(
                        vkRender,
                        ImageCreateInfo{
                            .label  = std::format("RT_FrameBuffer_{}_{}_Attachment_{}", label, i, j),
                            .format = attachment.format,
                            .extent = {
                                .width  = static_cast<uint32_t>(_extent.width),
                                .height = static_cast<uint32_t>(_extent.height),
                                .depth  = 1,
                            },
                            .mipLevels     = 1,
                            .samples       = attachment.samples,
                            .usage         = attachment.usage,
                            .sharingMode   = ESharingMode::Exclusive,
                            .initialLayout = EImageLayout::Undefined,
                        });
                    fbAttachments.push_back(ptr);
                }
                vkRender->setDebugObjectName(
                    VK_OBJECT_TYPE_IMAGE,
                    (fbAttachments.back())->getHandle(),
                    std::format("RT_FrameBuffer_{}_{}_Attachment_{}", label, i, j).c_str());
                ++j;
            }
        }

        auto fb = IFrameBuffer::create(render,
                                       _renderPass,
                                       FrameBufferCreateInfo{
                                           .width  = _extent.width,
                                           .height = _extent.height,
                                           .images = fbAttachments,
                                       });
        fb->recreate(fbAttachments, _extent.width, _extent.height);
        _frameBuffers[i] = fb;
        vkRender->setDebugObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, _frameBuffers[i]->getHandleAs<VkFramebuffer>(), std::format("RT_FrameBuffer_{}_{}", label, i));
    }
}

void VulkanRenderTarget::destroy()
{
    _materialSystems.clear();
}

void VulkanRenderTarget::onUpdate(float deltaTime)
{
    for (auto &system : _materialSystems) {
        if (system->bEnabled) {
            system->onUpdateByRenderTarget(deltaTime, this);
        }
    }
}

void VulkanRenderTarget::onRenderGUI()
{
    ImGui::PushID(label.c_str());
    if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        for (auto &system : _materialSystems) {
            system->renderGUI();
        }
        ImGui::Unindent();
    }
    ImGui::PopID();
}

void VulkanRenderTarget::begin(ICommandBuffer *cmdBuf)
{
    YA_PROFILE_FUNCTION()
    YA_CORE_ASSERT(!bBeginTarget, "Render target is already begun");

    if (bDirty)
    {
        recreate();
        bDirty = false;
    }

    if (bSwapChainTarget) {
        auto render        = App::get()->getRender();
        auto swapchain     = render->getSwapchain();
        _currentFrameIndex = swapchain->getCurImageIndex();
    }
    else {
        _currentFrameIndex = (_currentFrameIndex + 1) % _frameBufferCount;
    }

    YA_CORE_ASSERT(getFrameBuffer(), "FrameBuffer is null in VulkanRenderTarget::begin");

    _renderPass->begin(cmdBuf,
                       getFrameBuffer()->getHandle(),
                       {
                           .width  = static_cast<uint32_t>(_extent.width),
                           .height = static_cast<uint32_t>(_extent.height),
                       },
                       _clearValues);
    bBeginTarget = true;
}

void VulkanRenderTarget::end(ICommandBuffer *cmdBuf)
{
    // YA_CORE_ASSERT(bBeginTarget, "Render target is not begun, cannot end");
    _renderPass->end(cmdBuf);
    bBeginTarget = false;
}

void VulkanRenderTarget::setFrameBufferCount(uint32_t count)
{
    _frameBufferCount = count;
    bDirty            = true;
}

void VulkanRenderTarget::setColorClearValue(ClearValue clearValue)
{
    for (uint32_t i = 0; i < _clearValues.size(); i++)
    {
        setColorClearValue(i, clearValue);
    }
}

void VulkanRenderTarget::setColorClearValue(uint32_t index, ClearValue clearValue)
{
    const auto &rpAttachments = _renderPass->getAttachments();
    if (index < _clearValues.size()) {

        if (rpAttachments[index].usage & EImageUsage::ColorAttachment)
        {
            if (!RenderHelper::isDepthStencilFormat(rpAttachments[index].format) &&
                rpAttachments[index].loadOp == EAttachmentLoadOp::Clear)
            {
                _clearValues[index]                = clearValue;
                _clearValues[index].isDepthStencil = false;
            }
            else {
                YA_CORE_WARN("Attempting to set color clear value on non-color attachment, {}", index);
            }
        }
    }
}

void VulkanRenderTarget::setDepthStencilClearValue(ClearValue clearValue)
{
    for (uint32_t i = 0; i < _clearValues.size(); i++)
    {
        setDepthStencilClearValue(i, clearValue);
    }
}

void VulkanRenderTarget::setDepthStencilClearValue(uint32_t index, ClearValue clearValue)
{
    const auto &rpAttachments = _renderPass->getAttachments();
    if (index < _clearValues.size())
    {
        if (rpAttachments[index].usage & EImageUsage::DepthStencilAttachment)
        {
            if (RenderHelper::isDepthStencilFormat(rpAttachments[index].format) &&
                rpAttachments[index].loadOp == EAttachmentLoadOp::Clear)
            {
                _clearValues[index]                = clearValue;
                _clearValues[index].isDepthStencil = true;
            }
            else {
                YA_CORE_WARN("Attempting to set depth stencil clear value on non-depth attachment, {}", index);
            }
        }
    }
};

void VulkanRenderTarget::renderMaterialSystems(ICommandBuffer *cmdBuf)
{
    YA_PROFILE_FUNCTION()
    for (auto &system : _materialSystems) {
        if (system->bEnabled) {
            YA_PROFILE_SCOPE(std::format("RenderMaterialSystem_{}", system->_label));
            system->onRender(cmdBuf, this);
        }
    }
}

void VulkanRenderTarget::addMaterialSystemImpl(std::shared_ptr<IMaterialSystem> system)
{
    _materialSystems.push_back(system);
}

} // namespace ya

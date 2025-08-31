#include "RenderTarget.h"
#include "Core/App/App.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

namespace ya
{

RenderTarget::RenderTarget(VulkanRenderPass *renderPass)
{
    _renderPass = renderPass;

    auto vkRender     = static_cast<VulkanRender *>(App::get()->getRender());
    auto swapChain    = vkRender->getSwapChain();
    auto ext          = vkRender->getSwapChain()->getExtent();
    _extent           = {.width = ext.width, .height = ext.height};
    _frameBufferCount = vkRender->getSwapChain()->getImages().size();
    bSwapChainTarget  = true;



    swapChain->onRecreate.addLambda(
        this,
        [this](VulkanSwapChain::DiffInfo old, VulkanSwapChain::DiffInfo now) {
            if (now.extent.width != old.extent.width ||
                now.extent.height != old.extent.height ||
                old.presentMode != now.presentMode)
            {
                this->setExtent(now.extent);
            }
        });

    init();
    recreate();
}

RenderTarget::RenderTarget(VulkanRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent)
{
    _renderPass       = renderPass;
    _frameBufferCount = frameBufferCount;
    _extent           = {.width = static_cast<uint32_t>(extent.x), .height = static_cast<uint32_t>(extent.y)};

    init();
    recreate();
}

void RenderTarget::init()
{
    _clearValues.resize(_renderPass->getAttachmentCount());
    setColorClearValue(VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 1.0f}}});
    setDepthStencilClearValue(VkClearValue{.depthStencil = {1.0f, 0}});
}


void RenderTarget::recreate()
{
    YA_CORE_INFO("Recreating RenderTarget with extent: {}x{}, frameBufferCount: {}", _extent.width, _extent.height, _frameBufferCount);
    if (_extent.width <= 0 || _extent.height <= 0)
    {
        return;
    }
    _frameBuffers.clear();
    _frameBuffers.resize(_frameBufferCount);

    auto vkRender    = App::get()->getRender<VulkanRender>();
    auto swapChain   = vkRender->getSwapChain();
    auto attachments = _renderPass->getAttachments();
    if (attachments.empty()) {
        return;
    }

    for (size_t i = 0; i < _frameBufferCount; i++)
    {
        auto fb          = std::make_shared<VulkanFrameBuffer>(vkRender, _renderPass, _extent.width, _extent.height);
        _frameBuffers[i] = fb;

        std::vector<std::shared_ptr<VulkanImage>> fbAttachments;

        int j = 0;
        for (const AttachmentDescription &attachment : attachments)
        {
            // created by swapchain
            if (bSwapChainTarget &&
                attachment.finalLayout == EImageLayout::PresentSrcKHR &&
                attachment.samples == ESampleCount::Sample_1)
            {

                auto ptr = VulkanImage::from(vkRender,
                                             swapChain->getImages()[i],
                                             toVk(attachment.format),
                                             toVk(attachment.usage));
                fbAttachments.push_back(ptr);
            }
            else {
                auto ptr = VulkanImage::create(
                    vkRender,
                    ImageCreateInfo{
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
                        // .finalLayout    = attachment.finalLayout,
                    });
                fbAttachments.push_back(ptr);
            }
            vkRender->setDebugObjectName(
                VK_OBJECT_TYPE_IMAGE,
                (fbAttachments.back())->getHandle(),
                std::format("RT_FrameBuffer_{}_Attachment_{}", i, j).c_str());
            ++j;
        }

        _frameBuffers[i]->recreate(fbAttachments, _extent.width, _extent.height);


        vkRender->setDebugObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, _frameBuffers[i]->getHandle(), std::format("RT_FrameBuffer_{}", i));
    }
}

void RenderTarget::destroy()
{
    _materialSystems.clear();
}

void RenderTarget::onUpdate(float deltaTime)
{

    for (auto &system : _materialSystems) {
        system->onUpdate(deltaTime);
    }
}

void RenderTarget::onRenderGUI()
{
    for (auto &system : _materialSystems) {
        system->onRenderGUI();
    }
}



void RenderTarget::begin(void *cmdBuf)
{
    YA_CORE_ASSERT(!bBeginTarget, "Render target is already begun");

    VkCommandBuffer vkCmdBuf = static_cast<VkCommandBuffer>(cmdBuf);
    if (bDirty)
    {
        recreate();
        bDirty = false;
    }

    if (getCamera() && getCamera()->hasComponent<CameraComponent>()) {
        auto &cc = getCamera()->getComponent<CameraComponent>();
        cc.setAspectRatio(static_cast<float>(_extent.width) / static_cast<float>(_extent.height));
    }

    if (bSwapChainTarget) {
        auto render        = App::get()->getRender<VulkanRender>();
        _currentFrameIndex = render->getSwapChain()->getCurImageIndex();
    }
    else {
        _currentFrameIndex = (_currentFrameIndex + 1) % _frameBufferCount; // For custom render targets, always use the first frame buffer
    }

    _renderPass->begin(vkCmdBuf,
                       getFrameBuffer()->getHandle(),
                       {
                           static_cast<uint32_t>(_extent.width),
                           static_cast<uint32_t>(_extent.height),
                       },
                       _clearValues);
    bBeginTarget = true;
}

void RenderTarget::end(void *cmdBuf)
{

    // YA_CORE_ASSERT(bBeginTarget, "Render target is not begun, cannot end");
    auto vkCmdBuf = static_cast<VkCommandBuffer>(cmdBuf);
    _renderPass->end(vkCmdBuf);
    bBeginTarget = false;
}

void RenderTarget::setExtent(VkExtent2D extent)
{
    _extent = extent;
    bDirty  = true;
}

void RenderTarget::setBufferCount(uint32_t count)
{
    _frameBufferCount = count;
    bDirty            = true;
}

void RenderTarget::setColorClearValue(VkClearValue clearValue)
{
    for (uint32_t i = 0; i < _clearValues.size(); i++)
    {
        setColorClearValue(i, clearValue);
    }
}

void RenderTarget::setColorClearValue(uint32_t index, VkClearValue clearValue)
{
    const auto &rpAttachments = _renderPass->getAttachments();
    if (index < _clearValues.size()) {

        if (rpAttachments[index].usage & EImageUsage::ColorAttachment)
        {
            if (!RenderHelper::isDepthStencilFormat(rpAttachments[index].format) &&
                rpAttachments[index].loadOp == EAttachmentLoadOp::Clear)
            {
                _clearValues[index].color = clearValue.color;
            }
            else {
                YA_CORE_WARN("Attempting to set color clear value on non-color attachment, {}", index);
            }
        }
    }
}

void RenderTarget::setDepthStencilClearValue(VkClearValue clearValue)
{
    for (int i = 0; i < _clearValues.size(); i++)
    {
        setDepthStencilClearValue(i, clearValue);
    }
}

void RenderTarget::setDepthStencilClearValue(uint32_t index, VkClearValue clearValue)
{
    const auto &rpAttachments = _renderPass->getAttachments();
    if (index < _clearValues.size())
    {
        if (rpAttachments[index].usage & EImageUsage::DepthStencilAttachment)
        {
            if (RenderHelper::isDepthStencilFormat(rpAttachments[index].format) &&
                rpAttachments[index].loadOp == EAttachmentLoadOp::Clear)
            {
                _clearValues[index].depthStencil = clearValue.depthStencil;
            }
            else {
                YA_CORE_WARN("Attempting to set depth stencil clear value on non-depth attachment, {}", index);
            }
        }
    }
};

} // namespace ya

#include "RenderTarget.h"
#include "Core/App/App.h"
#include "Core/Event.h"
#include "Core/MessageBus.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/Image.h"
#include "imgui.h"


// Platform-specific includes
#if USE_VULKAN
    #include "Platform/Render/Vulkan/VulkanCommandBuffer.h"
    #include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
    #include "Platform/Render/Vulkan/VulkanImage.h"
    #include "Platform/Render/Vulkan/VulkanRender.h"
#endif


namespace ya
{

RenderTarget::RenderTarget(IRenderPass *renderPass) : _camera(nullptr)
{
    _renderPass = renderPass;

    auto r  = App::get()->getRender();
    _extent = {
        .width  = r->getSwapchainWidth(),
        .height = r->getSwapchainHeight(),
    };
    _frameBufferCount = r->getSwapchainImageCount();
    bSwapChainTarget  = true;

    r->getSwapchain()->onRecreate.addLambda(
        this,
        [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now) {
            if (now.extent.width != old.extent.width ||
                now.extent.height != old.extent.height ||
                old.presentMode != now.presentMode)
            {
                Extent2D newExtent{
                    .width  = now.extent.width,
                    .height = now.extent.height,
                };
                this->setExtent(newExtent);
            }
        });

    init();
    recreate();
}

RenderTarget::RenderTarget(IRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent) : _camera(nullptr)
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
    setColorClearValue(ClearValue(0.0f, 0.0f, 0.0f, 1.0f));
    setDepthStencilClearValue(ClearValue(1.0f, 0));
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

    auto render      = App::get()->getRender();
    auto swapchain   = render->getSwapchain();
    auto attachments = _renderPass->getAttachments();
    if (attachments.empty()) {
        return;
    }

#if USE_VULKAN
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
                    std::format("RT_FrameBuffer_{}_Attachment_{}", i, j).c_str());
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
        vkRender->setDebugObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, _frameBuffers[i]->getHandleAs<VkFramebuffer>(), std::format("RT_FrameBuffer_{}", i));
    }
#else
    #error "Platform not supported"
#endif
}

void RenderTarget::destroy()
{
    _materialSystems.clear();
}

void RenderTarget::onUpdate(float deltaTime)
{

    for (auto &system : _materialSystems) {
        if (system->bEnabled) {

            system->onUpdate(deltaTime);
        }
    }
}

void RenderTarget::onRenderGUI()
{
    ImGui::CollapsingHeader("RenderTarget", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::Checkbox("Use Entity Camera", &bEntityCamera)) {
        if (bEntityCamera) {
            // copy transform from app camera to entity camera
        }
    }
    for (auto &system : _materialSystems) {
        system->onRenderGUI();
        system->onEndRenderGUI();
    }
}



void RenderTarget::begin(ICommandBuffer *cmdBuf)
{
    YA_CORE_ASSERT(!bBeginTarget, "Render target is already begun");

    if (bDirty)
    {
        recreate();
        bDirty = false;
    }

    if (getCamera() && getCamera()->hasComponent<CameraComponent>()) {
        auto cc = getCameraMut()->getComponent<CameraComponent>();
        cc->setAspectRatio(static_cast<float>(_extent.width) / static_cast<float>(_extent.height));
    }

    if (bSwapChainTarget) {
        auto render        = App::get()->getRender();
        auto swapchain     = render->getSwapchain();
        _currentFrameIndex = swapchain->getCurImageIndex();
    }
    else {
        _currentFrameIndex = (_currentFrameIndex + 1) % _frameBufferCount;
    }

    _renderPass->begin(cmdBuf,
                       getFrameBuffer()->getHandle(),
                       {
                           .width  = static_cast<uint32_t>(_extent.width),
                           .height = static_cast<uint32_t>(_extent.height),
                       },
                       _clearValues);
    bBeginTarget = true;
}

void RenderTarget::end(ICommandBuffer *cmdBuf)
{
    // YA_CORE_ASSERT(bBeginTarget, "Render target is not begun, cannot end");
    _renderPass->end(cmdBuf);
    bBeginTarget = false;
}


void RenderTarget::setBufferCount(uint32_t count)
{
    _frameBufferCount = count;
    bDirty            = true;
}

void RenderTarget::setColorClearValue(ClearValue clearValue)
{
    for (uint32_t i = 0; i < _clearValues.size(); i++)
    {
        setColorClearValue(i, clearValue);
    }
}

void RenderTarget::setColorClearValue(uint32_t index, ClearValue clearValue)
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

void RenderTarget::setDepthStencilClearValue(ClearValue clearValue)
{
    for (uint32_t i = 0; i < _clearValues.size(); i++)
    {
        setDepthStencilClearValue(i, clearValue);
    }
}

void RenderTarget::setDepthStencilClearValue(uint32_t index, ClearValue clearValue)
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

void RenderTarget::renderMaterialSystems(ICommandBuffer *cmdBuf)
{
    for (auto &system : _materialSystems) {
        if (system->bEnabled) {
            system->onRender(cmdBuf, this);
        }
    }
}

const glm::mat4 RenderTarget::getProjectionMatrix() const
{
    glm::mat4 ret(1.0);
    if (isUseEntityCamera()) {
        if (const Entity *cam = getCamera()) {
            if (cam->hasComponent<CameraComponent>()) {
                ret = cam->getComponent<CameraComponent>()->getProjection();
            }
        }
    }
    else {
        // use app camera
        return App::get()->camera.getProjectionMatrix();
    }
    ret[1][1] *= -1;
    return ret;
}

const glm::mat4 RenderTarget::getViewMatrix() const
{
    glm::mat4 ret;
    if (isUseEntityCamera()) {
        if (auto *cam = getCamera()) {
            if (cam->hasComponent<CameraComponent>()) {
                ret = cam->getComponent<CameraComponent>()->getView();
            }
        }
    }
    else {
        ret = App::get()->camera.getViewMatrix();
    }
    return ret;
}

void RenderTarget::getViewAndProjMatrix(glm::mat4 &view, glm::mat4 &proj) const
{
    view = proj = glm::mat4(1.0f);
    if (isUseEntityCamera()) {
        if (auto *cam = getCamera()) {
            if (cam->hasComponent<CameraComponent>()) {
                auto cc = cam->getComponent<CameraComponent>();
                proj    = cc->getProjection();
#if USE_VULKAN
                // proj[1][1] *= -1;
#endif
                view = cc->getView();
                return;
            }
        }
    }

    // use app camera
    proj = App::get()->camera.getProjectionMatrix();
#if USE_VULKAN
    // proj[1][1] *= -1; // Invert Y for Vulkan
#endif
    view = App::get()->camera.getViewMatrix();
}

void RenderTarget::addMaterialSystemImpl(std::shared_ptr<IMaterialSystem> system)
{
    _materialSystems.push_back(system);
}

// Factory functions
std::shared_ptr<IRenderTarget> createRenderTarget(IRenderPass *renderPass)
{
    return std::make_shared<RenderTarget>(renderPass);
}

std::shared_ptr<IRenderTarget> createRenderTarget(IRenderPass *renderPass, uint32_t frameBufferCount, glm::vec2 extent)
{
    return std::make_shared<RenderTarget>(renderPass, frameBufferCount, extent);
}

} // namespace ya

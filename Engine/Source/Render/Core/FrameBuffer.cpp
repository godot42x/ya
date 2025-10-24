
#include "FrameBuffer.h"
#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"

namespace ya
{

stdptr<IFrameBuffer> IFrameBuffer::create(IRender* render, IRenderPass* renderPass, const FrameBufferCreateInfo& createInfo)
{
    auto api = App::get()->getRender()->getAPI();
    switch (api) {
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
        break;
    case ERenderAPI::Vulkan: {
        auto vkRender = static_cast<VulkanRender*>(render);
        auto vkRenderPass = static_cast<VulkanRenderPass*>(renderPass);
        
        // Create VulkanFrameBuffer
        auto fb = makeShared<VulkanFrameBuffer>(vkRender, vkRenderPass, createInfo.width, createInfo.height);
        
        // Convert image views
        std::vector<VkImageView> vkImageViews;
        vkImageViews.reserve(createInfo.imageViews.size());
        for (auto view : createInfo.imageViews) {
            vkImageViews.push_back(static_cast<VkImageView>(view));
        }
        
        // Create framebuffer using Vulkan API
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = vkRenderPass->getHandleAs<VkRenderPass>();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(vkImageViews.size());
        framebufferInfo.pAttachments    = vkImageViews.data();
        framebufferInfo.width           = createInfo.width;
        framebufferInfo.height          = createInfo.height;
        framebufferInfo.layers          = 1;

        if (vkCreateFramebuffer(vkRender->getDevice(), &framebufferInfo, nullptr, &fb->_framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer!");
        }

        fb->_imageViews = vkImageViews;
        
        return fb;
    }
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX:
        break;
    }
    UNREACHABLE();
    return nullptr;
}

} // namespace ya

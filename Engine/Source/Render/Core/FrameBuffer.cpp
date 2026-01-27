
#include "FrameBuffer.h"
#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"

namespace ya
{

stdptr<IFrameBuffer> IFrameBuffer::create(IRender *render, IRenderPass *renderPass, const FrameBufferCreateInfo &ci)
{
    auto api = App::get()->getRender()->getAPI();
    switch (api) {
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
        break;
    case ERenderAPI::Vulkan:
    {
        auto vkRender     = static_cast<VulkanRender *>(render);
        auto vkRenderPass = static_cast<VulkanRenderPass *>(renderPass);
        auto fb           = makeShared<VulkanFrameBuffer>(vkRender, vkRenderPass, ci.width, ci.height);
        fb->recreate(ci.images, ci.width, ci.height);
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

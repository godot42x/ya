
#include "FrameBuffer.h"
#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"

namespace ya
{

stdptr<IFrameBuffer> IFrameBuffer::create(IRender *render, const FrameBufferCreateInfo &ci)
{
    auto api = App::get()->getRender()->getAPI();
    switch (api) {
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
        break;
    case ERenderAPI::Vulkan:
    {
        auto fb = makeShared<VulkanFrameBuffer>(render->as<VulkanRender>());
        fb->recreate(ci);
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

void IFrameBuffer::clearAttachments()
{
    _colorTextures.clear();
    _depthTexture.reset();
}

Texture *IFrameBuffer::getColorTexture(uint32_t attachmentIdx) const
{
    if (attachmentIdx >= _colorTextures.size()) {
        return nullptr;
    }
    return _colorTextures[attachmentIdx].get();
}


} // namespace ya
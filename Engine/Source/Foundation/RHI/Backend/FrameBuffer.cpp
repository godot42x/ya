
#include "Foundation/RHI/Core/FrameBuffer.h"
#include "Foundation/RHI/Backend/Vulkan/VulkanFrameBuffer.h"

#include <format>

namespace ya
{

namespace
{

std::shared_ptr<Texture> makeTextureCompat(const std::shared_ptr<RenderImage>& attachment, const std::string& label)
{
    if (!attachment || !attachment->getImageShared() || !attachment->getImageViewShared()) {
        return nullptr;
    }

    auto texture = Texture::wrap(attachment->getImageShared(), attachment->getImageViewShared(), label);
    if (!texture) {
        return nullptr;
    }

    texture->_width  = attachment->getWidth();
    texture->_height = attachment->getHeight();
    texture->_format = attachment->getFormat();
    return texture;
}

} // namespace

stdptr<IFrameBuffer> IFrameBuffer::create(IRender *render, const FrameBufferCreateInfo &ci)
{
    YA_CORE_ASSERT(render != nullptr, "IFrameBuffer::create requires a valid render backend");

    auto api = render->getAPI();
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
    _colorAttachments.clear();
    _depthAttachment.reset();
    _resolveAttachment.reset();
    _colorTextures.clear();
    _depthTexture.reset();
    _resolveTexture.reset();
}

RenderImage* IFrameBuffer::getColorAttachment(uint32_t attachmentIdx) const
{
    if (attachmentIdx >= _colorAttachments.size()) {
        return nullptr;
    }
    return _colorAttachments[attachmentIdx].get();
}

std::shared_ptr<RenderImage> IFrameBuffer::getColorAttachmentShared(uint32_t attachmentIdx) const
{
    if (attachmentIdx >= _colorAttachments.size()) {
        return nullptr;
    }
    return _colorAttachments[attachmentIdx];
}

void IFrameBuffer::rebuildTextureCompatCache() const
{
    _colorTextures.clear();
    _colorTextures.reserve(_colorAttachments.size());
    for (size_t i = 0; i < _colorAttachments.size(); ++i) {
        _colorTextures.push_back(makeTextureCompat(_colorAttachments[i], std::format("{}_Color{}_Compat", _label, i)));
    }

    _depthTexture   = makeTextureCompat(_depthAttachment, std::format("{}_Depth_Compat", _label));
    _resolveTexture = makeTextureCompat(_resolveAttachment, std::format("{}_Resolve_Compat", _label));
}

Texture *IFrameBuffer::getColorTexture(uint32_t attachmentIdx) const
{
    if (_colorTextures.size() != _colorAttachments.size()) {
        rebuildTextureCompatCache();
    }
    if (attachmentIdx >= _colorTextures.size()) {
        return nullptr;
    }
    return _colorTextures[attachmentIdx].get();
}

Texture* IFrameBuffer::getDepthTexture() const
{
    if (_depthAttachment && !_depthTexture) {
        rebuildTextureCompatCache();
    }
    return _depthTexture.get();
}

Texture* IFrameBuffer::getResolveTexture() const
{
    if (_resolveAttachment && !_resolveTexture) {
        rebuildTextureCompatCache();
    }
    return _resolveTexture.get();
}


} // namespace ya

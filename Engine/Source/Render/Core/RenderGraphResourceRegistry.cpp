#include "RenderGraphResourceRegistry.h"

namespace ya
{

RenderImageDesc RenderGraphResourceRegistry::makeRenderImageDesc(const RGTextureDesc& desc)
{
    return RenderImageDesc{
        .image = ImageCreateInfo{
            .label       = desc.label,
            .format      = desc.format,
            .extent      = {.width = desc.extent.width, .height = desc.extent.height, .depth = desc.extent.depth},
            .mipLevels   = desc.mipLevels,
            .arrayLayers = desc.arrayLayers,
            .samples     = desc.samples,
            .usage       = desc.usage,
            .flags       = desc.flags,
        },
        .defaultView = makeDefaultViewDesc(desc),
    };
}

ImageViewCreateInfo RenderGraphResourceRegistry::makeDefaultViewDesc(const RGTextureDesc& desc)
{
    const bool bCube = desc.arrayLayers == 6;
    return ImageViewCreateInfo{
        .label          = std::format("{}.defaultView", desc.label),
        .viewType       = bCube ? EImageViewType::ViewCube : EImageViewType::View2D,
        .aspectFlags    = EFormat::isDepthStencilFormat(desc.format) ? EImageAspect::DepthStencil :
                          EFormat::isDepthFormat(desc.format) ? EImageAspect::Depth : EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = desc.mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = desc.arrayLayers,
    };
}

std::shared_ptr<RenderImage> RenderGraphResourceRegistry::createImportedTexture(const RGImportedTextureDesc& desc)
{
    auto image = desc.image;
    if (!image) {
        image = _factory.importImage(desc.importDesc);
    }
    if (!image) {
        YA_CORE_ERROR("Failed to import render graph texture '{}'", desc.importDesc.label);
        return nullptr;
    }

    auto view = _factory.createImageView(image, desc.viewDesc.value_or(makeDefaultViewDesc(desc.desc)));
    if (!view) {
        YA_CORE_ERROR("Failed to create imported render graph texture view '{}'", desc.importDesc.label);
        return nullptr;
    }

    auto resource         = std::make_shared<RenderImage>();
    resource->image       = std::move(image);
    resource->defaultView = std::move(view);
    return resource;
}

void RenderGraphResourceRegistry::sync(const RenderGraph& graph)
{
    for (const auto& texture : graph.getTextures()) {
        if (_textures.contains(texture.handle)) {
            continue;
        }

        if (texture.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(texture.imported.has_value(), "Imported render graph texture '{}' is missing import desc", texture.desc.label);
            _textures[texture.handle] = createImportedTexture(*texture.imported);
            continue;
        }

        _textures[texture.handle] = createRenderImage(_factory, makeRenderImageDesc(texture.desc));
    }

    for (const auto& buffer : graph.getBuffers()) {
        if (_ownedBuffers.contains(buffer.handle) || _importedBuffers.contains(buffer.handle)) {
            continue;
        }

        if (buffer.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(buffer.imported.has_value(), "Imported render graph buffer '{}' is missing import desc", buffer.desc.label);
            _importedBuffers[buffer.handle] = buffer.imported->buffer;
            continue;
        }

        _ownedBuffers[buffer.handle] = _factory.createBuffer(BufferCreateInfo{
            .label       = buffer.desc.label,
            .usage       = buffer.desc.usage,
            .size        = buffer.desc.size,
            .memoryUsage = buffer.desc.memoryUsage,
        });
    }
}

void RenderGraphResourceRegistry::clear()
{
    _textures.clear();
    _ownedBuffers.clear();
    _importedBuffers.clear();
}

const RenderImage* RenderGraphResourceRegistry::resolveTexture(RGTextureHandle handle) const
{
    const auto it = _textures.find(handle);
    return it != _textures.end() ? it->second.get() : nullptr;
}

IBuffer* RenderGraphResourceRegistry::resolveBuffer(RGBufferHandle handle) const
{
    if (const auto it = _ownedBuffers.find(handle); it != _ownedBuffers.end()) {
        return it->second.get();
    }
    if (const auto it = _importedBuffers.find(handle); it != _importedBuffers.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace ya

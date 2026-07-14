#include "RenderGraphResourceRegistry.h"

#include "Resource/DeferredDeletionQueue.h"

#include <unordered_set>

namespace ya
{

namespace
{

bool isSameTextureDesc(const RGTextureDesc& lhs, const RGTextureDesc& rhs)
{
    return lhs.label == rhs.label &&
           lhs.format == rhs.format &&
           lhs.extent.width == rhs.extent.width &&
           lhs.extent.height == rhs.extent.height &&
           lhs.extent.depth == rhs.extent.depth &&
           lhs.mipLevels == rhs.mipLevels &&
           lhs.arrayLayers == rhs.arrayLayers &&
           lhs.samples == rhs.samples &&
           lhs.usage == rhs.usage &&
           lhs.flags == rhs.flags;
}

bool isSameSubresourceRange(const ImageSubresourceRange& lhs, const ImageSubresourceRange& rhs)
{
    return lhs.aspectMask == rhs.aspectMask &&
           lhs.baseMipLevel == rhs.baseMipLevel &&
           lhs.levelCount == rhs.levelCount &&
           lhs.baseArrayLayer == rhs.baseArrayLayer &&
           lhs.layerCount == rhs.layerCount;
}

bool isSameImportedTextureDesc(const RGImportedTextureDesc& lhs, const RGImportedTextureDesc& rhs)
{
    const bool bSameSubresourceRange =
        lhs.subresourceRange.has_value() == rhs.subresourceRange.has_value() &&
        (!lhs.subresourceRange.has_value() || isSameSubresourceRange(*lhs.subresourceRange, *rhs.subresourceRange));

    const bool bSameViewDesc =
        lhs.viewDesc.has_value() == rhs.viewDesc.has_value() &&
        (!lhs.viewDesc.has_value() ||
         isSameImageViewDescKey(
             makeImageViewDescKey(*lhs.viewDesc),
             makeImageViewDescKey(*rhs.viewDesc)));

    return isSameTextureDesc(lhs.desc, rhs.desc) &&
           isSameImportedImageDesc(lhs.importDesc, rhs.importDesc) &&
           lhs.image.get() == rhs.image.get() &&
           lhs.imageView.get() == rhs.imageView.get() &&
           bSameSubresourceRange &&
           bSameViewDesc;
}

bool isSameBufferDesc(const RGBufferDesc& lhs, const RGBufferDesc& rhs)
{
    return lhs.label == rhs.label &&
           lhs.usage == rhs.usage &&
           lhs.size == rhs.size &&
           lhs.memoryUsage == rhs.memoryUsage;
}

bool isSameImportedBufferDesc(const RGImportedBufferDesc& lhs, const RGImportedBufferDesc& rhs)
{
    return isSameBufferDesc(lhs.desc, rhs.desc) &&
           lhs.buffer == rhs.buffer;
}

template <typename T>
void retireSharedResource(std::shared_ptr<T>& resource)
{
    if (!resource) {
        return;
    }

    auto& deletionQueue = DeferredDeletionQueue::get();
    if (deletionQueue.isInitialized()) {
        deletionQueue.retireResource(std::move(resource));
        return;
    }

    resource.reset();
}

} // namespace

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
    const bool bCube    = desc.arrayLayers == 6;
    const bool bArray2D = desc.arrayLayers > 1 && !bCube;
    return ImageViewCreateInfo{
        .label          = std::format("{}.defaultView", desc.label),
        .viewType       = bCube ? EImageViewType::ViewCube : bArray2D ? EImageViewType::View2DArray : EImageViewType::View2D,
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

    auto view = desc.imageView;
    if (view) {
        YA_CORE_ASSERT(view->getImage() == image.get(),
                       "Imported render graph texture '{}' provided an image view that does not reference the imported image",
                       desc.importDesc.label);
    }
    else {
        view = _factory.createImageView(image, desc.viewDesc.value_or(makeDefaultViewDesc(desc.desc)));
    }
    if (!view) {
        YA_CORE_ERROR("Failed to create imported render graph texture view '{}'", desc.importDesc.label);
        return nullptr;
    }

    auto resource         = std::make_shared<RenderImage>();
    resource->image       = std::move(image);
    resource->defaultView = std::move(view);
    resource->retainedResources = desc.retainedResources;
    return resource;
}

void RenderGraphResourceRegistry::pruneUnusedResources(const RenderGraph& graph)
{
    std::unordered_set<RGTextureHandle> liveTextures;
    liveTextures.reserve(graph.getTextures().size());
    for (const auto& texture : graph.getTextures()) {
        liveTextures.insert(texture.handle);
    }

    for (auto it = _textures.begin(); it != _textures.end();) {
        if (!liveTextures.contains(it->first)) {
            retireSharedResource(it->second.resource);
            it = _textures.erase(it);
        }
        else {
            ++it;
        }
    }

    std::unordered_set<RGBufferHandle> liveBuffers;
    liveBuffers.reserve(graph.getBuffers().size());
    for (const auto& buffer : graph.getBuffers()) {
        liveBuffers.insert(buffer.handle);
    }

    for (auto it = _ownedBuffers.begin(); it != _ownedBuffers.end();) {
        if (!liveBuffers.contains(it->first)) {
            retireSharedResource(it->second.resource);
            it = _ownedBuffers.erase(it);
        }
        else {
            ++it;
        }
    }

    for (auto it = _importedBuffers.begin(); it != _importedBuffers.end();) {
        if (!liveBuffers.contains(it->first)) {
            it = _importedBuffers.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool RenderGraphResourceRegistry::needsTextureReplacement(const TextureEntry& entry, const RGTextureResource& resource)
{
    if (!isSameTextureDesc(entry.desc, resource.desc)) {
        return true;
    }

    if (resource.lifetime == ERGResourceLifetime::Imported) {
        if (!resource.imported.has_value() || !entry.imported.has_value()) {
            return true;
        }
        return !isSameImportedTextureDesc(*entry.imported, *resource.imported);
    }

    return entry.imported.has_value();
}

bool RenderGraphResourceRegistry::needsOwnedBufferReplacement(const OwnedBufferEntry& entry, const RGBufferResource& resource)
{
    return !isSameBufferDesc(entry.desc, resource.desc);
}

bool RenderGraphResourceRegistry::needsImportedBufferReplacement(const ImportedBufferEntry& entry, const RGBufferResource& resource)
{
    if (resource.lifetime != ERGResourceLifetime::Imported || !resource.imported.has_value() || !entry.imported.has_value()) {
        return true;
    }
    return !isSameImportedBufferDesc(*entry.imported, *resource.imported);
}

void RenderGraphResourceRegistry::sync(const RenderGraph& graph)
{
    pruneUnusedResources(graph);

    for (const auto& texture : graph.getTextures()) {
        const auto existing = _textures.find(texture.handle);
        if (existing != _textures.end() && !needsTextureReplacement(existing->second, texture)) {
            if (texture.lifetime == ERGResourceLifetime::Imported) {
                existing->second.imported = texture.imported;
                if (existing->second.resource) {
                    existing->second.resource->retainedResources =
                        texture.imported ? texture.imported->retainedResources : std::vector<std::shared_ptr<void>>{};
                }
            }
            continue;
        }
        if (existing != _textures.end()) {
            retireSharedResource(existing->second.resource);
        }

        if (texture.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(texture.imported.has_value(), "Imported render graph texture '{}' is missing import desc", texture.desc.label);
            _textures[texture.handle] = TextureEntry{
                .resource = createImportedTexture(*texture.imported),
                .desc = texture.desc,
                .imported = texture.imported,
            };
            continue;
        }

        _textures[texture.handle] = TextureEntry{
            .resource = createRenderImage(_factory, makeRenderImageDesc(texture.desc)),
            .desc = texture.desc,
        };
    }

    for (const auto& buffer : graph.getBuffers()) {
        if (buffer.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(buffer.imported.has_value(), "Imported render graph buffer '{}' is missing import desc", buffer.desc.label);
            if (const auto owned = _ownedBuffers.find(buffer.handle); owned != _ownedBuffers.end()) {
                retireSharedResource(owned->second.resource);
            }
            _ownedBuffers.erase(buffer.handle);

            const auto existing = _importedBuffers.find(buffer.handle);
            if (existing != _importedBuffers.end() && !needsImportedBufferReplacement(existing->second, buffer)) {
                continue;
            }

            _importedBuffers[buffer.handle] = ImportedBufferEntry{
                .resource = buffer.imported->buffer,
                .imported = buffer.imported,
            };
            continue;
        }

        _importedBuffers.erase(buffer.handle);

        const auto existing = _ownedBuffers.find(buffer.handle);
        if (existing != _ownedBuffers.end() && !needsOwnedBufferReplacement(existing->second, buffer)) {
            continue;
        }
        if (existing != _ownedBuffers.end()) {
            retireSharedResource(existing->second.resource);
        }

        _ownedBuffers[buffer.handle] = OwnedBufferEntry{
            .resource = _factory.createBuffer(BufferCreateInfo{
                .label       = buffer.desc.label,
                .usage       = buffer.desc.usage,
                .size        = buffer.desc.size,
                .memoryUsage = buffer.desc.memoryUsage,
            }),
            .desc = buffer.desc,
        };
    }
}

void RenderGraphResourceRegistry::clear()
{
    for (auto& [handle, texture] : _textures) {
        (void)handle;
        retireSharedResource(texture.resource);
    }
    for (auto& [handle, buffer] : _ownedBuffers) {
        (void)handle;
        retireSharedResource(buffer.resource);
    }
    _textures.clear();
    _ownedBuffers.clear();
    _importedBuffers.clear();
}

const RenderImage* RenderGraphResourceRegistry::resolveTexture(RGTextureHandle handle) const
{
    const auto it = _textures.find(handle);
    return it != _textures.end() ? it->second.resource.get() : nullptr;
}

IBuffer* RenderGraphResourceRegistry::resolveBuffer(RGBufferHandle handle) const
{
    if (const auto it = _ownedBuffers.find(handle); it != _ownedBuffers.end()) {
        return it->second.resource.get();
    }
    if (const auto it = _importedBuffers.find(handle); it != _importedBuffers.end()) {
        return it->second.resource;
    }
    return nullptr;
}

} // namespace ya

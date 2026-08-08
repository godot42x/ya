#include "RenderGraphResourceRegistry.h"

#include "Core/Common/DeferredDeletionQueue.h"

#include <unordered_set>

namespace ya
{

namespace
{

std::string makePersistentTextureKeyValue(const RGPersistentTextureKey& key)
{
    return key.value;
}

std::string makePersistentBufferKeyValue(const RGPersistentBufferKey& key)
{
    return key.value;
}

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

    const bool bSameImportedImageIdentitySansLayout =
        lhs.importDesc.label == rhs.importDesc.label &&
        lhs.importDesc.nativeHandle == rhs.importDesc.nativeHandle &&
        lhs.importDesc.format == rhs.importDesc.format &&
        lhs.importDesc.usage == rhs.importDesc.usage &&
        lhs.importDesc.extent.width == rhs.importDesc.extent.width &&
        lhs.importDesc.extent.height == rhs.importDesc.extent.height &&
        lhs.importDesc.extent.depth == rhs.importDesc.extent.depth &&
        lhs.importDesc.mipLevels == rhs.importDesc.mipLevels &&
        lhs.importDesc.arrayLayers == rhs.importDesc.arrayLayers &&
        lhs.importDesc.ownership == rhs.importDesc.ownership;

    const bool bSameLayoutContract =
        lhs.importDesc.initialLayout == rhs.importDesc.initialLayout &&
        lhs.importDesc.finalLayout == rhs.importDesc.finalLayout;

    const bool bSameSharedImageBackedImport =
        lhs.image != nullptr &&
        rhs.image != nullptr &&
        lhs.image.get() == rhs.image.get();

    return isSameTextureDesc(lhs.desc, rhs.desc) &&
           bSameImportedImageIdentitySansLayout &&
           lhs.image.get() == rhs.image.get() &&
           lhs.imageView.get() == rhs.imageView.get() &&
           bSameSubresourceRange &&
           bSameViewDesc &&
           (bSameSharedImageBackedImport || bSameLayoutContract);
}

bool isSameBufferDesc(const RGBufferDesc& lhs, const RGBufferDesc& rhs)
{
    return lhs.label == rhs.label &&
           lhs.usage == rhs.usage &&
           lhs.size == rhs.size &&
           lhs.memoryUsage == rhs.memoryUsage &&
           lhs.alignment == rhs.alignment;
}

bool hasRequestedBufferUsage(EBufferUsage value, EBufferUsage required)
{
    return (value & required) == required;
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

void retireRetainedResources(std::vector<std::shared_ptr<void>>& retainedResources)
{
    if (retainedResources.empty()) {
        return;
    }

    auto& deletionQueue = DeferredDeletionQueue::get();
    if (deletionQueue.isInitialized()) {
        deletionQueue.enqueue(deletionQueue.currentFrame(), [captured = std::move(retainedResources)]() mutable {
            captured.clear();
        });
        return;
    }

    retainedResources.clear();
}

bool isSameRetainedResources(const std::vector<std::shared_ptr<void>>& lhs,
                             const std::vector<std::shared_ptr<void>>& rhs)
{
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

void refreshRetainedResources(std::vector<std::shared_ptr<void>>& currentRetainedResources,
                              const std::vector<std::shared_ptr<void>>& nextRetainedResources)
{
    if (isSameRetainedResources(currentRetainedResources, nextRetainedResources)) {
        return;
    }

    auto retiredResources = std::move(currentRetainedResources);
    currentRetainedResources = nextRetainedResources;
    retireRetainedResources(retiredResources);
}

} // namespace

void RenderGraphResourceRegistry::releaseTextureBinding(std::shared_ptr<TextureEntry>& entry)
{
    if (!entry) {
        return;
    }
    if (!entry->persistentKey.has_value() && !entry->pooledTransient) {
        retireSharedResource(entry->resource);
    }
    entry.reset();
}

void RenderGraphResourceRegistry::releaseOwnedBufferBinding(std::shared_ptr<OwnedBufferEntry>& entry)
{
    if (!entry) {
        return;
    }
    if (!entry->persistentKey.has_value() && !entry->pooledTransient) {
        retireSharedResource(entry->resource);
    }
    entry.reset();
}

bool RenderGraphResourceRegistry::canReuseTransientSlot(const OwnedBufferEntry& entry,
                                                        const RGTransientBufferSlotPlan& slot)
{
    return entry.pooledTransient &&
           entry.resource != nullptr &&
           entry.desc.memoryUsage == slot.desc.memoryUsage &&
           entry.desc.size >= slot.desc.size &&
           entry.desc.alignment >= slot.desc.alignment &&
           hasRequestedBufferUsage(entry.desc.usage, slot.desc.usage);
}

bool RenderGraphResourceRegistry::canReuseTransientTexture(const TextureEntry& entry,
                                                           const RGTextureDesc& desc)
{
    if (!entry.pooledTransient || !entry.resource || !entry.resource->isValid()) {
        return false;
    }

    const auto& allocation = entry.allocationDesc;
    return allocation.format == desc.format &&
           allocation.extent.width == desc.extent.width &&
           allocation.extent.height == desc.extent.height &&
           allocation.extent.depth == desc.extent.depth &&
           allocation.mipLevels == desc.mipLevels &&
           allocation.arrayLayers == desc.arrayLayers &&
           allocation.samples == desc.samples &&
           allocation.flags == desc.flags &&
           (allocation.usage & desc.usage) == desc.usage;
}

std::shared_ptr<RenderGraphResourceRegistry::TextureEntry> RenderGraphResourceRegistry::acquireTransientTexture(
    const RGTextureDesc& desc,
    std::unordered_set<TextureEntry*>& usedPoolEntries)
{
    for (const auto& entry : _transientTexturePool) {
        if (!entry || usedPoolEntries.contains(entry.get()) || !canReuseTransientTexture(*entry, desc)) {
            continue;
        }
        usedPoolEntries.insert(entry.get());
        return entry;
    }

    auto entry = std::make_shared<TextureEntry>(TextureEntry{
        .resource        = createRenderImage(_factory, makeRenderImageDesc(desc)),
        .desc            = desc,
        .allocationDesc  = desc,
        .pooledTransient = true,
    });
    YA_CORE_ASSERT(entry->resource != nullptr && entry->resource->isValid(),
                   "RenderGraph registry failed to create transient texture '{}'",
                   desc.label);
    _transientTexturePool.push_back(entry);
    usedPoolEntries.insert(entry.get());
    return entry;
}

std::shared_ptr<RenderGraphResourceRegistry::OwnedBufferEntry> RenderGraphResourceRegistry::acquireTransientSlot(
    const RGTransientBufferSlotPlan& slot,
    std::unordered_set<OwnedBufferEntry*>& usedPoolEntries)
{
    for (const auto& entry : _transientBufferPool) {
        if (!entry || usedPoolEntries.contains(entry.get()) || !canReuseTransientSlot(*entry, slot)) {
            continue;
        }
        usedPoolEntries.insert(entry.get());
        ++_transientPoolDiagnostics.lastHitCount;
        ++_transientPoolDiagnostics.totalHitCount;
        return entry;
    }

    auto entry = std::make_shared<OwnedBufferEntry>(OwnedBufferEntry{
        .resource = _factory.createBuffer(BufferCreateInfo{
            .label       = slot.desc.label,
            .usage       = slot.desc.usage,
            .size        = slot.desc.size,
            .memoryUsage = slot.desc.memoryUsage,
        }),
        .desc           = slot.desc,
        .pooledTransient = true,
    });
    YA_CORE_ASSERT(entry->resource != nullptr,
                   "RenderGraph registry failed to create transient buffer slot '{}'",
                   slot.desc.label);
    _transientBufferPool.push_back(entry);
    usedPoolEntries.insert(entry.get());
    ++_transientPoolDiagnostics.lastMissCount;
    ++_transientPoolDiagnostics.totalMissCount;
    return entry;
}

void RenderGraphResourceRegistry::materializeTransientSlots(const RenderGraph& graph,
                                                             const RGCompiledGraph& compiled)
{
    std::unordered_set<OwnedBufferEntry*> usedPoolEntries;
    usedPoolEntries.reserve(compiled.transientBufferSlots.size());

    for (const auto& slot : compiled.transientBufferSlots) {
        const auto entry = acquireTransientSlot(slot, usedPoolEntries);
        for (const auto handle : slot.buffers) {
            const auto* resource = graph.getBuffer(handle);
            YA_CORE_ASSERT(resource != nullptr && resource->lifetime == ERGResourceLifetime::Transient,
                           "RenderGraph transient slot {} references an invalid logical buffer {}",
                           slot.slotIndex,
                           handle.index);

            if (const auto existing = _ownedBuffers.find(handle);
                existing != _ownedBuffers.end() && existing->second != entry) {
                existing->second.reset();
            }
            _ownedBuffers[handle] = entry;
        }
    }
}

RenderGraphResourceRegistry::~RenderGraphResourceRegistry()
{
    clear();
}

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
            YA_CORE_TRACE("RenderGraph registry pruning texture '{}' (handle={}:{})",
                          it->second->desc.label,
                          it->first.index,
                          it->first.generation);
            releaseTextureBinding(it->second);
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
            releaseOwnedBufferBinding(it->second);
            it = _ownedBuffers.erase(it);
        }
        else {
            ++it;
        }
    }

    for (auto it = _importedBuffers.begin(); it != _importedBuffers.end();) {
        if (!liveBuffers.contains(it->first)) {
            if (it->second.imported.has_value()) {
                retireRetainedResources(it->second.imported->retainedResources);
            }
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

void RenderGraphResourceRegistry::sync(const RenderGraph& graph, const RGCompiledGraph* compiled)
{
    _transientPoolDiagnostics.lastHitCount = 0;
    _transientPoolDiagnostics.lastMissCount = 0;
    pruneUnusedResources(graph);

    std::unordered_set<TextureEntry*> usedTransientTextureEntries;
    usedTransientTextureEntries.reserve(graph.getTextures().size());

    for (const auto& texture : graph.getTextures()) {
        if (texture.lifetime == ERGResourceLifetime::Persistent) {
            YA_CORE_ASSERT(texture.persistentKey.has_value(),
                           "Persistent render graph texture '{}' is missing stable key",
                           texture.desc.label);
            const auto key = makePersistentTextureKeyValue(*texture.persistentKey);
            auto& persistentEntry = _persistentTextures[key];
            if (!persistentEntry) {
                persistentEntry = std::make_shared<TextureEntry>();
                persistentEntry->persistentKey = texture.persistentKey;
            }
            if (!persistentEntry->resource || needsTextureReplacement(*persistentEntry, texture)) {
                if (persistentEntry->resource) {
                    YA_CORE_TRACE("RenderGraph registry replacing persistent texture '{}' (key={})",
                                  texture.desc.label,
                                  key);
                    retireSharedResource(persistentEntry->resource);
                }
                persistentEntry->resource = createRenderImage(_factory, makeRenderImageDesc(texture.desc));
                persistentEntry->desc = texture.desc;
                persistentEntry->allocationDesc = texture.desc;
                persistentEntry->imported.reset();
                persistentEntry->pooledTransient = false;
            }

            if (auto existing = _textures.find(texture.handle); existing != _textures.end() &&
                existing->second != persistentEntry) {
                releaseTextureBinding(existing->second);
            }
            _textures[texture.handle] = persistentEntry;
            continue;
        }

        const auto existing = _textures.find(texture.handle);
        if (existing != _textures.end() && !needsTextureReplacement(*existing->second, texture)) {
            if (texture.lifetime == ERGResourceLifetime::Transient && existing->second->pooledTransient) {
                usedTransientTextureEntries.insert(existing->second.get());
            }
            if (texture.lifetime == ERGResourceLifetime::Imported) {
                if (existing->second->imported.has_value()) {
                    refreshRetainedResources(existing->second->imported->retainedResources,
                                             texture.imported ? texture.imported->retainedResources : std::vector<std::shared_ptr<void>>{});
                }
                existing->second->imported = texture.imported;
                if (existing->second->resource) {
                    refreshRetainedResources(existing->second->resource->retainedResources,
                                             texture.imported ? texture.imported->retainedResources : std::vector<std::shared_ptr<void>>{});
                }
            }
            continue;
        }
        if (existing != _textures.end()) {
            YA_CORE_TRACE("RenderGraph registry replacing texture '{}' (handle={}:{})",
                          texture.desc.label,
                          texture.handle.index,
                          texture.handle.generation);
            releaseTextureBinding(existing->second);
        }

        if (texture.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(texture.imported.has_value(), "Imported render graph texture '{}' is missing import desc", texture.desc.label);
            _textures[texture.handle] = std::make_shared<TextureEntry>(TextureEntry{
                .resource        = createImportedTexture(*texture.imported),
                .desc            = texture.desc,
                .allocationDesc  = texture.desc,
                .imported        = texture.imported,
                .pooledTransient = false,
            });
            continue;
        }

        auto transientEntry = acquireTransientTexture(texture.desc, usedTransientTextureEntries);
        transientEntry->desc = texture.desc;
        _textures[texture.handle] = std::move(transientEntry);
    }

    if (compiled != nullptr) {
        YA_CORE_ASSERT(compiled->isValid(), "RenderGraph registry cannot materialize an invalid compiled graph");
        materializeTransientSlots(graph, *compiled);
    }
    _transientPoolDiagnostics.poolEntryCount = static_cast<uint32_t>(_transientBufferPool.size());

    for (const auto& buffer : graph.getBuffers()) {
        if (compiled != nullptr && buffer.lifetime == ERGResourceLifetime::Transient) {
            continue;
        }
        if (buffer.lifetime == ERGResourceLifetime::Persistent) {
            YA_CORE_ASSERT(buffer.persistentKey.has_value(),
                           "Persistent render graph buffer '{}' is missing stable key",
                           buffer.desc.label);
            const auto key = makePersistentBufferKeyValue(*buffer.persistentKey);
            auto& persistentEntry = _persistentOwnedBuffers[key];
            if (!persistentEntry) {
                persistentEntry = std::make_shared<OwnedBufferEntry>();
                persistentEntry->persistentKey = buffer.persistentKey;
            }
            if (!persistentEntry->resource || needsOwnedBufferReplacement(*persistentEntry, buffer)) {
                if (persistentEntry->resource) {
                    YA_CORE_TRACE("RenderGraph registry replacing persistent buffer '{}' (key={})",
                                  buffer.desc.label,
                                  key);
                    retireSharedResource(persistentEntry->resource);
                }
                persistentEntry->resource = _factory.createBuffer(BufferCreateInfo{
                    .label       = buffer.desc.label,
                    .usage       = buffer.desc.usage,
                    .size        = buffer.desc.size,
                    .memoryUsage = buffer.desc.memoryUsage,
                });
                persistentEntry->desc = buffer.desc;
            }

            if (const auto imported = _importedBuffers.find(buffer.handle);
                imported != _importedBuffers.end() && imported->second.imported.has_value()) {
                retireRetainedResources(imported->second.imported->retainedResources);
            }
            _importedBuffers.erase(buffer.handle);

            if (const auto existing = _ownedBuffers.find(buffer.handle);
                existing != _ownedBuffers.end() && existing->second != persistentEntry) {
                releaseOwnedBufferBinding(existing->second);
            }
            _ownedBuffers[buffer.handle] = persistentEntry;
            continue;
        }

        if (buffer.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(buffer.imported.has_value(), "Imported render graph buffer '{}' is missing import desc", buffer.desc.label);
            if (const auto owned = _ownedBuffers.find(buffer.handle); owned != _ownedBuffers.end()) {
                releaseOwnedBufferBinding(owned->second);
            }
            _ownedBuffers.erase(buffer.handle);

            const auto existing = _importedBuffers.find(buffer.handle);
            if (existing != _importedBuffers.end() && !needsImportedBufferReplacement(existing->second, buffer)) {
                if (existing->second.imported.has_value()) {
                    refreshRetainedResources(existing->second.imported->retainedResources,
                                             buffer.imported ? buffer.imported->retainedResources : std::vector<std::shared_ptr<void>>{});
                }
                existing->second.imported = buffer.imported;
                continue;
            }
            if (existing != _importedBuffers.end() && existing->second.imported.has_value()) {
                retireRetainedResources(existing->second.imported->retainedResources);
            }

            _importedBuffers[buffer.handle] = ImportedBufferEntry{
                .resource = buffer.imported->buffer,
                .imported = buffer.imported,
            };
            continue;
        }

        if (const auto imported = _importedBuffers.find(buffer.handle);
            imported != _importedBuffers.end() && imported->second.imported.has_value()) {
            retireRetainedResources(imported->second.imported->retainedResources);
        }
        _importedBuffers.erase(buffer.handle);

        const auto existing = _ownedBuffers.find(buffer.handle);
        if (existing != _ownedBuffers.end() && !needsOwnedBufferReplacement(*existing->second, buffer)) {
            continue;
        }
        if (existing != _ownedBuffers.end()) {
            releaseOwnedBufferBinding(existing->second);
        }

        _ownedBuffers[buffer.handle] = std::make_shared<OwnedBufferEntry>(OwnedBufferEntry{
            .resource = _factory.createBuffer(BufferCreateInfo{
                .label       = buffer.desc.label,
                .usage       = buffer.desc.usage,
                .size        = buffer.desc.size,
                .memoryUsage = buffer.desc.memoryUsage,
            }),
            .desc = buffer.desc,
        });
    }
}

void RenderGraphResourceRegistry::clear()
{
    for (auto& [key, texture] : _persistentTextures) {
        (void)key;
        retireSharedResource(texture->resource);
    }
    for (auto& [key, buffer] : _persistentOwnedBuffers) {
        (void)key;
        retireSharedResource(buffer->resource);
    }
    for (auto& buffer : _transientBufferPool) {
        retireSharedResource(buffer->resource);
    }
    for (auto& texture : _transientTexturePool) {
        retireSharedResource(texture->resource);
    }
    for (auto& [handle, texture] : _textures) {
        (void)handle;
        releaseTextureBinding(texture);
    }
    for (auto& [handle, buffer] : _ownedBuffers) {
        (void)handle;
        releaseOwnedBufferBinding(buffer);
    }
    _persistentTextures.clear();
    _persistentOwnedBuffers.clear();
    _transientTexturePool.clear();
    _transientBufferPool.clear();
    _transientPoolDiagnostics = {};
    _textures.clear();
    _ownedBuffers.clear();
    for (auto& [handle, buffer] : _importedBuffers) {
        (void)handle;
        if (buffer.imported.has_value()) {
            retireRetainedResources(buffer.imported->retainedResources);
        }
    }
    _importedBuffers.clear();
}

const RenderImage* RenderGraphResourceRegistry::resolveTexture(RGTextureHandle handle) const
{
    const auto it = _textures.find(handle);
    return it != _textures.end() && it->second ? it->second->resource.get() : nullptr;
}

std::shared_ptr<RenderImage> RenderGraphResourceRegistry::resolveTextureShared(RGTextureHandle handle) const
{
    const auto it = _textures.find(handle);
    return it != _textures.end() && it->second ? it->second->resource : nullptr;
}

IBuffer* RenderGraphResourceRegistry::resolveBuffer(RGBufferHandle handle) const
{
    if (const auto it = _ownedBuffers.find(handle); it != _ownedBuffers.end()) {
        return it->second ? it->second->resource.get() : nullptr;
    }
    if (const auto it = _importedBuffers.find(handle); it != _importedBuffers.end()) {
        return it->second.resource;
    }
    return nullptr;
}

} // namespace ya

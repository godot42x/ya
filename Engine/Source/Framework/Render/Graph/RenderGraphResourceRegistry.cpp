#include "Graph/RenderGraphResourceRegistry.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"

#include <atomic>
#include <unordered_set>
#include <utility>

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

bool isSameRetainedResources(const std::vector<RetainedResource>& lhs,
                             const std::vector<RetainedResource>& rhs);

/// Import replacement is decided by the stable pieces only; Wrapper and Retained
/// bits are reported for diagnostics and never force a replacement.
uint32_t diffImportedTextureDesc(const RGImportedTextureDesc& lhs, const RGImportedTextureDesc& rhs, bool& outReplace)
{
    const bool bSameSubresourceRange =
        lhs.subresourceRange.has_value() == rhs.subresourceRange.has_value() &&
        (!lhs.subresourceRange.has_value() || isSameSubresourceRange(*lhs.subresourceRange, *rhs.subresourceRange));

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

    // Import identity is the *underlying* image/view, not the ImageResource
    // wrapper: callers clone a fresh wrapper per frame (cloneImageResourceWithView),
    // so comparing wrapper pointers would defeat cross-frame reuse and force a
    // per-frame rebuild of every imported texture. Compare the stable underlying
    // image/view identities instead (this restores the semantics that existed
    // before the imports/exports-to-ImageResource convergence).
    const auto lhsImage = lhs.resource ? lhs.resource->getImageShared() : nullptr;
    const auto rhsImage = rhs.resource ? rhs.resource->getImageShared() : nullptr;
    const auto lhsView  = lhs.resource ? lhs.resource->getImageViewShared() : nullptr;
    const auto rhsView  = rhs.resource ? rhs.resource->getImageViewShared() : nullptr;
    const bool bSameUnderlyingImage = lhsImage == rhsImage;
    const bool bSameUnderlyingView  = lhsView == rhsView;
    const bool bSameSharedImageBackedImport = bSameUnderlyingImage && lhsImage != nullptr;
    const bool bSameDesc = isSameTextureDesc(lhs.desc, rhs.desc);

    outReplace = !(bSameDesc &&
                   bSameImportedImageIdentitySansLayout &&
                   bSameUnderlyingImage &&
                   bSameUnderlyingView &&
                   bSameSubresourceRange &&
                   (bSameSharedImageBackedImport || bSameLayoutContract));

    uint32_t changes = ERGImportChange::None;
    changes |= bSameDesc ? 0u : ERGImportChange::Desc;
    changes |= bSameImportedImageIdentitySansLayout ? 0u : ERGImportChange::ImportIdentity;
    changes |= bSameUnderlyingImage ? 0u : ERGImportChange::Image;
    changes |= bSameUnderlyingView ? 0u : ERGImportChange::View;
    changes |= bSameSubresourceRange ? 0u : ERGImportChange::Subresource;
    changes |= bSameLayoutContract ? 0u : ERGImportChange::LayoutContract;
    changes |= lhs.resource == rhs.resource ? 0u : ERGImportChange::Wrapper;
    changes |= isSameRetainedResources(lhs.retainedResources, rhs.retainedResources) ? 0u : ERGImportChange::Retained;
    return changes;
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

uint32_t diffImportedBufferDesc(const RGImportedBufferDesc& lhs, const RGImportedBufferDesc& rhs, bool& outReplace)
{
    const bool bSameDesc   = isSameBufferDesc(lhs.desc, rhs.desc);
    const bool bSameBuffer = lhs.buffer == rhs.buffer;
    outReplace             = !(bSameDesc && bSameBuffer);

    uint32_t changes = ERGImportChange::None;
    changes |= bSameDesc ? 0u : ERGImportChange::Desc;
    changes |= bSameBuffer ? 0u : ERGImportChange::Buffer;
    changes |= isSameRetainedResources(lhs.retainedResources, rhs.retainedResources) ? 0u : ERGImportChange::Retained;
    return changes;
}

template <typename T>
void retireSharedResource(std::shared_ptr<T>& resource)
{
    if (!resource) {
        return;
    }

    DeferredDeletionQueue::get().retire(std::move(resource));
}

void retireRetainedResources(std::vector<RetainedResource>& retainedResources)
{
    if (retainedResources.empty()) {
        return;
    }

    DeferredDeletionQueue::get().retireContainer(std::move(retainedResources));
}

bool isSameRetainedResources(const std::vector<RetainedResource>& lhs,
                             const std::vector<RetainedResource>& rhs)
{
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](const RetainedResource& a, const RetainedResource& b) {
                          return a.resource == b.resource;
                      });
}

/// Returns true when the old keep-alive list was retired.
bool refreshRetainedResources(std::vector<RetainedResource>& currentRetainedResources,
                              const std::vector<RetainedResource>& nextRetainedResources)
{
    if (isSameRetainedResources(currentRetainedResources, nextRetainedResources)) {
        return false;
    }

    auto retiredResources = std::move(currentRetainedResources);
    currentRetainedResources = nextRetainedResources;
    retireRetainedResources(retiredResources);
    return true;
}

std::atomic<bool> gRenderGraphTraceEnabled{false};

} // namespace

namespace render_graph_trace
{

void setEnabled(bool bEnabled)
{
    gRenderGraphTraceEnabled.store(bEnabled, std::memory_order_relaxed);
}

bool isEnabled()
{
    return gRenderGraphTraceEnabled.load(std::memory_order_relaxed);
}

std::string_view toString(ERGRegistryEvent kind)
{
    switch (kind) {
    case ERGRegistryEvent::ImportBound:
        return "ImportBound";
    case ERGRegistryEvent::ImportReplaced:
        return "ImportReplaced";
    case ERGRegistryEvent::RetainedRefreshed:
        return "RetainedRefreshed";
    case ERGRegistryEvent::ImportPruned:
        return "ImportPruned";
    case ERGRegistryEvent::OwnedBufferCreated:
        return "OwnedBufferCreated";
    case ERGRegistryEvent::OwnedBufferReplaced:
        return "OwnedBufferReplaced";
    case ERGRegistryEvent::OwnedBufferPruned:
        return "OwnedBufferPruned";
    case ERGRegistryEvent::TransientTextureAllocated:
        return "TransientTextureAllocated";
    case ERGRegistryEvent::TransientTextureEvicted:
        return "TransientTextureEvicted";
    case ERGRegistryEvent::TransientBufferSlotAllocated:
        return "TransientBufferSlotAllocated";
    }
    return "Unknown";
}

std::string describeChanges(uint32_t changes)
{
    if (changes == ERGImportChange::None) {
        return "none";
    }

    constexpr std::pair<uint32_t, std::string_view> names[] = {
        {ERGImportChange::Desc, "desc"},
        {ERGImportChange::ImportIdentity, "import-identity"},
        {ERGImportChange::Image, "image"},
        {ERGImportChange::View, "view"},
        {ERGImportChange::Subresource, "subresource"},
        {ERGImportChange::LayoutContract, "layout"},
        {ERGImportChange::Buffer, "buffer"},
        {ERGImportChange::Wrapper, "wrapper"},
        {ERGImportChange::Retained, "retained"},
        {ERGImportChange::Lifetime, "lifetime"},
    };

    std::string text;
    for (const auto& [bit, name] : names) {
        if ((changes & bit) == 0) {
            continue;
        }
        if (!text.empty()) {
            text += '|';
        }
        text += name;
    }
    return text;
}

} // namespace render_graph_trace

void RenderGraphResourceRegistry::recordEvent(ERGRegistryEvent kind,
                                              bool             bTexture,
                                              uint32_t         handleIndex,
                                              uint32_t         changes,
                                              bool             bRegistryOwnsGpuObject,
                                              uint64_t         scope,
                                              std::string_view label)
{
    switch (kind) {
    case ERGRegistryEvent::ImportBound:
        ++_lastSyncStats.importBound;
        break;
    case ERGRegistryEvent::ImportReplaced:
        ++_lastSyncStats.importReplaced;
        break;
    case ERGRegistryEvent::RetainedRefreshed:
        ++_lastSyncStats.retainedRefreshed;
        break;
    case ERGRegistryEvent::ImportPruned:
        ++_lastSyncStats.importPruned;
        break;
    case ERGRegistryEvent::OwnedBufferCreated:
    case ERGRegistryEvent::TransientTextureAllocated:
    case ERGRegistryEvent::TransientBufferSlotAllocated:
        ++_lastSyncStats.registryObjectsCreated;
        break;
    case ERGRegistryEvent::OwnedBufferReplaced:
        ++_lastSyncStats.registryObjectsCreated;
        ++_lastSyncStats.registryObjectsRetired;
        break;
    case ERGRegistryEvent::OwnedBufferPruned:
    case ERGRegistryEvent::TransientTextureEvicted:
        ++_lastSyncStats.registryObjectsRetired;
        break;
    }

    if (!render_graph_trace::isEnabled()) {
        return;
    }
    _lastSyncEvents.push_back(RGRegistryEventRecord{
        .kind                   = kind,
        .bTexture               = bTexture,
        .handleIndex            = handleIndex,
        .changes                = changes,
        .bRegistryOwnsGpuObject = bRegistryOwnsGpuObject,
        .scope                  = scope,
        .label                  = std::string(label),
    });
}

void RenderGraphResourceRegistry::logSyncEvents() const
{
    if (_lastSyncEvents.empty()) {
        return;
    }

    const auto& stats = _lastSyncStats;
    YA_CORE_INFO("[RGTrace] registry '{}' sync#{}: bound={} replaced={} retained={} pruned={} rebound={} "
                 "registryCreated={} registryRetired={}",
                 _debugName,
                 stats.syncSerial,
                 stats.importBound,
                 stats.importReplaced,
                 stats.retainedRefreshed,
                 stats.importPruned,
                 stats.importRebound,
                 stats.registryObjectsCreated,
                 stats.registryObjectsRetired);
    for (const auto& event : _lastSyncEvents) {
        YA_CORE_INFO("[RGTrace]   {} {} '{}' scope={} handle={} changes={} registryOwned={}",
                     render_graph_trace::toString(event.kind),
                     event.bTexture ? "texture" : "buffer",
                     event.label,
                     event.scope,
                     event.handleIndex,
                     render_graph_trace::describeChanges(event.changes),
                     event.bRegistryOwnsGpuObject ? "yes" : "no");
    }
}

void RenderGraphResourceRegistry::releaseTextureBinding(std::shared_ptr<TextureEntry>& entry)
{
    if (!entry) {
        return;
    }
    if (!entry->pooledTransient) {
        retireSharedResource(entry->resource);
    }
    entry.reset();
}

void RenderGraphResourceRegistry::releaseOwnedBufferBinding(std::shared_ptr<OwnedBufferEntry>& entry)
{
    if (!entry) {
        return;
    }
    if (!entry->pooledTransient) {
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
        entry->lastUsedSync = _lastSyncStats.syncSerial;
        return entry;
    }

    auto entry = std::make_shared<TextureEntry>(TextureEntry{
        .resource        = RenderTexture::adopt(createImageResource(_factory, makeImageResourceDesc(desc))),
        .desc            = desc,
        .allocationDesc  = desc,
        .pooledTransient = true,
        .lastUsedSync    = _lastSyncStats.syncSerial,
    });
    YA_CORE_ASSERT(entry->resource != nullptr && entry->resource->isValid(),
                   "RenderGraph registry failed to create transient texture '{}'",
                   desc.label);
    _transientTexturePool.push_back(entry);
    usedPoolEntries.insert(entry.get());
    recordEvent(ERGRegistryEvent::TransientTextureAllocated, true, 0, ERGImportChange::None, true, 0, desc.label);
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
    recordEvent(ERGRegistryEvent::TransientBufferSlotAllocated, false, slot.slotIndex, ERGImportChange::None, true, 0, slot.desc.label);
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

ImageResourceDesc RenderGraphResourceRegistry::makeImageResourceDesc(const RGTextureDesc& desc)
{
    return ImageResourceDesc{
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

std::shared_ptr<RenderTexture> RenderGraphResourceRegistry::wrapImportedTexture(const RGImportedTextureDesc& desc)
{
    auto image = desc.resource ? desc.resource->getImageShared() : nullptr;
    auto view  = desc.resource ? desc.resource->getImageViewShared() : nullptr;
    YA_CORE_ASSERT(image != nullptr && view != nullptr && view->getImage() == image.get(),
                   "Imported render graph texture '{}' requires an owner-provided image and a view of it",
                   desc.importDesc.label);

    auto resource = std::make_shared<ImageResource>();
    resource->label = desc.desc.label.empty() ? desc.importDesc.label : desc.desc.label;
    resource->desc = ImageResourceDesc{
        .image = ImageCreateInfo{
            .label       = resource->label,
            .format      = desc.importDesc.format,
            .extent      = {
                .width  = desc.importDesc.extent.width,
                .height = desc.importDesc.extent.height,
                .depth  = desc.importDesc.extent.depth,
            },
            .mipLevels   = desc.importDesc.mipLevels,
            .arrayLayers = desc.importDesc.arrayLayers,
            .usage       = desc.importDesc.usage,
        },
        .defaultView = makeDefaultViewDesc(desc.desc),
    };
    resource->image = std::move(image);
    resource->defaultView = std::move(view);
    resource->retainedResources = desc.retainedResources;
    return RenderTexture::adopt(std::move(resource));
}

void RenderGraphResourceRegistry::pruneAbsentImports(const RenderGraph& graph)
{
    std::unordered_set<RGImportKey, RGImportKeyHash> liveTextureKeys;
    for (const auto& texture : graph.getTextures()) {
        if (texture.lifetime == ERGResourceLifetime::Imported) {
            liveTextureKeys.insert(texture.importKey);
        }
    }
    for (auto it = _importedTextures.begin(); it != _importedTextures.end();) {
        if (liveTextureKeys.contains(it->first)) {
            ++it;
            continue;
        }
        recordEvent(ERGRegistryEvent::ImportPruned, true, 0, ERGImportChange::None, false, it->first.scope, it->first.label);
        releaseTextureBinding(it->second);
        it = _importedTextures.erase(it);
    }

    std::unordered_set<RGImportKey, RGImportKeyHash> liveBufferKeys;
    for (const auto& buffer : graph.getBuffers()) {
        if (buffer.lifetime == ERGResourceLifetime::Imported) {
            liveBufferKeys.insert(buffer.importKey);
        }
    }
    for (auto it = _importedBuffers.begin(); it != _importedBuffers.end();) {
        if (liveBufferKeys.contains(it->first)) {
            ++it;
            continue;
        }
        recordEvent(ERGRegistryEvent::ImportPruned, false, 0, ERGImportChange::None, false, it->first.scope, it->first.label);
        if (it->second.imported.has_value()) {
            retireRetainedResources(it->second.imported->retainedResources);
        }
        it = _importedBuffers.erase(it);
    }
}

void RenderGraphResourceRegistry::pruneUnusedOwnedResources(const RenderGraph& graph)
{
    // Handle bindings of imports are views onto the key-owned entries above;
    // they are rebound below, never released through a handle.
    std::erase_if(_textures, [](const auto& binding) { return binding.second && binding.second->imported.has_value(); });
    _importedBufferBindings.clear();

    std::unordered_set<RGTextureHandle> liveTextures;
    liveTextures.reserve(graph.getTextures().size());
    for (const auto& texture : graph.getTextures()) {
        liveTextures.insert(texture.handle);
    }
    for (auto it = _textures.begin(); it != _textures.end();) {
        if (!liveTextures.contains(it->first)) {
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
            if (it->second && !it->second->pooledTransient) {
                recordEvent(ERGRegistryEvent::OwnedBufferPruned, false, it->first.index, ERGImportChange::None, true, 0, it->second->desc.label);
            }
            releaseOwnedBufferBinding(it->second);
            it = _ownedBuffers.erase(it);
        }
        else {
            ++it;
        }
    }
}

uint32_t RenderGraphResourceRegistry::diffImportedTexture(const TextureEntry& entry, const RGTextureResource& resource, bool& outReplace)
{
    YA_CORE_ASSERT(entry.imported.has_value() && resource.imported.has_value(),
                   "Imported texture binding '{}' is missing its import desc",
                   resource.desc.label);
    const bool     bSameDesc      = isSameTextureDesc(entry.desc, resource.desc);
    bool           bImportReplace = false;
    const uint32_t changes        = diffImportedTextureDesc(*entry.imported, *resource.imported, bImportReplace);
    outReplace                    = !bSameDesc || bImportReplace;
    return changes | (bSameDesc ? ERGImportChange::None : ERGImportChange::Desc);
}

bool RenderGraphResourceRegistry::needsOwnedBufferReplacement(const OwnedBufferEntry& entry, const RGBufferResource& resource)
{
    return !isSameBufferDesc(entry.desc, resource.desc);
}

uint32_t RenderGraphResourceRegistry::diffImportedBuffer(const ImportedBufferEntry& entry, const RGBufferResource& resource, bool& outReplace)
{
    YA_CORE_ASSERT(entry.imported.has_value() && resource.imported.has_value(),
                   "Imported buffer binding '{}' is missing its import desc",
                   resource.desc.label);
    return diffImportedBufferDesc(*entry.imported, *resource.imported, outReplace);
}

void RenderGraphResourceRegistry::syncImportedTexture(const RGTextureResource& texture)
{
    YA_CORE_ASSERT(texture.imported.has_value(), "Imported render graph texture '{}' is missing import desc", texture.desc.label);
    const auto& key      = texture.importKey;
    auto        existing = _importedTextures.find(key);
    bool        bReplace = true;
    uint32_t    changes  = ERGImportChange::None;
    if (existing != _importedTextures.end()) {
        changes = diffImportedTexture(*existing->second, texture, bReplace);
    }

    if (existing != _importedTextures.end() && !bReplace) {
        auto& entry             = *existing->second;
        bool  bRetiredKeepAlive = refreshRetainedResources(entry.imported->retainedResources, texture.imported->retainedResources);
        entry.imported          = texture.imported;
        if (entry.resource && entry.resource->resource) {
            bRetiredKeepAlive |= refreshRetainedResources(entry.resource->resource->retainedResources, texture.imported->retainedResources);
        }
        if (bRetiredKeepAlive) {
            recordEvent(ERGRegistryEvent::RetainedRefreshed, true, texture.handle.index, changes, false, key.scope, key.label);
        }
        else {
            ++_lastSyncStats.importRebound;
        }
        _textures[texture.handle] = existing->second;
        return;
    }

    const bool bReplaced = existing != _importedTextures.end();
    if (bReplaced) {
        YA_CORE_TRACE("RenderGraph registry replacing imported texture '{}' (scope={})", key.label, key.scope);
        releaseTextureBinding(existing->second);
    }
    auto entry = std::make_shared<TextureEntry>(TextureEntry{
        .resource       = wrapImportedTexture(*texture.imported),
        .desc           = texture.desc,
        .allocationDesc = texture.desc,
        .imported       = texture.imported,
    });
    _importedTextures[key]    = entry;
    _textures[texture.handle] = std::move(entry);
    recordEvent(bReplaced ? ERGRegistryEvent::ImportReplaced : ERGRegistryEvent::ImportBound,
                true,
                texture.handle.index,
                changes,
                false,
                key.scope,
                key.label);
}

void RenderGraphResourceRegistry::syncImportedBuffer(const RGBufferResource& buffer)
{
    YA_CORE_ASSERT(buffer.imported.has_value(), "Imported render graph buffer '{}' is missing import desc", buffer.desc.label);
    if (const auto owned = _ownedBuffers.find(buffer.handle); owned != _ownedBuffers.end()) {
        if (owned->second && !owned->second->pooledTransient) {
            recordEvent(ERGRegistryEvent::OwnedBufferPruned, false, buffer.handle.index, ERGImportChange::Lifetime, true, 0, buffer.desc.label);
        }
        releaseOwnedBufferBinding(owned->second);
        _ownedBuffers.erase(owned);
    }

    const auto& key      = buffer.importKey;
    auto        existing = _importedBuffers.find(key);
    bool        bReplace = true;
    uint32_t    changes  = ERGImportChange::None;
    if (existing != _importedBuffers.end()) {
        changes = diffImportedBuffer(existing->second, buffer, bReplace);
    }
    _importedBufferBindings[buffer.handle] = buffer.imported->buffer;

    if (existing != _importedBuffers.end() && !bReplace) {
        const bool bRetiredKeepAlive = refreshRetainedResources(existing->second.imported->retainedResources,
                                                                buffer.imported->retainedResources);
        existing->second.imported = buffer.imported;
        if (bRetiredKeepAlive) {
            recordEvent(ERGRegistryEvent::RetainedRefreshed, false, buffer.handle.index, changes, false, key.scope, key.label);
        }
        else {
            ++_lastSyncStats.importRebound;
        }
        return;
    }

    const bool bReplaced = existing != _importedBuffers.end();
    if (bReplaced) {
        retireRetainedResources(existing->second.imported->retainedResources);
    }
    _importedBuffers[key] = ImportedBufferEntry{.imported = buffer.imported};
    recordEvent(bReplaced ? ERGRegistryEvent::ImportReplaced : ERGRegistryEvent::ImportBound,
                false,
                buffer.handle.index,
                changes,
                false,
                key.scope,
                key.label);
}

void RenderGraphResourceRegistry::evictIdleTransientTextures()
{
    const uint64_t now = _lastSyncStats.syncSerial;
    std::erase_if(_transientTexturePool, [this, now](std::shared_ptr<TextureEntry>& entry) {
        if (!entry || now - entry->lastUsedSync <= kTransientTextureIdleSyncLimit) {
            return false;
        }
        recordEvent(ERGRegistryEvent::TransientTextureEvicted, true, 0, ERGImportChange::None, true, 0, entry->allocationDesc.label);
        retireSharedResource(entry->resource);
        return true;
    });
}

void RenderGraphResourceRegistry::sync(const RenderGraph& graph, const RGCompiledGraph* compiled)
{
    _transientPoolDiagnostics.lastHitCount = 0;
    _transientPoolDiagnostics.lastMissCount = 0;
    _lastSyncStats = RGRegistrySyncStats{.syncSerial = _lastSyncStats.syncSerial + 1};
    _lastSyncEvents.clear();
    pruneAbsentImports(graph);
    pruneUnusedOwnedResources(graph);

    std::unordered_set<TextureEntry*> usedTransientTextureEntries;
    usedTransientTextureEntries.reserve(graph.getTextures().size());

    // Claim kept transient bindings before any pool acquisition, so a newly
    // declared texture cannot take a pool entry a later handle still keeps.
    std::vector<const RGTextureResource*> transientsToAcquire;
    for (const auto& texture : graph.getTextures()) {
        if (texture.lifetime == ERGResourceLifetime::Imported) {
            syncImportedTexture(texture);
            continue;
        }

        const auto existing = _textures.find(texture.handle);
        if (existing != _textures.end() && existing->second && existing->second->pooledTransient &&
            isSameTextureDesc(existing->second->desc, texture.desc)) {
            usedTransientTextureEntries.insert(existing->second.get());
            existing->second->lastUsedSync = _lastSyncStats.syncSerial;
            continue;
        }
        transientsToAcquire.push_back(&texture);
    }
    for (const auto* texture : transientsToAcquire) {
        auto transientEntry = acquireTransientTexture(texture->desc, usedTransientTextureEntries);
        transientEntry->desc = texture->desc;
        _textures[texture->handle] = std::move(transientEntry);
    }
    evictIdleTransientTextures();

    if (compiled != nullptr) {
        YA_CORE_ASSERT(compiled->isValid(), "RenderGraph registry cannot materialize an invalid compiled graph");
        materializeTransientSlots(graph, *compiled);
    }
    _transientPoolDiagnostics.poolEntryCount = static_cast<uint32_t>(_transientBufferPool.size());

    for (const auto& buffer : graph.getBuffers()) {
        if (compiled != nullptr && buffer.lifetime == ERGResourceLifetime::Transient) {
            continue;
        }
        if (buffer.lifetime == ERGResourceLifetime::Imported) {
            syncImportedBuffer(buffer);
            continue;
        }

        const auto existing = _ownedBuffers.find(buffer.handle);
        if (existing != _ownedBuffers.end() && !needsOwnedBufferReplacement(*existing->second, buffer)) {
            continue;
        }
        const bool bReplacedOwned = existing != _ownedBuffers.end() && existing->second && !existing->second->pooledTransient;
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
        recordEvent(bReplacedOwned ? ERGRegistryEvent::OwnedBufferReplaced : ERGRegistryEvent::OwnedBufferCreated,
                    false,
                    buffer.handle.index,
                    bReplacedOwned ? ERGImportChange::Desc : ERGImportChange::None,
                    true,
                    0,
                    buffer.desc.label);
    }

    if (render_graph_trace::isEnabled()) {
        logSyncEvents();
    }
}

void RenderGraphResourceRegistry::clear()
{
    for (auto& buffer : _transientBufferPool) {
        retireSharedResource(buffer->resource);
    }
    for (auto& texture : _transientTexturePool) {
        retireSharedResource(texture->resource);
    }
    for (auto& [key, texture] : _importedTextures) {
        (void)key;
        releaseTextureBinding(texture);
    }
    for (auto& [handle, buffer] : _ownedBuffers) {
        (void)handle;
        releaseOwnedBufferBinding(buffer);
    }
    _transientTexturePool.clear();
    _transientBufferPool.clear();
    _transientPoolDiagnostics = {};
    _textures.clear();
    _importedTextures.clear();
    _ownedBuffers.clear();
    for (auto& [key, buffer] : _importedBuffers) {
        (void)key;
        if (buffer.imported.has_value()) {
            retireRetainedResources(buffer.imported->retainedResources);
        }
    }
    _importedBuffers.clear();
    _importedBufferBindings.clear();
}

const RenderTexture* RenderGraphResourceRegistry::resolveTexture(RGTextureHandle handle) const
{
    const auto it = _textures.find(handle);
    return it != _textures.end() && it->second ? it->second->resource.get() : nullptr;
}

std::shared_ptr<RenderTexture> RenderGraphResourceRegistry::resolveTextureShared(RGTextureHandle handle) const
{
    const auto it = _textures.find(handle);
    return it != _textures.end() && it->second ? it->second->resource : nullptr;
}

IBuffer* RenderGraphResourceRegistry::resolveBuffer(RGBufferHandle handle) const
{
    if (const auto it = _ownedBuffers.find(handle); it != _ownedBuffers.end()) {
        return it->second ? it->second->resource.get() : nullptr;
    }
    if (const auto it = _importedBufferBindings.find(handle); it != _importedBufferBindings.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace ya

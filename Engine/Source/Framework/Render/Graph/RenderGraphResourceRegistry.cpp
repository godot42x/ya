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
                   bSameViewDesc &&
                   (bSameSharedImageBackedImport || bSameLayoutContract));

    uint32_t changes = ERGImportChange::None;
    changes |= bSameDesc ? 0u : ERGImportChange::Desc;
    changes |= bSameImportedImageIdentitySansLayout ? 0u : ERGImportChange::ImportIdentity;
    changes |= bSameUnderlyingImage ? 0u : ERGImportChange::Image;
    changes |= bSameUnderlyingView ? 0u : ERGImportChange::View;
    changes |= bSameSubresourceRange ? 0u : ERGImportChange::Subresource;
    changes |= bSameViewDesc ? 0u : ERGImportChange::ViewDesc;
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
        {ERGImportChange::ViewDesc, "view-desc"},
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
                                              std::string_view label)
{
    switch (kind) {
    case ERGRegistryEvent::ImportBound:
        ++_lastSyncStats.importBound;
        _lastSyncStats.registryObjectsCreated += bRegistryOwnsGpuObject ? 1u : 0u;
        break;
    case ERGRegistryEvent::ImportReplaced:
        ++_lastSyncStats.importReplaced;
        _lastSyncStats.registryObjectsRetired += bRegistryOwnsGpuObject ? 1u : 0u;
        break;
    case ERGRegistryEvent::RetainedRefreshed:
        ++_lastSyncStats.retainedRefreshed;
        break;
    case ERGRegistryEvent::ImportPruned:
        ++_lastSyncStats.importPruned;
        _lastSyncStats.registryObjectsRetired += bRegistryOwnsGpuObject ? 1u : 0u;
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
        YA_CORE_INFO("[RGTrace]   {} {} '{}' handle={} changes={} registryOwned={}",
                     render_graph_trace::toString(event.kind),
                     event.bTexture ? "texture" : "buffer",
                     event.label,
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
        return entry;
    }

    auto entry = std::make_shared<TextureEntry>(TextureEntry{
        .resource        = RenderTexture::adopt(createImageResource(_factory, makeImageResourceDesc(desc))),
        .desc            = desc,
        .allocationDesc  = desc,
        .pooledTransient = true,
    });
    YA_CORE_ASSERT(entry->resource != nullptr && entry->resource->isValid(),
                   "RenderGraph registry failed to create transient texture '{}'",
                   desc.label);
    _transientTexturePool.push_back(entry);
    usedPoolEntries.insert(entry.get());
    recordEvent(ERGRegistryEvent::TransientTextureAllocated, true, 0, ERGImportChange::None, true, desc.label);
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
    recordEvent(ERGRegistryEvent::TransientBufferSlotAllocated, false, slot.slotIndex, ERGImportChange::None, true, slot.desc.label);
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

std::shared_ptr<RenderTexture> RenderGraphResourceRegistry::createImportedTexture(const RGImportedTextureDesc& desc,
                                                                                  bool& outRegistryCreatedObject)
{
    outRegistryCreatedObject = false;
    auto image = desc.resource ? desc.resource->getImageShared() : nullptr;
    if (!image) {
        image = _factory.importImage(desc.importDesc);
        outRegistryCreatedObject = image != nullptr;
    }
    if (!image) {
        YA_CORE_ERROR("Failed to import render graph texture '{}'", desc.importDesc.label);
        return nullptr;
    }

    auto view = desc.resource ? desc.resource->getImageViewShared() : nullptr;
    if (view) {
        YA_CORE_ASSERT(view->getImage() == image.get(),
                       "Imported render graph texture '{}' provided an image view that does not reference the imported image",
                       desc.importDesc.label);
    }
    else {
        view = _factory.createImageView(image, desc.viewDesc.value_or(makeDefaultViewDesc(desc.desc)));
        outRegistryCreatedObject = outRegistryCreatedObject || view != nullptr;
    }
    if (!view) {
        YA_CORE_ERROR("Failed to create imported render graph texture view '{}'", desc.importDesc.label);
        return nullptr;
    }

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
        .defaultView = desc.viewDesc.value_or(makeDefaultViewDesc(desc.desc)),
    };
    resource->image = std::move(image);
    resource->defaultView = std::move(view);
    resource->retainedResources = desc.retainedResources;
    return RenderTexture::adopt(std::move(resource));
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
            if (it->second && it->second->imported.has_value()) {
                recordEvent(ERGRegistryEvent::ImportPruned,
                            true,
                            it->first.index,
                            ERGImportChange::None,
                            it->second->bRegistryCreatedImportObject,
                            it->second->desc.label);
            }
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
                recordEvent(ERGRegistryEvent::OwnedBufferPruned, false, it->first.index, ERGImportChange::None, true, it->second->desc.label);
            }
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
                recordEvent(ERGRegistryEvent::ImportPruned, false, it->first.index, ERGImportChange::None, false, it->second.imported->desc.label);
                retireRetainedResources(it->second.imported->retainedResources);
            }
            it = _importedBuffers.erase(it);
        }
        else {
            ++it;
        }
    }
}

uint32_t RenderGraphResourceRegistry::diffTexture(const TextureEntry& entry, const RGTextureResource& resource, bool& outReplace)
{
    const bool     bSameDesc   = isSameTextureDesc(entry.desc, resource.desc);
    const uint32_t descChange  = bSameDesc ? ERGImportChange::None : ERGImportChange::Desc;

    if (resource.lifetime == ERGResourceLifetime::Imported) {
        if (!resource.imported.has_value() || !entry.imported.has_value()) {
            outReplace = true;
            return descChange | ERGImportChange::Lifetime;
        }
        bool           bImportReplace = false;
        const uint32_t changes        = diffImportedTextureDesc(*entry.imported, *resource.imported, bImportReplace);
        outReplace                    = !bSameDesc || bImportReplace;
        return changes | descChange;
    }

    outReplace = !bSameDesc || entry.imported.has_value();
    return descChange | (entry.imported.has_value() ? ERGImportChange::Lifetime : ERGImportChange::None);
}

bool RenderGraphResourceRegistry::needsOwnedBufferReplacement(const OwnedBufferEntry& entry, const RGBufferResource& resource)
{
    return !isSameBufferDesc(entry.desc, resource.desc);
}

uint32_t RenderGraphResourceRegistry::diffImportedBuffer(const ImportedBufferEntry& entry, const RGBufferResource& resource, bool& outReplace)
{
    if (resource.lifetime != ERGResourceLifetime::Imported || !resource.imported.has_value() || !entry.imported.has_value()) {
        outReplace = true;
        return ERGImportChange::Lifetime;
    }
    return diffImportedBufferDesc(*entry.imported, *resource.imported, outReplace);
}

void RenderGraphResourceRegistry::sync(const RenderGraph& graph, const RGCompiledGraph* compiled)
{
    _transientPoolDiagnostics.lastHitCount = 0;
    _transientPoolDiagnostics.lastMissCount = 0;
    _lastSyncStats = RGRegistrySyncStats{.syncSerial = _lastSyncStats.syncSerial + 1};
    _lastSyncEvents.clear();
    pruneUnusedResources(graph);

    std::unordered_set<TextureEntry*> usedTransientTextureEntries;
    usedTransientTextureEntries.reserve(graph.getTextures().size());

    for (const auto& texture : graph.getTextures()) {
        const auto existing = _textures.find(texture.handle);
        bool       bReplace = true;
        uint32_t   changes  = ERGImportChange::None;
        if (existing != _textures.end()) {
            changes = diffTexture(*existing->second, texture, bReplace);
        }
        if (existing != _textures.end() && !bReplace) {
            if (texture.lifetime == ERGResourceLifetime::Transient && existing->second->pooledTransient) {
                usedTransientTextureEntries.insert(existing->second.get());
            }
            if (texture.lifetime == ERGResourceLifetime::Imported) {
                bool bRetiredKeepAlive = false;
                if (existing->second->imported.has_value()) {
                    bRetiredKeepAlive |= refreshRetainedResources(
                        existing->second->imported->retainedResources,
                        texture.imported ? texture.imported->retainedResources : std::vector<RetainedResource>{});
                }
                existing->second->imported = texture.imported;
                if (existing->second->resource && existing->second->resource->resource) {
                    bRetiredKeepAlive |= refreshRetainedResources(
                        existing->second->resource->resource->retainedResources,
                        texture.imported ? texture.imported->retainedResources : std::vector<RetainedResource>{});
                }
                if (bRetiredKeepAlive) {
                    recordEvent(ERGRegistryEvent::RetainedRefreshed, true, texture.handle.index, changes, false, texture.desc.label);
                }
                else {
                    ++_lastSyncStats.importRebound;
                }
            }
            continue;
        }

        bool bReplacedImport         = false;
        bool bRetiredRegistryObject  = false;
        if (existing != _textures.end()) {
            YA_CORE_TRACE("RenderGraph registry replacing texture '{}' (handle={}:{})",
                          texture.desc.label,
                          texture.handle.index,
                          texture.handle.generation);
            bReplacedImport        = existing->second && existing->second->imported.has_value();
            bRetiredRegistryObject = existing->second && existing->second->bRegistryCreatedImportObject;
            releaseTextureBinding(existing->second);
        }

        if (texture.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(texture.imported.has_value(), "Imported render graph texture '{}' is missing import desc", texture.desc.label);
            bool bCreatedRegistryObject = false;
            auto resource               = createImportedTexture(*texture.imported, bCreatedRegistryObject);
            _textures[texture.handle] = std::make_shared<TextureEntry>(TextureEntry{
                .resource                     = std::move(resource),
                .desc                         = texture.desc,
                .allocationDesc               = texture.desc,
                .imported                     = texture.imported,
                .pooledTransient              = false,
                .bRegistryCreatedImportObject = bCreatedRegistryObject,
            });
            if (bReplacedImport) {
                recordEvent(ERGRegistryEvent::ImportReplaced, true, texture.handle.index, changes, bRetiredRegistryObject, texture.desc.label);
                _lastSyncStats.registryObjectsCreated += bCreatedRegistryObject ? 1u : 0u;
            }
            else {
                recordEvent(ERGRegistryEvent::ImportBound, true, texture.handle.index, changes, bCreatedRegistryObject, texture.desc.label);
            }
            continue;
        }
        if (bReplacedImport) {
            recordEvent(ERGRegistryEvent::ImportPruned, true, texture.handle.index, changes, bRetiredRegistryObject, texture.desc.label);
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
        if (buffer.lifetime == ERGResourceLifetime::Imported) {
            YA_CORE_ASSERT(buffer.imported.has_value(), "Imported render graph buffer '{}' is missing import desc", buffer.desc.label);
            if (const auto owned = _ownedBuffers.find(buffer.handle); owned != _ownedBuffers.end()) {
                if (owned->second && !owned->second->pooledTransient) {
                    recordEvent(ERGRegistryEvent::OwnedBufferPruned, false, buffer.handle.index, ERGImportChange::Lifetime, true, buffer.desc.label);
                }
                releaseOwnedBufferBinding(owned->second);
            }
            _ownedBuffers.erase(buffer.handle);

            const auto existing = _importedBuffers.find(buffer.handle);
            bool       bReplace = true;
            uint32_t   changes  = ERGImportChange::None;
            if (existing != _importedBuffers.end()) {
                changes = diffImportedBuffer(existing->second, buffer, bReplace);
            }
            if (existing != _importedBuffers.end() && !bReplace) {
                bool bRetiredKeepAlive = false;
                if (existing->second.imported.has_value()) {
                    bRetiredKeepAlive = refreshRetainedResources(
                        existing->second.imported->retainedResources,
                        buffer.imported ? buffer.imported->retainedResources : std::vector<RetainedResource>{});
                }
                existing->second.imported = buffer.imported;
                if (bRetiredKeepAlive) {
                    recordEvent(ERGRegistryEvent::RetainedRefreshed, false, buffer.handle.index, changes, false, buffer.desc.label);
                }
                else {
                    ++_lastSyncStats.importRebound;
                }
                continue;
            }
            const bool bReplacedImport = existing != _importedBuffers.end() && existing->second.imported.has_value();
            if (bReplacedImport) {
                retireRetainedResources(existing->second.imported->retainedResources);
            }

            _importedBuffers[buffer.handle] = ImportedBufferEntry{
                .resource = buffer.imported->buffer,
                .imported = buffer.imported,
            };
            recordEvent(bReplacedImport ? ERGRegistryEvent::ImportReplaced : ERGRegistryEvent::ImportBound,
                        false,
                        buffer.handle.index,
                        changes,
                        false,
                        buffer.desc.label);
            continue;
        }

        if (const auto imported = _importedBuffers.find(buffer.handle);
            imported != _importedBuffers.end() && imported->second.imported.has_value()) {
            recordEvent(ERGRegistryEvent::ImportPruned, false, buffer.handle.index, ERGImportChange::Lifetime, false, buffer.desc.label);
            retireRetainedResources(imported->second.imported->retainedResources);
        }
        _importedBuffers.erase(buffer.handle);

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
    for (auto& [handle, texture] : _textures) {
        (void)handle;
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
    _ownedBuffers.clear();
    for (auto& [handle, buffer] : _importedBuffers) {
        (void)handle;
        if (buffer.imported.has_value()) {
            retireRetainedResources(buffer.imported->retainedResources);
        }
    }
    _importedBuffers.clear();
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
    if (const auto it = _importedBuffers.find(handle); it != _importedBuffers.end()) {
        return it->second.resource;
    }
    return nullptr;
}

} // namespace ya

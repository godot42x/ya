#pragma once

#include "Graph/RenderGraph.h"
#include "Core/Api.h"
#include "RHI/Core/RenderTexture.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ya
{

enum class ERGRegistryEvent : uint8_t
{
    ImportBound,                  ///< Imported handle bound to a new registry entry.
    ImportReplaced,               ///< Imported handle now names a different resource; the old binding is retired.
    RetainedRefreshed,            ///< Binding kept, keep-alive list changed; the old list is retired.
    ImportPruned,                 ///< Imported handle absent from this graph; its binding is retired.
    OwnedBufferCreated,           ///< Non-compiled sync path allocated a registry-owned buffer.
    OwnedBufferReplaced,
    OwnedBufferPruned,
    TransientTextureAllocated,    ///< Transient texture pool miss: a new registry-owned image.
    TransientBufferSlotAllocated, ///< Transient buffer pool miss: a new registry-owned buffer.
};

namespace ERGImportChange
{
enum T : uint32_t
{
    None           = 0,
    Desc           = 1 << 0, ///< RGTextureDesc / RGBufferDesc differs.
    ImportIdentity = 1 << 1, ///< ImportedImageDesc label/native handle/format/usage/extent/mips/layers/ownership.
    Image          = 1 << 2, ///< Underlying IImage differs.
    View           = 1 << 3, ///< Underlying IImageView differs.
    Subresource    = 1 << 4,
    ViewDesc       = 1 << 5,
    LayoutContract = 1 << 6, ///< initial/final layout differs.
    Buffer         = 1 << 7, ///< Underlying IBuffer differs.
    Wrapper        = 1 << 8, ///< ImageResource wrapper differs (informational: never replaces by itself).
    Retained       = 1 << 9, ///< Keep-alive list differs.
    Lifetime       = 1 << 10, ///< Handle switched between imported and registry-owned.
};
} // namespace ERGImportChange

struct RGRegistryEventRecord
{
    ERGRegistryEvent kind         = ERGRegistryEvent::ImportBound;
    bool             bTexture     = true;
    uint32_t         handleIndex  = 0;
    uint32_t         changes      = ERGImportChange::None;
    /// The retired or created binding holds a GPU object the registry created itself
    /// (image view / imported image for textures, buffer for owned/transient buffers).
    bool             bRegistryOwnsGpuObject = false;
    std::string      label;
};

struct RGRegistrySyncStats
{
    uint64_t syncSerial              = 0;
    uint32_t importBound             = 0;
    uint32_t importReplaced          = 0;
    uint32_t importRebound           = 0; ///< Binding kept while the import desc was swapped in (steady state).
    uint32_t retainedRefreshed       = 0;
    uint32_t importPruned            = 0;
    uint32_t registryObjectsCreated  = 0;
    uint32_t registryObjectsRetired  = 0;

    [[nodiscard]] bool hasChurn() const
    {
        return importBound || importReplaced || retainedRefreshed || importPruned ||
               registryObjectsCreated || registryObjectsRetired;
    }
};

namespace render_graph_trace
{
/// Process-wide switch for registry sync event logging and per-executor graph dumps.
YA_RENDER_GRAPH_API void setEnabled(bool bEnabled);
[[nodiscard]] YA_RENDER_GRAPH_API bool isEnabled();
[[nodiscard]] YA_RENDER_GRAPH_API std::string_view toString(ERGRegistryEvent kind);
[[nodiscard]] YA_RENDER_GRAPH_API std::string describeChanges(uint32_t changes);
} // namespace render_graph_trace

class RenderGraphResourceRegistry
{
  private:
    struct TextureEntry
    {
        std::shared_ptr<RenderTexture> resource;
        RGTextureDesc                desc{};
        RGTextureDesc                allocationDesc{};
        std::optional<RGImportedTextureDesc> imported{};
        bool                         pooledTransient = false;
        /// Import binding whose image or view the registry created (no owner-provided one).
        bool                         bRegistryCreatedImportObject = false;
    };

    struct OwnedBufferEntry
    {
        std::shared_ptr<IBuffer> resource;
        RGBufferDesc             desc{};
        bool                     pooledTransient = false;
    };

    struct ImportedBufferEntry
    {
        IBuffer*                          resource = nullptr;
        std::optional<RGImportedBufferDesc> imported{};
    };

    IRenderResourceFactory& _factory;
    std::string             _debugName;
    std::unordered_map<RGTextureHandle, std::shared_ptr<TextureEntry>> _textures;
    std::vector<std::shared_ptr<TextureEntry>> _transientTexturePool;
    std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>> _ownedBuffers;
    std::vector<std::shared_ptr<OwnedBufferEntry>> _transientBufferPool;
    RGTransientBufferPoolDiagnostics _transientPoolDiagnostics{};
    std::unordered_map<RGBufferHandle, ImportedBufferEntry> _importedBuffers;
    RGRegistrySyncStats                _lastSyncStats{};
    /// Filled only while render_graph_trace is enabled.
    std::vector<RGRegistryEventRecord> _lastSyncEvents;

    static ImageResourceDesc makeImageResourceDesc(const RGTextureDesc& desc);
    static ImageViewCreateInfo makeDefaultViewDesc(const RGTextureDesc& desc);
    std::shared_ptr<RenderTexture> createImportedTexture(const RGImportedTextureDesc& desc, bool& outRegistryCreatedObject);
    void recordEvent(ERGRegistryEvent kind,
                     bool             bTexture,
                     uint32_t         handleIndex,
                     uint32_t         changes,
                     bool             bRegistryOwnsGpuObject,
                     std::string_view label);
    void logSyncEvents() const;
    void pruneUnusedResources(const RenderGraph& graph);
    /// Returns ERGImportChange bits; outReplace carries the (unchanged) replacement decision.
    static uint32_t diffTexture(const TextureEntry& entry, const RGTextureResource& resource, bool& outReplace);
    static bool needsOwnedBufferReplacement(const OwnedBufferEntry& entry, const RGBufferResource& resource);
    static uint32_t diffImportedBuffer(const ImportedBufferEntry& entry, const RGBufferResource& resource, bool& outReplace);
    static void releaseTextureBinding(std::shared_ptr<TextureEntry>& entry);
    static void releaseOwnedBufferBinding(std::shared_ptr<OwnedBufferEntry>& entry);
    static bool canReuseTransientSlot(const OwnedBufferEntry& entry, const RGTransientBufferSlotPlan& slot);
    static bool canReuseTransientTexture(const TextureEntry& entry, const RGTextureDesc& desc);
    std::shared_ptr<TextureEntry> acquireTransientTexture(
        const RGTextureDesc& desc,
        std::unordered_set<TextureEntry*>& usedPoolEntries);
    std::shared_ptr<OwnedBufferEntry> acquireTransientSlot(
        const RGTransientBufferSlotPlan& slot,
        std::unordered_set<OwnedBufferEntry*>& usedPoolEntries);
    void materializeTransientSlots(const RenderGraph& graph, const RGCompiledGraph& compiled);

  public:
    explicit RenderGraphResourceRegistry(IRenderResourceFactory& factory, std::string debugName = {})
        : _factory(factory)
        , _debugName(std::move(debugName))
    {}
    YA_RENDER_GRAPH_API ~RenderGraphResourceRegistry();

    YA_RENDER_GRAPH_API void sync(const RenderGraph& graph, const RGCompiledGraph* compiled = nullptr);
    YA_RENDER_GRAPH_API void clear();

    [[nodiscard]] YA_RENDER_GRAPH_API const RenderTexture* resolveTexture(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API std::shared_ptr<RenderTexture> resolveTextureShared(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API IBuffer* resolveBuffer(RGBufferHandle handle) const;
    [[nodiscard]] const RGTransientBufferPoolDiagnostics& getTransientBufferPoolDiagnostics() const
    {
        return _transientPoolDiagnostics;
    }
    [[nodiscard]] const RGRegistrySyncStats& getLastSyncStats() const { return _lastSyncStats; }
    [[nodiscard]] const std::vector<RGRegistryEventRecord>& getLastSyncEvents() const { return _lastSyncEvents; }
    [[nodiscard]] const std::string& getDebugName() const { return _debugName; }

    [[nodiscard]] const std::unordered_map<RGTextureHandle, std::shared_ptr<TextureEntry>>& getTextures() const { return _textures; }
    [[nodiscard]] const std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>>& getOwnedBuffers() const { return _ownedBuffers; }
};

} // namespace ya

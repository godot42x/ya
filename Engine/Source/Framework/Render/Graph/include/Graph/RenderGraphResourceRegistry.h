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
    TransientTextureEvicted,      ///< Pooled transient texture idle past the limit; retired.
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
    LayoutContract = 1 << 5, ///< initial/final layout differs.
    Buffer         = 1 << 6, ///< Underlying IBuffer differs.
    Wrapper        = 1 << 7, ///< ImageResource wrapper differs (informational: never replaces by itself).
    Retained       = 1 << 8, ///< Keep-alive list differs.
    Lifetime       = 1 << 9, ///< Buffer handle switched between imported and registry-owned.
};
} // namespace ERGImportChange

struct RGRegistryEventRecord
{
    ERGRegistryEvent kind         = ERGRegistryEvent::ImportBound;
    bool             bTexture     = true;
    uint32_t         handleIndex  = 0;
    uint32_t         changes      = ERGImportChange::None;
    /// The retired or created binding is a registry-owned GPU object (transient
    /// texture/buffer, owned buffer). Imports are always owner-provided.
    bool             bRegistryOwnsGpuObject = false;
    uint64_t         scope        = 0; ///< Import scope; 0 for registry-owned resources.
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

/// Import bindings are kept by RGImportKey (owner label + import scope), so a
/// graph that inserts, drops or reorders imports only touches the imports that
/// actually changed. Handles are per-graph positions and only map this sync's
/// graph onto those bindings.
class RenderGraphResourceRegistry
{
  public:
    /// A pooled transient texture unused for this many syncs is retired. Several
    /// families may share one executor, so a single idle sync is not "unused".
    static constexpr uint64_t kTransientTextureIdleSyncLimit = 8;

  private:
    struct TextureEntry
    {
        std::shared_ptr<RenderTexture> resource;
        RGTextureDesc                desc{};
        RGTextureDesc                allocationDesc{};
        std::optional<RGImportedTextureDesc> imported{};
        bool                         pooledTransient = false;
        uint64_t                     lastUsedSync    = 0;
    };

    struct OwnedBufferEntry
    {
        std::shared_ptr<IBuffer> resource;
        RGBufferDesc             desc{};
        bool                     pooledTransient = false;
    };

    struct ImportedBufferEntry
    {
        std::optional<RGImportedBufferDesc> imported{};
    };

    IRenderResourceFactory& _factory;
    std::string             _debugName;
    /// This sync's handle -> binding map (imported and transient); rebuilt every sync.
    std::unordered_map<RGTextureHandle, std::shared_ptr<TextureEntry>> _textures;
    std::unordered_map<RGImportKey, std::shared_ptr<TextureEntry>, RGImportKeyHash> _importedTextures;
    std::vector<std::shared_ptr<TextureEntry>> _transientTexturePool;
    std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>> _ownedBuffers;
    std::vector<std::shared_ptr<OwnedBufferEntry>> _transientBufferPool;
    RGTransientBufferPoolDiagnostics _transientPoolDiagnostics{};
    std::unordered_map<RGImportKey, ImportedBufferEntry, RGImportKeyHash> _importedBuffers;
    /// This sync's handle -> imported buffer map; rebuilt every sync.
    std::unordered_map<RGBufferHandle, IBuffer*> _importedBufferBindings;
    RGRegistrySyncStats                _lastSyncStats{};
    /// Filled only while render_graph_trace is enabled.
    std::vector<RGRegistryEventRecord> _lastSyncEvents;

    static ImageResourceDesc makeImageResourceDesc(const RGTextureDesc& desc);
    static ImageViewCreateInfo makeDefaultViewDesc(const RGTextureDesc& desc);
    static std::shared_ptr<RenderTexture> wrapImportedTexture(const RGImportedTextureDesc& desc);
    void recordEvent(ERGRegistryEvent kind,
                     bool             bTexture,
                     uint32_t         handleIndex,
                     uint32_t         changes,
                     bool             bRegistryOwnsGpuObject,
                     uint64_t         scope,
                     std::string_view label);
    void logSyncEvents() const;
    void pruneAbsentImports(const RenderGraph& graph);
    void pruneUnusedOwnedResources(const RenderGraph& graph);
    void syncImportedTexture(const RGTextureResource& texture);
    void syncImportedBuffer(const RGBufferResource& buffer);
    void evictIdleTransientTextures();
    /// Returns ERGImportChange bits; outReplace carries the replacement decision.
    static uint32_t diffImportedTexture(const TextureEntry& entry, const RGTextureResource& resource, bool& outReplace);
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
    [[nodiscard]] size_t getTransientTexturePoolSize() const { return _transientTexturePool.size(); }

    [[nodiscard]] const std::unordered_map<RGTextureHandle, std::shared_ptr<TextureEntry>>& getTextures() const { return _textures; }
    [[nodiscard]] const std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>>& getOwnedBuffers() const { return _ownedBuffers; }
};

} // namespace ya

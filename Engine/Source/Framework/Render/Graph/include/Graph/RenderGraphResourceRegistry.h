#pragma once

#include "Graph/RenderGraph.h"
#include "Core/Api.h"
#include "RHI/Core/RenderTexture.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace ya
{

class RenderGraphResourceRegistry
{
  private:
    struct TextureEntry
    {
        std::shared_ptr<RenderTexture> resource;
        RGTextureDesc                desc{};
        RGTextureDesc                allocationDesc{};
        std::optional<RGPersistentTextureKey> persistentKey{};
        std::optional<RGImportedTextureDesc> imported{};
        bool                         pooledTransient = false;
    };

    struct OwnedBufferEntry
    {
        std::shared_ptr<IBuffer> resource;
        RGBufferDesc             desc{};
        std::optional<RGPersistentBufferKey> persistentKey{};
        bool                     pooledTransient = false;
    };

    struct ImportedBufferEntry
    {
        IBuffer*                          resource = nullptr;
        std::optional<RGImportedBufferDesc> imported{};
    };

    IRenderResourceFactory& _factory;
    std::unordered_map<RGTextureHandle, std::shared_ptr<TextureEntry>> _textures;
    std::unordered_map<std::string, std::shared_ptr<TextureEntry>> _persistentTextures;
    std::vector<std::shared_ptr<TextureEntry>> _transientTexturePool;
    std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>> _ownedBuffers;
    std::unordered_map<std::string, std::shared_ptr<OwnedBufferEntry>> _persistentOwnedBuffers;
    std::vector<std::shared_ptr<OwnedBufferEntry>> _transientBufferPool;
    RGTransientBufferPoolDiagnostics _transientPoolDiagnostics{};
    std::unordered_map<RGBufferHandle, ImportedBufferEntry> _importedBuffers;

    static ImageResourceDesc makeImageResourceDesc(const RGTextureDesc& desc);
    static ImageViewCreateInfo makeDefaultViewDesc(const RGTextureDesc& desc);
    std::shared_ptr<RenderTexture> createImportedTexture(const RGImportedTextureDesc& desc);
    void pruneUnusedResources(const RenderGraph& graph);
    static bool needsTextureReplacement(const TextureEntry& entry, const RGTextureResource& resource);
    static bool needsOwnedBufferReplacement(const OwnedBufferEntry& entry, const RGBufferResource& resource);
    static bool needsImportedBufferReplacement(const ImportedBufferEntry& entry, const RGBufferResource& resource);
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
    explicit RenderGraphResourceRegistry(IRenderResourceFactory& factory)
        : _factory(factory)
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

    [[nodiscard]] const std::unordered_map<RGTextureHandle, std::shared_ptr<TextureEntry>>& getTextures() const { return _textures; }
    [[nodiscard]] const std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>>& getOwnedBuffers() const { return _ownedBuffers; }
};

} // namespace ya

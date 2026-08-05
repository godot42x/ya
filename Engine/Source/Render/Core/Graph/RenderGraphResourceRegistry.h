#pragma once

#include "Render/Core/Graph/RenderGraph.h"
#include "Core/Api.h"
#include "Render/Core/RenderImage.h"

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
        std::shared_ptr<RenderImage> resource;
        RGTextureDesc                desc{};
        std::optional<RGPersistentTextureKey> persistentKey{};
        std::optional<RGImportedTextureDesc> imported{};
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
    std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>> _ownedBuffers;
    std::unordered_map<std::string, std::shared_ptr<OwnedBufferEntry>> _persistentOwnedBuffers;
    std::vector<std::shared_ptr<OwnedBufferEntry>> _transientBufferPool;
    RGTransientBufferPoolDiagnostics _transientPoolDiagnostics{};
    std::unordered_map<RGBufferHandle, ImportedBufferEntry> _importedBuffers;

    static RenderImageDesc makeRenderImageDesc(const RGTextureDesc& desc);
    static ImageViewCreateInfo makeDefaultViewDesc(const RGTextureDesc& desc);
    std::shared_ptr<RenderImage> createImportedTexture(const RGImportedTextureDesc& desc);
    void pruneUnusedResources(const RenderGraph& graph);
    static bool needsTextureReplacement(const TextureEntry& entry, const RGTextureResource& resource);
    static bool needsOwnedBufferReplacement(const OwnedBufferEntry& entry, const RGBufferResource& resource);
    static bool needsImportedBufferReplacement(const ImportedBufferEntry& entry, const RGBufferResource& resource);
    static void releaseTextureBinding(std::shared_ptr<TextureEntry>& entry);
    static void releaseOwnedBufferBinding(std::shared_ptr<OwnedBufferEntry>& entry);
    static bool canReuseTransientSlot(const OwnedBufferEntry& entry, const RGTransientBufferSlotPlan& slot);
    std::shared_ptr<OwnedBufferEntry> acquireTransientSlot(
        const RGTransientBufferSlotPlan& slot,
        std::unordered_set<OwnedBufferEntry*>& usedPoolEntries);
    void materializeTransientSlots(const RenderGraph& graph, const RGCompiledGraph& compiled);

  public:
    explicit RenderGraphResourceRegistry(IRenderResourceFactory& factory)
        : _factory(factory)
    {}
    ENGINE_API ~RenderGraphResourceRegistry();

    ENGINE_API void sync(const RenderGraph& graph, const RGCompiledGraph* compiled = nullptr);
    ENGINE_API void clear();

    [[nodiscard]] ENGINE_API const RenderImage* resolveTexture(RGTextureHandle handle) const;
    [[nodiscard]] ENGINE_API std::shared_ptr<RenderImage> resolveTextureShared(RGTextureHandle handle) const;
    [[nodiscard]] ENGINE_API IBuffer* resolveBuffer(RGBufferHandle handle) const;
    [[nodiscard]] const RGTransientBufferPoolDiagnostics& getTransientBufferPoolDiagnostics() const
    {
        return _transientPoolDiagnostics;
    }

    [[nodiscard]] const std::unordered_map<RGTextureHandle, std::shared_ptr<TextureEntry>>& getTextures() const { return _textures; }
    [[nodiscard]] const std::unordered_map<RGBufferHandle, std::shared_ptr<OwnedBufferEntry>>& getOwnedBuffers() const { return _ownedBuffers; }
};

} // namespace ya

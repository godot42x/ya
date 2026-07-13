#pragma once

#include "Render/Core/RenderGraph.h"
#include "Render/Core/RenderImage.h"

#include <memory>
#include <unordered_map>

namespace ya
{

class RenderGraphResourceRegistry
{
  private:
    struct TextureEntry
    {
        std::shared_ptr<RenderImage> resource;
        RGTextureDesc                desc{};
        std::optional<RGImportedTextureDesc> imported{};
    };

    struct OwnedBufferEntry
    {
        std::shared_ptr<IBuffer> resource;
        RGBufferDesc             desc{};
    };

    struct ImportedBufferEntry
    {
        IBuffer*                          resource = nullptr;
        std::optional<RGImportedBufferDesc> imported{};
    };

    IRenderResourceFactory& _factory;
    std::unordered_map<RGTextureHandle, TextureEntry> _textures;
    std::unordered_map<RGBufferHandle, OwnedBufferEntry> _ownedBuffers;
    std::unordered_map<RGBufferHandle, ImportedBufferEntry> _importedBuffers;

    static RenderImageDesc makeRenderImageDesc(const RGTextureDesc& desc);
    static ImageViewCreateInfo makeDefaultViewDesc(const RGTextureDesc& desc);
    std::shared_ptr<RenderImage> createImportedTexture(const RGImportedTextureDesc& desc);
    void pruneUnusedResources(const RenderGraph& graph);
    static bool needsTextureReplacement(const TextureEntry& entry, const RGTextureResource& resource);
    static bool needsOwnedBufferReplacement(const OwnedBufferEntry& entry, const RGBufferResource& resource);
    static bool needsImportedBufferReplacement(const ImportedBufferEntry& entry, const RGBufferResource& resource);

  public:
    explicit RenderGraphResourceRegistry(IRenderResourceFactory& factory)
        : _factory(factory)
    {}

    void sync(const RenderGraph& graph);
    void clear();

    [[nodiscard]] const RenderImage* resolveTexture(RGTextureHandle handle) const;
    [[nodiscard]] IBuffer* resolveBuffer(RGBufferHandle handle) const;

    [[nodiscard]] const std::unordered_map<RGTextureHandle, TextureEntry>& getTextures() const { return _textures; }
    [[nodiscard]] const std::unordered_map<RGBufferHandle, OwnedBufferEntry>& getOwnedBuffers() const { return _ownedBuffers; }
};

} // namespace ya

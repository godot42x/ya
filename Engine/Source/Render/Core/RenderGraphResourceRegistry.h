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
    IRenderResourceFactory& _factory;
    std::unordered_map<RGTextureHandle, std::shared_ptr<RenderImage>> _textures;
    std::unordered_map<RGBufferHandle, std::shared_ptr<IBuffer>>      _ownedBuffers;
    std::unordered_map<RGBufferHandle, IBuffer*>                      _importedBuffers;

    static RenderImageDesc makeRenderImageDesc(const RGTextureDesc& desc);
    static ImageViewCreateInfo makeDefaultViewDesc(const RGTextureDesc& desc);
    std::shared_ptr<RenderImage> createImportedTexture(const RGImportedTextureDesc& desc);

  public:
    explicit RenderGraphResourceRegistry(IRenderResourceFactory& factory)
        : _factory(factory)
    {}

    void sync(const RenderGraph& graph);
    void clear();

    [[nodiscard]] const RenderImage* resolveTexture(RGTextureHandle handle) const;
    [[nodiscard]] IBuffer* resolveBuffer(RGBufferHandle handle) const;

    [[nodiscard]] const std::unordered_map<RGTextureHandle, std::shared_ptr<RenderImage>>& getTextures() const { return _textures; }
    [[nodiscard]] const std::unordered_map<RGBufferHandle, std::shared_ptr<IBuffer>>& getOwnedBuffers() const { return _ownedBuffers; }
};

} // namespace ya

#pragma once

#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/RenderDefines.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct RenderImage;

template <typename Tag>
struct RGHandle
{
    static constexpr uint32_t INVALID_INDEX = ~0u;

    uint32_t index      = INVALID_INDEX;
    uint32_t generation = 0;

    [[nodiscard]] bool isValid() const
    {
        return index != INVALID_INDEX;
    }

    bool operator==(const RGHandle&) const = default;
};

struct RGTextureHandleTag
{};
using RGTextureHandle = RGHandle<RGTextureHandleTag>;

struct RGBufferHandleTag
{};
using RGBufferHandle = RGHandle<RGBufferHandleTag>;

struct RGPassHandleTag
{};
using RGPassHandle = RGHandle<RGPassHandleTag>;

enum class ERGResourceLifetime : uint8_t
{
    Imported,
    Transient,
    Persistent,
};

struct RGTextureDesc
{
    std::string         label;
    EFormat::T          format      = EFormat::Undefined;
    Extent3D            extent      = {};
    uint32_t            mipLevels   = 1;
    uint32_t            arrayLayers = 1;
    ESampleCount::T     samples     = ESampleCount::Sample_1;
    EImageUsage::T      usage       = EImageUsage::None;
    EImageCreateFlag::T flags       = EImageCreateFlag::None;
};

struct RGBufferDesc
{
    std::string  label;
    EBufferUsage usage       = EBufferUsage::None;
    uint32_t     size        = 0;
    EMemoryUsage memoryUsage = EMemoryUsage::Auto;
};

struct RGImportedTextureDesc
{
    RGTextureDesc     desc;
    ImportedImageDesc importDesc;
    std::shared_ptr<IImage> image = nullptr;
    std::shared_ptr<IImageView> imageView = nullptr;
    std::optional<ImageSubresourceRange> subresourceRange{};
    std::optional<ImageViewCreateInfo> viewDesc{};
    std::vector<std::shared_ptr<void>> retainedResources{};
};

struct RGImportedBufferDesc
{
    RGBufferDesc       desc;
    IBuffer*           buffer = nullptr;
    BufferResourceState initialState{};
    std::vector<std::shared_ptr<void>> retainedResources{};
};

struct RGTextureResource
{
    RGTextureHandle                  handle{};
    ERGResourceLifetime              lifetime = ERGResourceLifetime::Transient;
    RGTextureDesc                    desc{};
    std::optional<RGImportedTextureDesc> imported{};
};

struct RGBufferResource
{
    RGBufferHandle                   handle{};
    ERGResourceLifetime              lifetime = ERGResourceLifetime::Transient;
    RGBufferDesc                     desc{};
    std::optional<RGImportedBufferDesc> imported{};
};

enum class ERGPassResourceAccess : uint8_t
{
    Read,
    Write,
    ColorAttachment,
    DepthAttachment,
    TransferSrc,
    TransferDst,
};

struct RGTextureUsage
{
    RGTextureHandle         handle{};
    ERGPassResourceAccess   access = ERGPassResourceAccess::Read;
};

enum class ERGBufferAccess : uint8_t
{
    ShaderRead,
    ShaderWrite,
    ShaderReadWrite,
    IndirectRead,
    TransferRead,
    TransferWrite,
};

struct RGBufferUsage
{
    RGBufferHandle handle{};
    ERGBufferAccess access = ERGBufferAccess::ShaderRead;
};

struct RGPass
{
    RGPassHandle                handle{};
    std::string                 name;
    std::vector<RGTextureUsage> textures;
    std::vector<RGBufferUsage>  buffers;
    std::vector<RGPassHandle>   dependencies;
    std::function<void(class RGRenderContext&)> execute;
};

struct RGDependencyEdge
{
    RGPassHandle from{};
    RGPassHandle to{};

    bool operator==(const RGDependencyEdge&) const = default;
};

struct RGTextureStatePlan
{
    RGPassHandle        pass{};
    RGTextureHandle     texture{};
    ImageResourceState  requiredState{};
};

struct RGBufferStatePlan
{
    RGPassHandle         pass{};
    RGBufferHandle       buffer{};
    BufferResourceState  requiredState{};
};

struct RGCompileIssue
{
    enum class EKind : uint8_t
    {
        ReadBeforeWrite,
        InvalidResource,
        InvalidUsage,
        Cycle,
    };

    EKind       kind = EKind::InvalidResource;
    RGPassHandle pass{};
    std::string message;
};

struct RGCompiledGraph
{
    std::vector<RGPassHandle>      order;
    std::vector<RGDependencyEdge>  dependencies;
    std::vector<RGTextureStatePlan> textureStates;
    std::vector<RGBufferStatePlan>  bufferStates;
    std::vector<RGCompileIssue>    issues;

    [[nodiscard]] bool isValid() const
    {
        return issues.empty();
    }
};

class RenderGraph;
class RenderGraphResourceRegistry;

class RGPassContext
{
  private:
    const RenderGraph& _graph;
    const RGPass&      _pass;

  public:
    RGPassContext(const RenderGraph& graph, const RGPass& pass)
        : _graph(graph), _pass(pass)
    {}

    [[nodiscard]] const RGPass& getPass() const { return _pass; }
    [[nodiscard]] const RGTextureResource& getTexture(RGTextureHandle handle) const;
    [[nodiscard]] const RGBufferResource& getBuffer(RGBufferHandle handle) const;
    [[nodiscard]] const RGTextureDesc& getTextureDesc(RGTextureHandle handle) const;
    [[nodiscard]] const RGBufferDesc& getBufferDesc(RGBufferHandle handle) const;
};

class RGRenderContext
{
  public:
    struct ColorAttachmentRenderingDesc
    {
        RGTextureHandle          color;
        ClearValue               clearValue = ClearValue::Black();
        EAttachmentLoadOp::T     loadOp     = EAttachmentLoadOp::Clear;
        EAttachmentStoreOp::T    storeOp    = EAttachmentStoreOp::Store;
        EImageLayout::T          finalLayout = EImageLayout::ColorAttachmentOptimal;
    };

    struct ColorRenderingDesc
    {
        RGTextureHandle          color;
        Rect2D                   renderArea{};
        ClearValue               clearValue = ClearValue::Black();
        uint32_t                 layerCount = 1;
        EAttachmentLoadOp::T     loadOp     = EAttachmentLoadOp::Clear;
        EAttachmentStoreOp::T    storeOp    = EAttachmentStoreOp::Store;
        EImageLayout::T          finalLayout = EImageLayout::ColorAttachmentOptimal;
    };

    struct DepthRenderingDesc
    {
        RGTextureHandle          depth;
        ClearValue               clearValue = ClearValue(1.0f, 0);
        EAttachmentLoadOp::T     loadOp     = EAttachmentLoadOp::Load;
        EAttachmentStoreOp::T    storeOp    = EAttachmentStoreOp::Store;
        EImageLayout::T          finalLayout = EImageLayout::DepthStencilAttachmentOptimal;
    };

    struct RasterRenderingDesc
    {
        Rect2D                               renderArea{};
        uint32_t                             layerCount = 1;
        std::vector<ColorAttachmentRenderingDesc> colors{};
        std::optional<DepthRenderingDesc> depth{};
    };

  private:
    const RenderGraph&                 _graph;
    const RGPass&                      _pass;
    const RenderGraphResourceRegistry& _registry;
    ICommandBuffer&                    _cmdBuf;
    mutable std::optional<RenderingInfo> _activeRenderingInfo;

  public:
    RGRenderContext(
        const RenderGraph& graph,
        const RGPass& pass,
        const RenderGraphResourceRegistry& registry,
        ICommandBuffer& cmdBuf)
        : _graph(graph), _pass(pass), _registry(registry), _cmdBuf(cmdBuf)
    {}

    [[nodiscard]] const RGPass& getPass() const { return _pass; }
    [[nodiscard]] ICommandBuffer& getCommandBuffer() const { return _cmdBuf; }
    [[nodiscard]] const RGTextureResource& getTexture(RGTextureHandle handle) const;
    [[nodiscard]] const RGBufferResource& getBuffer(RGBufferHandle handle) const;
    [[nodiscard]] const RGTextureDesc& getTextureDesc(RGTextureHandle handle) const;
    [[nodiscard]] const RGBufferDesc& getBufferDesc(RGBufferHandle handle) const;
    [[nodiscard]] const RenderImage* resolveTexture(RGTextureHandle handle) const;
    [[nodiscard]] IBuffer* resolveBuffer(RGBufferHandle handle) const;
    void beginRasterRendering(const RasterRenderingDesc& desc) const;
    void beginColorRendering(const ColorRenderingDesc& desc) const;
    void endRendering() const;
    void copyBuffer(RGBufferHandle src, RGBufferHandle dst, uint64_t size, uint64_t srcOffset = 0, uint64_t dstOffset = 0) const;
    void copyTexture(RGTextureHandle src, RGTextureHandle dst, const ImageCopy& region) const;
};

class RGPassBuilder
{
  private:
    RenderGraph& _graph;
    size_t       _passIndex = 0;

    RGPass& pass();

  public:
    RGPassBuilder(RenderGraph& graph, size_t passIndex)
        : _graph(graph), _passIndex(passIndex)
    {}

    void read(RGTextureHandle handle);
    void write(RGTextureHandle handle);
    void read(RGBufferHandle handle);
    void write(RGBufferHandle handle);
    void readWrite(RGBufferHandle handle);
    void indirectRead(RGBufferHandle handle);
    void transferSrc(RGBufferHandle handle);
    void transferDst(RGBufferHandle handle);
    void dependsOn(RGPassHandle handle);
    void useColorAttachment(RGTextureHandle handle);
    void useDepthAttachment(RGTextureHandle handle);
    void transferSrc(RGTextureHandle handle);
    void transferDst(RGTextureHandle handle);
};

class RenderGraph
{
  private:
    uint32_t _nextTextureGeneration = 1;
    uint32_t _nextBufferGeneration  = 1;
    uint32_t _nextPassGeneration    = 1;
    std::vector<RGTextureResource> _textures;
    std::vector<RGBufferResource>  _buffers;
    std::vector<RGPass>            _passes;

    template <typename HandleT, typename ResourceT>
    static const ResourceT* findResource(const std::vector<ResourceT>& resources, HandleT handle)
    {
        if (!handle.isValid() || handle.index >= resources.size()) {
            return nullptr;
        }
        const auto& resource = resources[handle.index];
        return resource.handle == handle ? &resource : nullptr;
    }

  public:
    [[nodiscard]] RGTextureHandle createTexture(const RGTextureDesc& desc, ERGResourceLifetime lifetime = ERGResourceLifetime::Transient);
    [[nodiscard]] RGTextureHandle importTexture(const RGImportedTextureDesc& desc);

    [[nodiscard]] RGBufferHandle createBuffer(const RGBufferDesc& desc, ERGResourceLifetime lifetime = ERGResourceLifetime::Transient);
    [[nodiscard]] RGBufferHandle importBuffer(const RGImportedBufferDesc& desc);

    [[nodiscard]] const RGTextureResource* getTexture(RGTextureHandle handle) const;
    [[nodiscard]] const RGBufferResource* getBuffer(RGBufferHandle handle) const;
    [[nodiscard]] const RGPass* getPass(RGPassHandle handle) const;

    [[nodiscard]] RGPassHandle addPass(
        const std::string& name,
        const std::function<void(RGPassBuilder&)>& setup,
        const std::function<void(RGRenderContext&)>& execute = {});
    [[nodiscard]] RGCompiledGraph compile() const;
    [[nodiscard]] std::optional<RGPassContext> createPassContext(RGPassHandle handle) const;
    [[nodiscard]] std::string debugDump(const RGCompiledGraph& compiled) const;

    [[nodiscard]] const std::vector<RGTextureResource>& getTextures() const { return _textures; }
    [[nodiscard]] const std::vector<RGBufferResource>& getBuffers() const { return _buffers; }
    [[nodiscard]] const std::vector<RGPass>& getPasses() const { return _passes; }
};

} // namespace ya

namespace std
{

template <typename Tag>
struct hash<ya::RGHandle<Tag>>
{
    std::size_t operator()(const ya::RGHandle<Tag>& h) const noexcept
    {
        return (static_cast<std::size_t>(h.generation) << 32) ^ static_cast<std::size_t>(h.index);
    }
};

} // namespace std

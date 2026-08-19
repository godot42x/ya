#pragma once

#include "RHI/Core/Buffer.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Core/Sampler.h"
#include "RHI/RenderDefines.h"
#include "Core/Api.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct ImageResource;
struct RenderTexture;

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

struct RGPersistentTextureKey
{
    std::string value;

    [[nodiscard]] bool isValid() const
    {
        return !value.empty();
    }

    bool operator==(const RGPersistentTextureKey&) const = default;
};

struct RGPersistentBufferKey
{
    std::string value;

    [[nodiscard]] bool isValid() const
    {
        return !value.empty();
    }

    bool operator==(const RGPersistentBufferKey&) const = default;
};

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
    // Alignment is an explicit backend-neutral contract for future suballocation.
    // A value of one means that the resource has no stronger requirement.
    uint32_t     alignment   = 1;
};

struct RGBufferRange
{
    uint64_t offset = 0;
    uint64_t size   = 0;
};

struct RGImportedTextureDesc
{
    RGTextureDesc     desc;
    ImportedImageDesc importDesc;
    std::shared_ptr<ImageResource> resource = nullptr;
    std::optional<ImageSubresourceRange> subresourceRange{};
    std::optional<ImageViewCreateInfo> viewDesc{};
    std::vector<std::shared_ptr<void>> retainedResources{};
};

struct RGImportedBufferDesc
{
    RGBufferDesc       desc;
    IBuffer*           buffer = nullptr;
    BufferResourceState initialState{};
    std::optional<BufferResourceState> finalState{};
    std::vector<std::shared_ptr<void>> retainedResources{};
};

struct RGTextureResource
{
    RGTextureHandle                  handle{};
    ERGResourceLifetime              lifetime = ERGResourceLifetime::Transient;
    std::optional<RGPersistentTextureKey> persistentKey{};
    RGTextureDesc                    desc{};
    std::optional<RGImportedTextureDesc> imported{};
};

struct RGBufferResource
{
    RGBufferHandle                   handle{};
    ERGResourceLifetime              lifetime = ERGResourceLifetime::Transient;
    std::optional<RGPersistentBufferKey> persistentKey{};
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

enum class ERGPassKind : uint8_t
{
    Unknown,
    Raster,
    Compute,
    Copy,
};

struct RGTextureUsage
{
    RGTextureHandle         handle{};
    ERGPassResourceAccess   access = ERGPassResourceAccess::Read;
};

enum class ERGBufferAccess : uint8_t
{
    UniformRead,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    IndirectRead,
    TransferRead,
    TransferWrite,
};

struct RGBufferUsage
{
    RGBufferHandle handle{};
    ERGBufferAccess access = ERGBufferAccess::StorageRead;
    RGBufferRange   range{};
};

struct RGColorAttachmentDesc
{
    RGTextureHandle          color;
    RGTextureHandle          resolve{};
    EResolveMode::T          resolveMode = EResolveMode::None;
    ClearValue               clearValue = ClearValue::Black();
    EAttachmentLoadOp::T     loadOp     = EAttachmentLoadOp::Clear;
    EAttachmentStoreOp::T    storeOp    = EAttachmentStoreOp::Store;
    EImageLayout::T          finalLayout = EImageLayout::ColorAttachmentOptimal;
};

struct RGDepthAttachmentDesc
{
    RGTextureHandle          depth;
    ClearValue               clearValue = ClearValue(1.0f, 0);
    EAttachmentLoadOp::T     loadOp     = EAttachmentLoadOp::Load;
    EAttachmentStoreOp::T    storeOp    = EAttachmentStoreOp::Store;
    EImageLayout::T          finalLayout = EImageLayout::DepthStencilAttachmentOptimal;
};

struct RGRasterPassDesc
{
    Rect2D                               renderArea{};
    uint32_t                             layerCount = 1;
    std::vector<RGColorAttachmentDesc>   colors{};
    std::optional<RGDepthAttachmentDesc> depth{};
};

struct RGPass
{
    RGPassHandle                handle{};
    std::string                 name;
    ERGPassKind                 kind = ERGPassKind::Unknown;
    std::vector<RGTextureUsage> textures;
    std::vector<RGBufferUsage>  buffers;
    std::vector<RGPassHandle>   dependencies;
    std::optional<RGRasterPassDesc> rasterDesc{};
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
    RGPassHandle          pass{};
    RGTextureHandle       texture{};
    EImageLayout::T       layout = EImageLayout::Undefined;
    ImageSubresourceRange subresourceRange{};
};

struct RGBufferStatePlan
{
    RGPassHandle         pass{};
    RGBufferHandle       buffer{};
    BufferResourceState  requiredState{};
};

struct RGCompiledPassPlan
{
    RGPassHandle                    pass{};
    ERGPassKind                     kind = ERGPassKind::Unknown;
    std::vector<RGTextureStatePlan> textureStates;
    std::vector<RGBufferStatePlan>  bufferStates;
    std::optional<RGRasterPassDesc> rasterPlan{};
};

struct RGTextureExportPlan
{
    std::string     name;
    RGTextureHandle texture{};
};

struct RGImportedTextureFinalizePlan
{
    RGTextureHandle                        texture{};
    EImageLayout::T                        finalLayout = EImageLayout::Undefined;
    std::optional<ImageSubresourceRange>   subresourceRange{};
};

struct RGImportedBufferFinalizePlan
{
    RGBufferHandle        buffer{};
    BufferResourceState   initialState{};
    BufferResourceState   finalState{};
};

struct RGTransientBufferLifetimePlan
{
    RGBufferHandle buffer{};
    RGBufferDesc   desc{};
    uint32_t       firstPassIndex = ~0u;
    uint32_t       lastPassIndex  = ~0u;
    RGPassHandle   firstPass{};
    RGPassHandle   lastPass{};

    [[nodiscard]] bool isUsed() const
    {
        return firstPassIndex != ~0u;
    }
};

struct RGTransientBufferAssignment
{
    RGBufferHandle buffer{};
    uint32_t       slotIndex = ~0u;
};

struct RGTransientBufferSlotPlan
{
    uint32_t                 slotIndex = ~0u;
    RGBufferDesc             desc{};
    std::vector<RGBufferHandle> buffers;
};

struct RGTransientBufferAliasBoundaryPlan
{
    uint32_t       slotIndex = ~0u;
    RGBufferHandle previousBuffer{};
    RGBufferHandle nextBuffer{};
    RGPassHandle   nextPass{};
};

struct RGTransientBufferDiagnostics
{
    uint32_t logicalCount       = 0;
    uint64_t logicalBytes       = 0;
    uint32_t usedCount          = 0;
    uint64_t usedBytes          = 0;
    uint32_t unusedCount        = 0;
    uint64_t unusedBytes        = 0;
    uint32_t physicalSlotCount  = 0;
    uint64_t physicalBytes      = 0;
    uint32_t aliasedBufferCount = 0;
    uint32_t aliasBoundaryCount = 0;
    double   reuseRatio         = 0.0;
};

struct RGTransientBufferPoolDiagnostics
{
    uint32_t lastHitCount   = 0;
    uint32_t lastMissCount  = 0;
    uint32_t poolEntryCount = 0;
    uint64_t totalHitCount  = 0;
    uint64_t totalMissCount = 0;
};

struct RGCompileIssue
{
    enum class EKind : uint8_t
    {
        ReadBeforeWrite,
        InvalidResource,
        InvalidUsage,
        InvalidPassKind,
        InvalidPersistentIdentity,
        Cycle,
    };

    EKind       kind = EKind::InvalidResource;
    RGPassHandle pass{};
    std::string message;
};

struct RGCompiledGraph
{
    std::vector<RGPassHandle>                 order;
    std::vector<RGDependencyEdge>             dependencies;
    std::vector<RGCompiledPassPlan>           passPlans;
    std::vector<RGTextureExportPlan>          exportedTextures;
    std::vector<RGImportedTextureFinalizePlan> importedTextureFinalizes;
    std::vector<RGImportedBufferFinalizePlan>  importedBufferFinalizes;
    std::vector<RGTransientBufferLifetimePlan> transientBufferLifetimes;
    std::vector<RGTransientBufferAssignment>   transientBufferAssignments;
    std::vector<RGTransientBufferSlotPlan>     transientBufferSlots;
    std::vector<RGTransientBufferAliasBoundaryPlan> transientBufferAliasBoundaries;
    RGTransientBufferDiagnostics               transientBufferDiagnostics;
    std::vector<RGCompileIssue>               issues;

    [[nodiscard]] bool isValid() const
    {
        return issues.empty();
    }
};

struct RGTopologyPassInfo
{
    RGPassHandle     pass{};
    std::string_view name{};
    ERGPassKind      kind = ERGPassKind::Unknown;
    uint32_t         orderIndex = ~0u;
};

struct RGTopologyDependencyInfo
{
    RGPassHandle     from{};
    RGPassHandle     to{};
    std::string_view fromName{};
    std::string_view toName{};
};

struct RGTopologyDescription
{
    std::vector<RGTopologyPassInfo>       passOrder;
    std::vector<RGTopologyDependencyInfo> dependencies;
};

class YA_RENDER_GRAPH_API RenderGraphExecutionResult
{
  private:
    std::unordered_map<std::string, std::shared_ptr<RenderTexture>> _exportedTextures;

  public:
    void clear() { _exportedTextures.clear(); }
    void bindExportedTexture(std::string name, std::shared_ptr<RenderTexture> texture);
    [[nodiscard]] bool hasExportedTexture(std::string_view name) const;
    [[nodiscard]] std::shared_ptr<RenderTexture> getExportedTextureShared(std::string_view name) const;
    [[nodiscard]] RenderTexture* getExportedTexture(std::string_view name) const;
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
    [[nodiscard]] YA_RENDER_GRAPH_API const RGTextureResource& getTexture(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGBufferResource& getBuffer(RGBufferHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGTextureDesc& getTextureDesc(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGBufferDesc& getBufferDesc(RGBufferHandle handle) const;
};

class RGRenderContext
{
  public:
    using ColorAttachmentRenderingDesc = RGColorAttachmentDesc;
    using DepthRenderingDesc           = RGDepthAttachmentDesc;
    using RasterRenderingDesc          = RGRasterPassDesc;

    struct RasterPassExecutionParams
    {
        const RGRasterPassDesc& rasterPlan;

        [[nodiscard]] Extent2D getRenderExtent() const
        {
            return Extent2D{
                .width  = static_cast<uint32_t>(rasterPlan.renderArea.extent.x),
                .height = static_cast<uint32_t>(rasterPlan.renderArea.extent.y),
            };
        }

        [[nodiscard]] const RGColorAttachmentDesc& getColorAttachment(size_t index = 0) const
        {
            YA_CORE_ASSERT(index < rasterPlan.colors.size(),
                           "RasterPassExecutionParams color attachment index {} is out of range (size={})",
                           index,
                           rasterPlan.colors.size());
            return rasterPlan.colors[index];
        }

        [[nodiscard]] const RGDepthAttachmentDesc& getDepthAttachment() const
        {
            YA_CORE_ASSERT(rasterPlan.depth.has_value(), "RasterPassExecutionParams does not have a depth attachment");
            return *rasterPlan.depth;
        }
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

  private:
    const RenderGraph&                 _graph;
    const RGPass&                      _pass;
    const RenderGraphResourceRegistry& _registry;
    ICommandBuffer&                    _cmdBuf;
    const RGCompiledPassPlan*          _compiledPassPlan = nullptr;
    mutable std::optional<RenderingInfo> _activeRenderingInfo;

    [[nodiscard]] YA_RENDER_GRAPH_API const RGTextureUsage* findDeclaredTextureUsage(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGBufferUsage* findDeclaredBufferUsage(RGBufferHandle handle) const;
    YA_RENDER_GRAPH_API void assertTextureDeclared(RGTextureHandle handle, const char* operation) const;
    YA_RENDER_GRAPH_API void assertBufferDeclared(RGBufferHandle handle, const char* operation) const;
    YA_RENDER_GRAPH_API void assertTextureAccess(RGTextureHandle handle,
                                       std::initializer_list<ERGPassResourceAccess> allowed,
                                       const char* operation) const;
    YA_RENDER_GRAPH_API void assertBufferAccess(RGBufferHandle handle,
                                      std::initializer_list<ERGBufferAccess> allowed,
                                      const char* operation) const;

  public:
    RGRenderContext(
        const RenderGraph& graph,
        const RGPass& pass,
        const RenderGraphResourceRegistry& registry,
        const RGCompiledPassPlan* compiledPassPlan,
        ICommandBuffer& cmdBuf)
        : _graph(graph), _pass(pass), _registry(registry), _cmdBuf(cmdBuf), _compiledPassPlan(compiledPassPlan)
    {}

    /// Pass-scoped binding helpers.
    ///
    /// Resolves a declared graph handle to the descriptor info needed by
    /// existing descriptor update paths, and retains every resource the
    /// descriptor refers to until the command buffer completes. The context is
    /// backend-neutral: callers stay responsible for binding/set layout and
    /// descriptor type, while the graph guarantees the handle was declared and
    /// the resolved GPU object stays alive during command recording.
    class RGPassBindingContext
    {
      public:
        RGPassBindingContext(const RenderGraphResourceRegistry& registry,
                             const RenderGraph&                  graph,
                             const RGPass&                       pass,
                             ICommandBuffer&                    cmdBuf)
            : _registry(registry), _graph(graph), _pass(pass), _cmdBuf(cmdBuf)
        {}

        [[nodiscard]] YA_RENDER_GRAPH_API std::optional<DescriptorImageInfo> resolveTextureDescriptor(
            RGTextureHandle handle,
            Sampler*        sampler) const;
        [[nodiscard]] YA_RENDER_GRAPH_API std::optional<DescriptorBufferInfo> resolveBufferDescriptor(
            RGBufferHandle handle) const;

      private:
        [[nodiscard]] const RGTextureUsage* findDeclaredTextureUsage(RGTextureHandle handle) const;
        [[nodiscard]] const RGBufferUsage* findDeclaredBufferUsage(RGBufferHandle handle) const;

        const RenderGraphResourceRegistry& _registry;
        const RenderGraph&                  _graph;
        const RGPass&                       _pass;
        ICommandBuffer&                    _cmdBuf;
    };

    [[nodiscard]] const RGPass& getPass() const { return _pass; }
    [[nodiscard]] ICommandBuffer& getCommandBuffer() const { return _cmdBuf; }
    [[nodiscard]] RGPassBindingContext getBindingContext() const
    {
        return RGPassBindingContext(_registry, _graph, _pass, _cmdBuf);
    }
    [[nodiscard]] YA_RENDER_GRAPH_API const RGTextureResource& getTexture(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGBufferResource& getBuffer(RGBufferHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGTextureDesc& getTextureDesc(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGBufferDesc& getBufferDesc(RGBufferHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API bool hasDeclaredTextureUsage(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API bool hasDeclaredBufferUsage(RGBufferHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API bool hasDeclaredTextureAccess(RGTextureHandle handle, ERGPassResourceAccess access) const;
    [[nodiscard]] YA_RENDER_GRAPH_API bool hasDeclaredBufferAccess(RGBufferHandle handle, ERGBufferAccess access) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RenderTexture* resolveTexture(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API IBuffer* resolveBuffer(RGBufferHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGRasterPassDesc* getDeclaredRasterPlan() const;
    [[nodiscard]] YA_RENDER_GRAPH_API RasterPassExecutionParams getRasterPassExecutionParams() const;
    YA_RENDER_GRAPH_API void beginRasterRendering(const RasterRenderingDesc& desc) const;
    YA_RENDER_GRAPH_API void beginDeclaredRasterRendering() const;
    YA_RENDER_GRAPH_API void beginColorRendering(const ColorRenderingDesc& desc) const;
    YA_RENDER_GRAPH_API void endRendering() const;
    YA_RENDER_GRAPH_API void copyBuffer(RGBufferHandle src, RGBufferHandle dst, uint64_t size, uint64_t srcOffset = 0, uint64_t dstOffset = 0) const;
    YA_RENDER_GRAPH_API void copyTextureToBuffer(
        RGTextureHandle src,
        RGBufferHandle  dst,
        const std::vector<BufferImageCopy>& regions) const;
    YA_RENDER_GRAPH_API void copyTexture(RGTextureHandle src, RGTextureHandle dst, const ImageCopy& region) const;
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

    YA_RENDER_GRAPH_API void read(RGTextureHandle handle);
    YA_RENDER_GRAPH_API void write(RGTextureHandle handle);
    YA_RENDER_GRAPH_API void uniformRead(RGBufferHandle handle, RGBufferRange range = {});
    YA_RENDER_GRAPH_API void storageRead(RGBufferHandle handle, RGBufferRange range = {});
    YA_RENDER_GRAPH_API void storageWrite(RGBufferHandle handle, RGBufferRange range = {});
    YA_RENDER_GRAPH_API void storageReadWrite(RGBufferHandle handle, RGBufferRange range = {});
    YA_RENDER_GRAPH_API void indirectRead(RGBufferHandle handle, RGBufferRange range = {});
    YA_RENDER_GRAPH_API void transferSrc(RGBufferHandle handle, RGBufferRange range = {});
    YA_RENDER_GRAPH_API void transferDst(RGBufferHandle handle, RGBufferRange range = {});
    YA_RENDER_GRAPH_API void dependsOn(RGPassHandle handle);
    YA_RENDER_GRAPH_API void declareCompute();
    YA_RENDER_GRAPH_API void declareCopy();
    YA_RENDER_GRAPH_API void declareRaster(const RGRasterPassDesc& desc);
    YA_RENDER_GRAPH_API void useColorAttachment(RGTextureHandle handle);
    YA_RENDER_GRAPH_API void useDepthAttachment(RGTextureHandle handle);
    YA_RENDER_GRAPH_API void transferSrc(RGTextureHandle handle);
    YA_RENDER_GRAPH_API void transferDst(RGTextureHandle handle);
};

class RenderGraph
{
  private:
    struct RGTextureExportRequest
    {
        std::string     name;
        RGTextureHandle texture{};
    };

    uint32_t _nextTextureGeneration = 1;
    uint32_t _nextBufferGeneration  = 1;
    uint32_t _nextPassGeneration    = 1;
    std::vector<RGTextureResource> _textures;
    std::vector<RGBufferResource>  _buffers;
    std::vector<RGPass>            _passes;
    std::vector<RGTextureExportRequest> _textureExports;

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
    [[nodiscard]] YA_RENDER_GRAPH_API RGTextureHandle createTexture(const RGTextureDesc& desc, ERGResourceLifetime lifetime = ERGResourceLifetime::Transient);
    [[nodiscard]] YA_RENDER_GRAPH_API RGTextureHandle createPersistentTexture(const RGTextureDesc& desc, const RGPersistentTextureKey& key);
    [[nodiscard]] YA_RENDER_GRAPH_API RGTextureHandle importTexture(const RGImportedTextureDesc& desc);

    [[nodiscard]] YA_RENDER_GRAPH_API RGBufferHandle createBuffer(const RGBufferDesc& desc, ERGResourceLifetime lifetime = ERGResourceLifetime::Transient);
    [[nodiscard]] YA_RENDER_GRAPH_API RGBufferHandle createPersistentBuffer(const RGBufferDesc& desc, const RGPersistentBufferKey& key);
    [[nodiscard]] YA_RENDER_GRAPH_API RGBufferHandle importBuffer(const RGImportedBufferDesc& desc);

    [[nodiscard]] YA_RENDER_GRAPH_API const RGTextureResource* getTexture(RGTextureHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGBufferResource* getBuffer(RGBufferHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API const RGPass* getPass(RGPassHandle handle) const;

    [[nodiscard]] YA_RENDER_GRAPH_API RGPassHandle addPass(
        const std::string& name,
        const std::function<void(RGPassBuilder&)>& setup,
        const std::function<void(RGRenderContext&)>& execute = {});
    [[nodiscard]] YA_RENDER_GRAPH_API RGCompiledGraph compile() const;
    [[nodiscard]] YA_RENDER_GRAPH_API RGTopologyDescription describeCompiledTopology(const RGCompiledGraph& compiled) const;
    [[nodiscard]] YA_RENDER_GRAPH_API std::optional<RGPassContext> createPassContext(RGPassHandle handle) const;
    [[nodiscard]] YA_RENDER_GRAPH_API std::string debugDump(const RGCompiledGraph& compiled) const;
    YA_RENDER_GRAPH_API void exportTexture(RGTextureHandle handle, std::string name);

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

template <>
struct hash<ya::RGPersistentTextureKey>
{
    std::size_t operator()(const ya::RGPersistentTextureKey& key) const noexcept
    {
        return std::hash<std::string>{}(key.value);
    }
};

template <>
struct hash<ya::RGPersistentBufferKey>
{
    std::size_t operator()(const ya::RGPersistentBufferKey& key) const noexcept
    {
        return std::hash<std::string>{}(key.value);
    }
};

} // namespace std

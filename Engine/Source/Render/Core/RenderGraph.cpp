#include "RenderGraph.h"
#include "RenderGraphResourceRegistry.h"
#include "RenderingInfoUtils.h"

#include <algorithm>
#include <deque>
#include <sstream>

namespace ya
{

namespace
{

template <typename UsageT>
bool containsUsage(const std::vector<UsageT>& usages, const UsageT& needle)
{
    return std::find(usages.begin(), usages.end(), needle) != usages.end();
}

bool hasImageUsage(EImageUsage::T haystack, EImageUsage::T needle)
{
    return (haystack & needle) == needle;
}

bool hasBufferUsage(EBufferUsage haystack, EBufferUsage needle)
{
    return (haystack & needle) == needle;
}

bool isTransferTextureAccess(ERGPassResourceAccess access)
{
    return access == ERGPassResourceAccess::TransferSrc || access == ERGPassResourceAccess::TransferDst;
}

bool isTransferBufferAccess(ERGBufferAccess access)
{
    return access == ERGBufferAccess::TransferRead || access == ERGBufferAccess::TransferWrite;
}

const char* toString(ERGPassKind kind)
{
    switch (kind) {
        case ERGPassKind::Unknown: return "Unknown";
        case ERGPassKind::Raster: return "Raster";
        case ERGPassKind::Compute: return "Compute";
        case ERGPassKind::Copy: return "Copy";
    }
    return "Unknown";
}

uint32_t graphDefaultAspectMask(EFormat::T format)
{
    if (EFormat::isDepthStencilFormat(format)) {
        return EImageAspect::DepthStencil;
    }
    if (EFormat::isDepthFormat(format)) {
        return EImageAspect::Depth;
    }
        return EImageAspect::Color;
}

ImageSubresourceRange makeFullRange(const RGTextureDesc& desc)
{
    return ImageSubresourceRange{
        .aspectMask     = graphDefaultAspectMask(desc.format),
        .baseMipLevel   = 0,
        .levelCount     = desc.mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = desc.arrayLayers,
    };
}

ImageSubresourceRange makeImportedViewRange(const RGTextureResource& resource)
{
    if (resource.imported && resource.imported->subresourceRange.has_value()) {
        return *resource.imported->subresourceRange;
    }
    if (resource.imported && resource.imported->viewDesc.has_value()) {
        const auto& viewDesc = *resource.imported->viewDesc;
        return ImageSubresourceRange{
            .aspectMask     = viewDesc.aspectFlags != EImageAspect::None ? viewDesc.aspectFlags : graphDefaultAspectMask(resource.desc.format),
            .baseMipLevel   = viewDesc.baseMipLevel,
            .levelCount     = viewDesc.levelCount,
            .baseArrayLayer = viewDesc.baseArrayLayer,
            .layerCount     = viewDesc.layerCount,
        };
    }
    return makeFullRange(resource.desc);
}

void normalizeImportedTextureDesc(RGImportedTextureDesc& importedDesc)
{
    if (importedDesc.image) {
        const IImage& image = *importedDesc.image;
        const auto    nativeHandle = static_cast<void*>(image.getHandle());

        YA_CORE_ASSERT(importedDesc.importDesc.nativeHandle == nullptr || importedDesc.importDesc.nativeHandle == nativeHandle,
                       "Imported texture image/native handle mismatch for '{}'",
                       importedDesc.importDesc.label.empty() ? importedDesc.desc.label : importedDesc.importDesc.label);
        YA_CORE_ASSERT(importedDesc.importDesc.format == EFormat::Undefined || importedDesc.importDesc.format == image.getFormat(),
                       "Imported texture image/format mismatch");
        YA_CORE_ASSERT(importedDesc.importDesc.usage == EImageUsage::None || hasImageUsage(image.getUsage(), importedDesc.importDesc.usage),
                       "Imported texture image/usage mismatch");
        YA_CORE_ASSERT((importedDesc.importDesc.extent.width == 0 && importedDesc.importDesc.extent.height == 0 && importedDesc.importDesc.extent.depth == 0) ||
                           (importedDesc.importDesc.extent.width == image.getWidth() &&
                            importedDesc.importDesc.extent.height == image.getHeight() &&
                            importedDesc.importDesc.extent.depth == 1),
                       "Imported texture image/extent mismatch");
        YA_CORE_ASSERT(importedDesc.importDesc.mipLevels == 1 || importedDesc.importDesc.mipLevels == image.getMipLevels(),
                       "Imported texture image/mip mismatch");
        YA_CORE_ASSERT(importedDesc.importDesc.arrayLayers == 1 || importedDesc.importDesc.arrayLayers == image.getArrayLayers(),
                       "Imported texture image/array-layer mismatch");

        importedDesc.importDesc.nativeHandle = nativeHandle;
        importedDesc.importDesc.format       = image.getFormat();
        importedDesc.importDesc.usage        = image.getUsage();
        importedDesc.importDesc.extent       = Extent3D{image.getWidth(), image.getHeight(), 1};
        importedDesc.importDesc.mipLevels    = image.getMipLevels();
        importedDesc.importDesc.arrayLayers  = image.getArrayLayers();
    }

    auto& desc = importedDesc.desc;
    if (desc.format == EFormat::Undefined) {
        desc.format = importedDesc.importDesc.format;
    }
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        desc.extent = importedDesc.importDesc.extent;
    }
    if (desc.usage == EImageUsage::None) {
        desc.usage = importedDesc.importDesc.usage;
    }
    if (desc.label.empty()) {
        desc.label = importedDesc.importDesc.label;
    }
    if (desc.mipLevels == 1 && importedDesc.importDesc.mipLevels != 1) {
        desc.mipLevels = importedDesc.importDesc.mipLevels;
    }
    if (desc.arrayLayers == 1 && importedDesc.importDesc.arrayLayers != 1) {
        desc.arrayLayers = importedDesc.importDesc.arrayLayers;
    }
}

ImageResourceState makeTextureState(const RGTextureResource& resource, ERGPassResourceAccess access)
{
    ImageResourceState state{
        .subresourceRange = makeImportedViewRange(resource),
    };

    switch (access) {
        case ERGPassResourceAccess::Read:
            state.stages = EPipelineStage::FragmentShader;
            state.access = EResourceAccess::ShaderRead;
            state.layout = EImageLayout::ShaderReadOnlyOptimal;
            break;
        case ERGPassResourceAccess::Write:
            state.stages = EPipelineStage::ComputeShader;
            state.access = EResourceAccess::ShaderWrite;
            state.layout = EImageLayout::General;
            break;
        case ERGPassResourceAccess::ColorAttachment:
            state.stages = EPipelineStage::ColorAttachmentOutput;
            state.access = EResourceAccess::ColorAttachmentWrite;
            state.layout = EImageLayout::ColorAttachmentOptimal;
            break;
        case ERGPassResourceAccess::DepthAttachment:
            state.stages = static_cast<EPipelineStage::T>(EPipelineStage::EarlyFragmentTests | EPipelineStage::LateFragmentTests);
            state.access = static_cast<EResourceAccess::T>(
                EResourceAccess::DepthStencilAttachmentRead | EResourceAccess::DepthStencilAttachmentWrite);
            state.layout = EImageLayout::DepthStencilAttachmentOptimal;
            break;
        case ERGPassResourceAccess::TransferSrc:
            state.stages = EPipelineStage::Transfer;
            state.access = EResourceAccess::TransferRead;
            state.layout = EImageLayout::TransferSrc;
            break;
        case ERGPassResourceAccess::TransferDst:
            state.stages = EPipelineStage::Transfer;
            state.access = EResourceAccess::TransferWrite;
            state.layout = EImageLayout::TransferDst;
            break;
    }

    return state;
}

BufferResourceState makeBufferState(const RGBufferResource& resource, ERGBufferAccess access)
{
    BufferResourceState state{
        .offset = 0,
        .size   = resource.desc.size,
    };

    switch (access) {
        case ERGBufferAccess::ShaderRead:
            state.stages = EPipelineStage::AllCommands;
            state.access = EResourceAccess::ShaderRead;
            break;
        case ERGBufferAccess::ShaderWrite:
            state.stages = EPipelineStage::AllCommands;
            state.access = EResourceAccess::ShaderWrite;
            break;
        case ERGBufferAccess::ShaderReadWrite:
            state.stages = EPipelineStage::ComputeShader;
            state.access = static_cast<EResourceAccess::T>(EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite);
            break;
        case ERGBufferAccess::IndirectRead:
            state.stages = EPipelineStage::DrawIndirect;
            state.access = EResourceAccess::IndirectCommandRead;
            break;
        case ERGBufferAccess::TransferRead:
            state.stages = EPipelineStage::Transfer;
            state.access = EResourceAccess::TransferRead;
            break;
        case ERGBufferAccess::TransferWrite:
            state.stages = EPipelineStage::Transfer;
            state.access = EResourceAccess::TransferWrite;
            break;
    }

    return state;
}

void retainResolvedRenderImage(ICommandBuffer& cmdBuf, const RenderImage& image)
{
    cmdBuf.retainResource(image.getImageShared());
    cmdBuf.retainResource(image.getImageViewShared());
    cmdBuf.retainResources(image.getRetainedResources());
}

} // namespace

const RGTextureResource& RGPassContext::getTexture(RGTextureHandle handle) const
{
    const auto* resource = _graph.getTexture(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGPassContext pass {} references invalid texture handle {}", _pass.name, handle.index);
    return *resource;
}

const RGBufferResource& RGPassContext::getBuffer(RGBufferHandle handle) const
{
    const auto* resource = _graph.getBuffer(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGPassContext pass {} references invalid buffer handle {}", _pass.name, handle.index);
    return *resource;
}

const RGTextureDesc& RGPassContext::getTextureDesc(RGTextureHandle handle) const
{
    return getTexture(handle).desc;
}

const RGBufferDesc& RGPassContext::getBufferDesc(RGBufferHandle handle) const
{
    return getBuffer(handle).desc;
}

const RGTextureResource& RGRenderContext::getTexture(RGTextureHandle handle) const
{
    const auto* resource = _graph.getTexture(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGRenderContext pass {} references invalid texture handle {}", _pass.name, handle.index);
    return *resource;
}

const RGBufferResource& RGRenderContext::getBuffer(RGBufferHandle handle) const
{
    const auto* resource = _graph.getBuffer(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGRenderContext pass {} references invalid buffer handle {}", _pass.name, handle.index);
    return *resource;
}

const RGTextureDesc& RGRenderContext::getTextureDesc(RGTextureHandle handle) const
{
    return getTexture(handle).desc;
}

const RGBufferDesc& RGRenderContext::getBufferDesc(RGBufferHandle handle) const
{
    return getBuffer(handle).desc;
}

const RenderImage* RGRenderContext::resolveTexture(RGTextureHandle handle) const
{
    const auto* texture = _registry.resolveTexture(handle);
    if (texture) {
        retainResolvedRenderImage(_cmdBuf, *texture);
    }
    return texture;
}

IBuffer* RGRenderContext::resolveBuffer(RGBufferHandle handle) const
{
    const auto* resource = _graph.getBuffer(handle);
    YA_CORE_ASSERT(resource != nullptr, "RGRenderContext pass {} references invalid buffer handle {}", _pass.name, handle.index);
    if (resource->imported.has_value()) {
        _cmdBuf.retainResources(resource->imported->retainedResources);
    }
    return _registry.resolveBuffer(handle);
}

const RGRasterPassDesc* RGRenderContext::getDeclaredRasterPlan() const
{
    if (_compiledPassPlan == nullptr || !_compiledPassPlan->rasterPlan.has_value()) {
        return nullptr;
    }
    return &*_compiledPassPlan->rasterPlan;
}

RGRenderContext::RasterPassExecutionParams RGRenderContext::getRasterPassExecutionParams() const
{
    const auto* rasterPlan = getDeclaredRasterPlan();
    YA_CORE_ASSERT(rasterPlan != nullptr,
                   "RGRenderContext pass {} has no declared raster execution params",
                   _pass.name);
    return RasterPassExecutionParams{.rasterPlan = *rasterPlan};
}

void RGRenderContext::beginColorRendering(const ColorRenderingDesc& desc) const
{
    beginRasterRendering(RasterRenderingDesc{
        .renderArea = desc.renderArea,
        .layerCount = desc.layerCount,
        .colors = {{
            .color       = desc.color,
            .clearValue  = desc.clearValue,
            .loadOp      = desc.loadOp,
            .storeOp     = desc.storeOp,
            .finalLayout = desc.finalLayout,
        }},
    });
}

void RGRenderContext::beginDeclaredRasterRendering() const
{
    YA_CORE_ASSERT(_compiledPassPlan != nullptr,
                   "RGRenderContext pass {} is missing compiled pass plan for declared raster rendering",
                   _pass.name);
    YA_CORE_ASSERT(_compiledPassPlan->rasterPlan.has_value(),
                   "RGRenderContext pass {} has no declared raster rendering plan",
                   _pass.name);
    beginRasterRendering(*_compiledPassPlan->rasterPlan);
}

void RGRenderContext::beginRasterRendering(const RasterRenderingDesc& desc) const
{
    YA_CORE_ASSERT(!desc.colors.empty() || desc.depth.has_value(),
                   "RGRenderContext pass {} requires at least one attachment", _pass.name);

    RenderAttachmentSet attachments{
        .renderArea = desc.renderArea,
        .layerCount = desc.layerCount,
    };
    attachments.colors.reserve(desc.colors.size());

    for (const auto& colorDesc : desc.colors) {
        const auto* color = resolveTexture(colorDesc.color);
        YA_CORE_ASSERT(color != nullptr, "RGRenderContext pass {} failed to resolve color target {}", _pass.name, colorDesc.color.index);
        YA_CORE_ASSERT(color->getImage() != nullptr && color->getImageView() != nullptr,
                       "RGRenderContext pass {} color target {} is missing image/view", _pass.name, colorDesc.color.index);
        retainResolvedRenderImage(_cmdBuf, *color);
        auto attachment = makeRenderAttachment(
            color->getImageView(),
            colorDesc.loadOp,
            colorDesc.storeOp,
            EImageLayout::ColorAttachmentOptimal,
            colorDesc.finalLayout,
            colorDesc.clearValue);
        if (colorDesc.resolve.isValid()) {
            const auto* resolve = resolveTexture(colorDesc.resolve);
            YA_CORE_ASSERT(resolve != nullptr,
                           "RGRenderContext pass {} failed to resolve color-resolve target {}",
                           _pass.name,
                           colorDesc.resolve.index);
            YA_CORE_ASSERT(resolve->getImage() != nullptr && resolve->getImageView() != nullptr,
                           "RGRenderContext pass {} color-resolve target {} is missing image/view",
                           _pass.name,
                           colorDesc.resolve.index);
            retainResolvedRenderImage(_cmdBuf, *resolve);
            attachment.resolveImage     = resolve->getImage();
            attachment.resolveImageView = resolve->getImageView();
            attachment.resolveMode      = colorDesc.resolveMode;
        }
        attachments.colors.push_back(std::move(attachment));
    }

    if (desc.depth.has_value()) {
        const auto* depth = resolveTexture(desc.depth->depth);
        YA_CORE_ASSERT(depth != nullptr, "RGRenderContext pass {} failed to resolve depth target {}", _pass.name, desc.depth->depth.index);
        YA_CORE_ASSERT(depth->getImage() != nullptr && depth->getImageView() != nullptr,
                       "RGRenderContext pass {} depth target {} is missing image/view", _pass.name, desc.depth->depth.index);
        retainResolvedRenderImage(_cmdBuf, *depth);

        attachments.depth = makeRenderAttachment(
            depth->getImageView(),
            desc.depth->loadOp,
            desc.depth->storeOp,
            EImageLayout::DepthStencilAttachmentOptimal,
            desc.depth->finalLayout,
            desc.depth->clearValue);
    }

    _activeRenderingInfo = RenderingInfo{
        .label       = _pass.name,
        .attachments = std::move(attachments),
    };
    _cmdBuf.beginRendering(*_activeRenderingInfo);
}

void RGRenderContext::endRendering() const
{
    _cmdBuf.endRendering(_activeRenderingInfo.value_or(RenderingInfo{}));
    _activeRenderingInfo.reset();
}

void RGRenderContext::copyBuffer(RGBufferHandle src, RGBufferHandle dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset) const
{
    auto* srcBuffer = resolveBuffer(src);
    auto* dstBuffer = resolveBuffer(dst);
    YA_CORE_ASSERT(srcBuffer != nullptr, "RGRenderContext pass {} failed to resolve src buffer {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstBuffer != nullptr, "RGRenderContext pass {} failed to resolve dst buffer {}", _pass.name, dst.index);
    _cmdBuf.copyBuffer(srcBuffer, dstBuffer, size, srcOffset, dstOffset);
}

void RGRenderContext::copyTextureToBuffer(
    RGTextureHandle src,
    RGBufferHandle  dst,
    const std::vector<BufferImageCopy>& regions) const
{
    const auto* srcTexture = resolveTexture(src);
    auto*       dstBuffer  = resolveBuffer(dst);
    YA_CORE_ASSERT(srcTexture != nullptr, "RGRenderContext pass {} failed to resolve src texture {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstBuffer != nullptr, "RGRenderContext pass {} failed to resolve dst buffer {}", _pass.name, dst.index);
    YA_CORE_ASSERT(srcTexture->getImage() != nullptr, "RGRenderContext pass {} src texture {} is missing image", _pass.name, src.index);
    retainResolvedRenderImage(_cmdBuf, *srcTexture);

    _cmdBuf.copyImageToBuffer(
        srcTexture->getImage(),
        EImageLayout::TransferSrc,
        dstBuffer,
        regions);
}

void RGRenderContext::copyTexture(RGTextureHandle src, RGTextureHandle dst, const ImageCopy& region) const
{
    const auto* srcTexture = resolveTexture(src);
    const auto* dstTexture = resolveTexture(dst);
    YA_CORE_ASSERT(srcTexture != nullptr, "RGRenderContext pass {} failed to resolve src texture {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstTexture != nullptr, "RGRenderContext pass {} failed to resolve dst texture {}", _pass.name, dst.index);
    YA_CORE_ASSERT(srcTexture->getImage() != nullptr, "RGRenderContext pass {} src texture {} is missing image", _pass.name, src.index);
    YA_CORE_ASSERT(dstTexture->getImage() != nullptr, "RGRenderContext pass {} dst texture {} is missing image", _pass.name, dst.index);
    retainResolvedRenderImage(_cmdBuf, *srcTexture);
    retainResolvedRenderImage(_cmdBuf, *dstTexture);

    _cmdBuf.copyImage(
        srcTexture->getImage(),
        EImageLayout::TransferSrc,
        dstTexture->getImage(),
        EImageLayout::TransferDst,
        {region});
}

RGPass& RGPassBuilder::pass()
{
    return const_cast<RGPass&>(_graph.getPasses()[_passIndex]);
}

void RGPassBuilder::read(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::Read});
}

void RGPassBuilder::write(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::Write});
}

void RGPassBuilder::read(RGBufferHandle handle)
{
    pass().buffers.push_back({.handle = handle, .access = ERGBufferAccess::ShaderRead});
}

void RGPassBuilder::write(RGBufferHandle handle)
{
    pass().buffers.push_back({.handle = handle, .access = ERGBufferAccess::ShaderWrite});
}

void RGPassBuilder::readWrite(RGBufferHandle handle)
{
    pass().buffers.push_back({.handle = handle, .access = ERGBufferAccess::ShaderReadWrite});
}

void RGPassBuilder::indirectRead(RGBufferHandle handle)
{
    pass().buffers.push_back({.handle = handle, .access = ERGBufferAccess::IndirectRead});
}

void RGPassBuilder::transferSrc(RGBufferHandle handle)
{
    pass().buffers.push_back({.handle = handle, .access = ERGBufferAccess::TransferRead});
}

void RGPassBuilder::transferDst(RGBufferHandle handle)
{
    pass().buffers.push_back({.handle = handle, .access = ERGBufferAccess::TransferWrite});
}

void RGPassBuilder::dependsOn(RGPassHandle handle)
{
    pass().dependencies.push_back(handle);
}

void RGPassBuilder::declareCompute()
{
    pass().kind = ERGPassKind::Compute;
}

void RGPassBuilder::declareCopy()
{
    pass().kind = ERGPassKind::Copy;
}

void RGPassBuilder::declareRaster(const RGRasterPassDesc& desc)
{
    auto& currentPass = pass();
    currentPass.kind = ERGPassKind::Raster;
    currentPass.rasterDesc = desc;

    for (const auto& color : desc.colors) {
        currentPass.textures.push_back({.handle = color.color, .access = ERGPassResourceAccess::ColorAttachment});
        if (color.resolve.isValid()) {
            currentPass.textures.push_back({.handle = color.resolve, .access = ERGPassResourceAccess::ColorAttachment});
        }
    }

    if (desc.depth.has_value()) {
        currentPass.textures.push_back({.handle = desc.depth->depth, .access = ERGPassResourceAccess::DepthAttachment});
    }
}

void RGPassBuilder::useColorAttachment(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::ColorAttachment});
}

void RGPassBuilder::useDepthAttachment(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::DepthAttachment});
}

void RGPassBuilder::transferSrc(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::TransferSrc});
}

void RGPassBuilder::transferDst(RGTextureHandle handle)
{
    pass().textures.push_back({.handle = handle, .access = ERGPassResourceAccess::TransferDst});
}

RGTextureHandle RenderGraph::createTexture(const RGTextureDesc& desc, ERGResourceLifetime lifetime)
{
    YA_CORE_ASSERT(lifetime != ERGResourceLifetime::Imported, "Imported texture must use importTexture()");
    YA_CORE_ASSERT(desc.format != EFormat::Undefined, "RenderGraph texture desc format must be defined");
    YA_CORE_ASSERT(desc.extent.width > 0 && desc.extent.height > 0 && desc.extent.depth > 0,
                   "RenderGraph texture extent must be non-zero");

    RGTextureHandle handle{
        .index      = static_cast<uint32_t>(_textures.size()),
        .generation = _nextTextureGeneration++,
    };
    _textures.push_back(RGTextureResource{
        .handle   = handle,
        .lifetime = lifetime,
        .desc     = desc,
    });
    return handle;
}

RGTextureHandle RenderGraph::createPersistentTexture(const RGTextureDesc& desc, const RGPersistentTextureKey& key)
{
    YA_CORE_ASSERT(key.isValid(), "Persistent texture key must not be empty for '{}'", desc.label);

    const auto handle = createTexture(desc, ERGResourceLifetime::Persistent);
    _textures.back().persistentKey = key;
    return handle;
}

RGTextureHandle RenderGraph::importTexture(const RGImportedTextureDesc& importedDesc)
{
    YA_CORE_ASSERT(importedDesc.image || importedDesc.importDesc.nativeHandle != nullptr,
                   "Imported texture requires either shared image or native handle");

    auto normalizedImported = importedDesc;
    normalizeImportedTextureDesc(normalizedImported);

    RGTextureHandle handle{
        .index      = static_cast<uint32_t>(_textures.size()),
        .generation = _nextTextureGeneration++,
    };
    _textures.push_back(RGTextureResource{
        .handle   = handle,
        .lifetime = ERGResourceLifetime::Imported,
        .desc     = normalizedImported.desc,
        .imported = normalizedImported,
    });
    return handle;
}

RGBufferHandle RenderGraph::createBuffer(const RGBufferDesc& desc, ERGResourceLifetime lifetime)
{
    YA_CORE_ASSERT(lifetime != ERGResourceLifetime::Imported, "Imported buffer must use importBuffer()");
    YA_CORE_ASSERT(desc.size > 0, "RenderGraph buffer size must be non-zero");

    RGBufferHandle handle{
        .index      = static_cast<uint32_t>(_buffers.size()),
        .generation = _nextBufferGeneration++,
    };
    _buffers.push_back(RGBufferResource{
        .handle   = handle,
        .lifetime = lifetime,
        .desc     = desc,
    });
    return handle;
}

RGBufferHandle RenderGraph::createPersistentBuffer(const RGBufferDesc& desc, const RGPersistentBufferKey& key)
{
    YA_CORE_ASSERT(key.isValid(), "Persistent buffer key must not be empty for '{}'", desc.label);

    const auto handle = createBuffer(desc, ERGResourceLifetime::Persistent);
    _buffers.back().persistentKey = key;
    return handle;
}

RGBufferHandle RenderGraph::importBuffer(const RGImportedBufferDesc& importedDesc)
{
    YA_CORE_ASSERT(importedDesc.buffer != nullptr, "Imported buffer must not be null");

    RGBufferHandle handle{
        .index      = static_cast<uint32_t>(_buffers.size()),
        .generation = _nextBufferGeneration++,
    };
    _buffers.push_back(RGBufferResource{
        .handle   = handle,
        .lifetime = ERGResourceLifetime::Imported,
        .desc     = importedDesc.desc,
        .imported = importedDesc,
    });
    return handle;
}

const RGTextureResource* RenderGraph::getTexture(RGTextureHandle handle) const
{
    return findResource(_textures, handle);
}

const RGBufferResource* RenderGraph::getBuffer(RGBufferHandle handle) const
{
    return findResource(_buffers, handle);
}

const RGPass* RenderGraph::getPass(RGPassHandle handle) const
{
    return findResource(_passes, handle);
}

RGPassHandle RenderGraph::addPass(
    const std::string& name,
    const std::function<void(RGPassBuilder&)>& setup,
    const std::function<void(RGRenderContext&)>& execute)
{
    RGPassHandle handle{
        .index      = static_cast<uint32_t>(_passes.size()),
        .generation = _nextPassGeneration++,
    };
    _passes.push_back(RGPass{
        .handle  = handle,
        .name    = name,
        .execute = execute,
    });

    RGPassBuilder builder(*this, _passes.size() - 1);
    setup(builder);
    return handle;
}

RGCompiledGraph RenderGraph::compile() const
{
    RGCompiledGraph compiled;
    compiled.order.reserve(_passes.size());
    compiled.passPlans.reserve(_passes.size());
    compiled.importedTextureFinalizes.reserve(_textures.size());
    compiled.importedBufferFinalizes.reserve(_buffers.size());
    compiled.transientBufferLifetimes.reserve(_buffers.size());

    std::unordered_map<RGTextureHandle, RGPassHandle> textureWriters;
    std::unordered_map<RGBufferHandle, RGPassHandle>  bufferWriters;
    std::unordered_map<std::string, const RGTextureResource*> persistentTexturesByKey;
    std::unordered_map<std::string, const RGBufferResource*>  persistentBuffersByKey;
    std::vector<std::vector<uint32_t>> adjacency(_passes.size());
    std::vector<uint32_t> indegree(_passes.size(), 0);
    std::vector<RGCompiledPassPlan> passPlans(_passes.size());

    for (size_t i = 0; i < _passes.size(); ++i) {
        passPlans[i].pass = _passes[i].handle;
        passPlans[i].rasterPlan = _passes[i].rasterDesc;
    }

    const auto addDependency = [&](RGPassHandle from, RGPassHandle to) {
        if (!from.isValid() || !to.isValid() || from == to) {
            return;
        }
        RGDependencyEdge edge{.from = from, .to = to};
        if (containsUsage(compiled.dependencies, edge)) {
            return;
        }
        compiled.dependencies.push_back(edge);
        adjacency[from.index].push_back(to.index);
        ++indegree[to.index];
    };

    const auto addIssue = [&](RGCompileIssue::EKind kind, RGPassHandle pass, std::string message) {
        compiled.issues.push_back({
            .kind    = kind,
            .pass    = pass,
            .message = std::move(message),
        });
    };

    const auto resolvePassKind = [](const RGPass& pass) {
        if (pass.kind != ERGPassKind::Unknown) {
            return pass.kind;
        }
        if (pass.rasterDesc.has_value()) {
            return ERGPassKind::Raster;
        }

        bool hasRasterUsage      = false;
        bool hasTransferUsage    = false;
        bool hasNonTransferUsage = false;

        for (const auto& usage : pass.textures) {
            if (usage.access == ERGPassResourceAccess::ColorAttachment ||
                usage.access == ERGPassResourceAccess::DepthAttachment) {
                hasRasterUsage = true;
            }
            else if (isTransferTextureAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasNonTransferUsage = true;
            }
        }

        for (const auto& usage : pass.buffers) {
            if (isTransferBufferAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasNonTransferUsage = true;
            }
        }

        if (hasRasterUsage) {
            return ERGPassKind::Raster;
        }
        if (hasTransferUsage && !hasNonTransferUsage) {
            return ERGPassKind::Copy;
        }
        return ERGPassKind::Compute;
    };

    const auto validatePassKind = [&](const RGPass& pass, ERGPassKind kind) {
        bool hasRasterUsage   = false;
        bool hasTransferUsage = false;
        bool hasOtherUsage    = false;

        for (const auto& usage : pass.textures) {
            if (usage.access == ERGPassResourceAccess::ColorAttachment ||
                usage.access == ERGPassResourceAccess::DepthAttachment) {
                hasRasterUsage = true;
            }
            else if (isTransferTextureAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasOtherUsage = true;
            }
        }
        for (const auto& usage : pass.buffers) {
            if (isTransferBufferAccess(usage.access)) {
                hasTransferUsage = true;
            }
            else {
                hasOtherUsage = true;
            }
        }

        switch (kind) {
            case ERGPassKind::Raster:
                if (!pass.rasterDesc.has_value() && !hasRasterUsage) {
                    addIssue(RGCompileIssue::EKind::InvalidPassKind,
                             pass.handle,
                             std::format("pass {} is Raster but has no raster declaration or raster attachment usage", pass.name));
                }
                break;
            case ERGPassKind::Compute:
                if (pass.rasterDesc.has_value() || hasRasterUsage) {
                    addIssue(RGCompileIssue::EKind::InvalidPassKind,
                             pass.handle,
                             std::format("pass {} is Compute but declares raster attachments", pass.name));
                }
                break;
            case ERGPassKind::Copy:
                if (pass.rasterDesc.has_value() || hasRasterUsage || hasOtherUsage) {
                    addIssue(RGCompileIssue::EKind::InvalidPassKind,
                             pass.handle,
                             std::format("pass {} is Copy but uses non-transfer resources", pass.name));
                }
                if (!hasTransferUsage) {
                    addIssue(RGCompileIssue::EKind::InvalidPassKind,
                             pass.handle,
                             std::format("pass {} is Copy but has no transfer resource usage", pass.name));
                }
                break;
            case ERGPassKind::Unknown:
                break;
        }
    };

    for (const RGPass& pass : _passes) {
        passPlans[pass.handle.index].kind = resolvePassKind(pass);
        validatePassKind(pass, passPlans[pass.handle.index].kind);

        for (const RGHandle<RGPassHandleTag> dependency : pass.dependencies) {
            if (!getPass(dependency)) {
                addIssue(RGCompileIssue::EKind::InvalidResource, pass.handle,
                         std::format("pass {} depends on invalid pass handle", pass.name));
                continue;
            }
            addDependency(dependency, pass.handle);
        }

        for (const auto& usage : pass.textures) {
            const auto* resource = getTexture(usage.handle);
            if (!resource) {
                addIssue(RGCompileIssue::EKind::InvalidResource, pass.handle, std::format("pass {} references invalid texture handle", pass.name));
                continue;
            }

            const auto requiredUsage = [&]() {
                switch (usage.access) {
                    case ERGPassResourceAccess::Read:
                        return EImageUsage::Sampled;
                    case ERGPassResourceAccess::Write:
                        return EImageUsage::Storage;
                    case ERGPassResourceAccess::ColorAttachment:
                        return EImageUsage::ColorAttachment;
                    case ERGPassResourceAccess::DepthAttachment:
                        return EImageUsage::DepthStencilAttachment;
                    case ERGPassResourceAccess::TransferSrc:
                        return EImageUsage::TransferSrc;
                    case ERGPassResourceAccess::TransferDst:
                        return EImageUsage::TransferDst;
                }
                return EImageUsage::None;
            }();
            if (requiredUsage != EImageUsage::None && !hasImageUsage(resource->desc.usage, requiredUsage)) {
                addIssue(RGCompileIssue::EKind::InvalidUsage, pass.handle,
                         std::format("pass {} uses texture {} with incompatible usage flags", pass.name, resource->desc.label));
                continue;
            }

            passPlans[pass.handle.index].textureStates.push_back({
                .pass          = pass.handle,
                .texture       = usage.handle,
                .requiredState = makeTextureState(*resource, usage.access),
            });

            const bool bWrite =
                usage.access == ERGPassResourceAccess::Write ||
                usage.access == ERGPassResourceAccess::ColorAttachment ||
                usage.access == ERGPassResourceAccess::DepthAttachment ||
                usage.access == ERGPassResourceAccess::TransferDst;

            if (!bWrite) {
                const auto writerIt = textureWriters.find(usage.handle);
                if (writerIt == textureWriters.end() && resource->lifetime != ERGResourceLifetime::Imported) {
                    addIssue(RGCompileIssue::EKind::ReadBeforeWrite, pass.handle,
                             std::format("pass {} reads texture {} before any writer", pass.name, resource->desc.label));
                } else if (writerIt != textureWriters.end()) {
                    addDependency(writerIt->second, pass.handle);
                }
                continue;
            }

            if (const auto writerIt = textureWriters.find(usage.handle); writerIt != textureWriters.end()) {
                addDependency(writerIt->second, pass.handle);
            }
            textureWriters[usage.handle] = pass.handle;
        }

        for (const auto& usage : pass.buffers) {
            const auto* resource = getBuffer(usage.handle);
            if (!resource) {
                addIssue(RGCompileIssue::EKind::InvalidResource, pass.handle, std::format("pass {} references invalid buffer handle", pass.name));
                continue;
            }

            const bool bCompatibleUsage = [&]() {
                switch (usage.access) {
                    case ERGBufferAccess::ShaderRead:
                        return hasBufferUsage(resource->desc.usage, EBufferUsage::StorageBuffer) ||
                               hasBufferUsage(resource->desc.usage, EBufferUsage::UniformBuffer);
                    case ERGBufferAccess::ShaderWrite:
                    case ERGBufferAccess::ShaderReadWrite:
                        return hasBufferUsage(resource->desc.usage, EBufferUsage::StorageBuffer);
                    case ERGBufferAccess::IndirectRead:
                        return hasBufferUsage(resource->desc.usage, EBufferUsage::IndirectBuffer);
                    case ERGBufferAccess::TransferRead:
                        return hasBufferUsage(resource->desc.usage, EBufferUsage::TransferSrc);
                    case ERGBufferAccess::TransferWrite:
                        return hasBufferUsage(resource->desc.usage, EBufferUsage::TransferDst);
                }
                return false;
            }();
            if (!bCompatibleUsage) {
                addIssue(RGCompileIssue::EKind::InvalidUsage, pass.handle,
                         std::format("pass {} uses buffer {} with unsupported usage flags", pass.name, resource->desc.label));
                continue;
            }

            passPlans[pass.handle.index].bufferStates.push_back({
                .pass          = pass.handle,
                .buffer        = usage.handle,
                .requiredState = makeBufferState(*resource, usage.access),
            });

            const bool bWrite = usage.access == ERGBufferAccess::ShaderWrite ||
                                usage.access == ERGBufferAccess::ShaderReadWrite ||
                                usage.access == ERGBufferAccess::TransferWrite;
            if (!bWrite) {
                const auto writerIt = bufferWriters.find(usage.handle);
                if (writerIt == bufferWriters.end() && resource->lifetime != ERGResourceLifetime::Imported) {
                    addIssue(RGCompileIssue::EKind::ReadBeforeWrite, pass.handle,
                             std::format("pass {} reads buffer {} before any writer", pass.name, resource->desc.label));
                } else if (writerIt != bufferWriters.end()) {
                    addDependency(writerIt->second, pass.handle);
                }
                continue;
            }

            if (const auto writerIt = bufferWriters.find(usage.handle); writerIt != bufferWriters.end()) {
                addDependency(writerIt->second, pass.handle);
            }
            bufferWriters[usage.handle] = pass.handle;
        }
    }

    for (const auto& texture : _textures) {
        if (texture.lifetime != ERGResourceLifetime::Persistent) {
            continue;
        }
        if (!texture.persistentKey.has_value() || !texture.persistentKey->isValid()) {
            addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                     RGPassHandle{},
                     std::format("persistent texture {} is missing a stable key", texture.desc.label));
            continue;
        }

        if (const auto existing = persistentTexturesByKey.find(texture.persistentKey->value);
            existing != persistentTexturesByKey.end()) {
            if (existing->second->desc.format != texture.desc.format ||
                existing->second->desc.extent.width != texture.desc.extent.width ||
                existing->second->desc.extent.height != texture.desc.extent.height ||
                existing->second->desc.extent.depth != texture.desc.extent.depth ||
                existing->second->desc.mipLevels != texture.desc.mipLevels ||
                existing->second->desc.arrayLayers != texture.desc.arrayLayers ||
                existing->second->desc.samples != texture.desc.samples ||
                existing->second->desc.usage != texture.desc.usage ||
                existing->second->desc.flags != texture.desc.flags) {
                addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                         RGPassHandle{},
                         std::format("persistent texture key '{}' maps to conflicting descriptors ('{}' vs '{}')",
                                     texture.persistentKey->value,
                                     existing->second->desc.label,
                                     texture.desc.label));
            }
            continue;
        }
        persistentTexturesByKey.emplace(texture.persistentKey->value, &texture);
    }

    for (const auto& buffer : _buffers) {
        if (buffer.lifetime != ERGResourceLifetime::Persistent) {
            continue;
        }
        if (!buffer.persistentKey.has_value() || !buffer.persistentKey->isValid()) {
            addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                     RGPassHandle{},
                     std::format("persistent buffer {} is missing a stable key", buffer.desc.label));
            continue;
        }

        if (const auto existing = persistentBuffersByKey.find(buffer.persistentKey->value);
            existing != persistentBuffersByKey.end()) {
            if (existing->second->desc.usage != buffer.desc.usage ||
                existing->second->desc.size != buffer.desc.size ||
                existing->second->desc.memoryUsage != buffer.desc.memoryUsage) {
                addIssue(RGCompileIssue::EKind::InvalidPersistentIdentity,
                         RGPassHandle{},
                         std::format("persistent buffer key '{}' maps to conflicting descriptors ('{}' vs '{}')",
                                     buffer.persistentKey->value,
                                     existing->second->desc.label,
                                     buffer.desc.label));
            }
            continue;
        }
        persistentBuffersByKey.emplace(buffer.persistentKey->value, &buffer);
    }

    std::deque<uint32_t> ready;
    for (uint32_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push_back(i);
        }
    }

    while (!ready.empty()) {
        const auto index = ready.front();
        ready.pop_front();
        compiled.order.push_back(_passes[index].handle);

        auto neighbors = adjacency[index];
        std::sort(neighbors.begin(), neighbors.end());
        for (const auto next : neighbors) {
            if (--indegree[next] == 0) {
                ready.push_back(next);
            }
        }
    }

    if (compiled.order.size() != _passes.size()) {
        compiled.order.clear();
        addIssue(RGCompileIssue::EKind::Cycle, RGPassHandle{}, "render graph contains a dependency cycle");
    }
    else {
        for (const auto& passHandle : compiled.order) {
            compiled.passPlans.push_back(std::move(passPlans[passHandle.index]));
        }
    }

    for (const auto& texture : _textures) {
        if (texture.lifetime != ERGResourceLifetime::Imported || !texture.imported.has_value()) {
            continue;
        }

        const auto finalLayout = texture.imported->importDesc.finalLayout;
        if (finalLayout == EImageLayout::Undefined) {
            continue;
        }

        compiled.importedTextureFinalizes.push_back({
            .texture          = texture.handle,
            .finalLayout      = finalLayout,
            .subresourceRange = texture.imported->subresourceRange,
        });
    }

    for (const auto& buffer : _buffers) {
        if (buffer.lifetime != ERGResourceLifetime::Imported || !buffer.imported.has_value() ||
            !buffer.imported->finalState.has_value()) {
            continue;
        }

        compiled.importedBufferFinalizes.push_back({
            .buffer       = buffer.handle,
            .initialState = buffer.imported->initialState,
            .finalState   = *buffer.imported->finalState,
        });
    }

    if (compiled.isValid()) {
        std::unordered_map<RGBufferHandle, RGTransientBufferLifetimePlan*> transientLifetimeByHandle;
        for (const auto& buffer : _buffers) {
            if (buffer.lifetime != ERGResourceLifetime::Transient) {
                continue;
            }
            compiled.transientBufferLifetimes.push_back({
                .buffer = buffer.handle,
                .desc   = buffer.desc,
            });
            transientLifetimeByHandle.emplace(buffer.handle, &compiled.transientBufferLifetimes.back());
            ++compiled.transientBufferDiagnostics.logicalCount;
            compiled.transientBufferDiagnostics.logicalBytes += buffer.desc.size;
        }

        for (uint32_t passIndex = 0; passIndex < compiled.passPlans.size(); ++passIndex) {
            const auto& passPlan = compiled.passPlans[passIndex];
            for (const auto& bufferState : passPlan.bufferStates) {
                const auto lifetimeIt = transientLifetimeByHandle.find(bufferState.buffer);
                if (lifetimeIt == transientLifetimeByHandle.end()) {
                    continue;
                }
                auto& lifetime = *lifetimeIt->second;
                if (!lifetime.isUsed()) {
                    lifetime.firstPassIndex = passIndex;
                    lifetime.firstPass      = passPlan.pass;
                }
                lifetime.lastPassIndex = passIndex;
                lifetime.lastPass      = passPlan.pass;
            }
        }

        for (const auto& lifetime : compiled.transientBufferLifetimes) {
            if (lifetime.isUsed()) {
                ++compiled.transientBufferDiagnostics.usedCount;
                compiled.transientBufferDiagnostics.usedBytes += lifetime.desc.size;
            }
            else {
                ++compiled.transientBufferDiagnostics.unusedCount;
                compiled.transientBufferDiagnostics.unusedBytes += lifetime.desc.size;
            }
        }
    }

    return compiled;
}

std::optional<RGPassContext> RenderGraph::createPassContext(RGPassHandle handle) const
{
    const auto* pass = getPass(handle);
    if (!pass) {
        return std::nullopt;
    }
    return RGPassContext(*this, *pass);
}

std::string RenderGraph::debugDump(const RGCompiledGraph& compiled) const
{
    std::ostringstream oss;
    oss << "passes(" << _passes.size() << ")\n";
    for (const auto& pass : _passes) {
        oss << "  [" << pass.handle.index << ":" << pass.handle.generation << "] " << pass.name << "\n";
    }

    oss << "order(" << compiled.order.size() << ")\n";
    for (const auto& handle : compiled.order) {
        const auto* pass = getPass(handle);
        oss << "  [" << handle.index << ":" << handle.generation << "] "
            << (pass ? pass->name : "<invalid-pass>") << "\n";
    }

    oss << "dependencies(" << compiled.dependencies.size() << ")\n";
    for (const auto& edge : compiled.dependencies) {
        const auto* from = getPass(edge.from);
        const auto* to   = getPass(edge.to);
        oss << "  " << (from ? from->name : "<invalid-pass>")
            << " -> " << (to ? to->name : "<invalid-pass>") << "\n";
    }

    oss << "passPlans(" << compiled.passPlans.size() << ")\n";
    for (const auto& passPlan : compiled.passPlans) {
        const auto* pass = getPass(passPlan.pass);
        oss << "  [" << passPlan.pass.index << ":" << passPlan.pass.generation << "] "
            << (pass ? pass->name : "<invalid-pass>")
            << " kind=" << toString(passPlan.kind) << "\n";

        if (passPlan.rasterPlan.has_value()) {
            oss << "    raster colors=" << passPlan.rasterPlan->colors.size()
                << " depth=" << (passPlan.rasterPlan->depth.has_value() ? 1 : 0)
                << " layers=" << passPlan.rasterPlan->layerCount << "\n";
        }

        oss << "    textureStates(" << passPlan.textureStates.size() << ")\n";
        for (const auto& state : passPlan.textureStates) {
            const auto* texture = getTexture(state.texture);
            oss << "      " << (texture ? texture->desc.label : "<invalid-texture>")
                << " layout=" << static_cast<uint32_t>(state.requiredState.layout) << "\n";
        }

        oss << "    bufferStates(" << passPlan.bufferStates.size() << ")\n";
        for (const auto& state : passPlan.bufferStates) {
            const auto* buffer = getBuffer(state.buffer);
            oss << "      " << (buffer ? buffer->desc.label : "<invalid-buffer>")
                << " access=" << static_cast<uint32_t>(state.requiredState.access) << "\n";
        }
    }

    oss << "importedTextureFinalizes(" << compiled.importedTextureFinalizes.size() << ")\n";
    for (const auto& finalize : compiled.importedTextureFinalizes) {
        const auto* texture = getTexture(finalize.texture);
        oss << "  " << (texture ? texture->desc.label : "<invalid-texture>")
            << " layout=" << static_cast<uint32_t>(finalize.finalLayout) << "\n";
    }

    oss << "importedBufferFinalizes(" << compiled.importedBufferFinalizes.size() << ")\n";
    for (const auto& finalize : compiled.importedBufferFinalizes) {
        const auto* buffer = getBuffer(finalize.buffer);
        oss << "  " << (buffer ? buffer->desc.label : "<invalid-buffer>")
            << " access=" << static_cast<uint32_t>(finalize.finalState.access) << "\n";
    }

    oss << "transientBufferLifetimes(" << compiled.transientBufferLifetimes.size() << ")\n";
    for (const auto& lifetime : compiled.transientBufferLifetimes) {
        const auto* buffer = getBuffer(lifetime.buffer);
        oss << "  " << (buffer ? buffer->desc.label : "<invalid-buffer>");
        if (lifetime.isUsed()) {
            const auto* firstPass = getPass(lifetime.firstPass);
            const auto* lastPass  = getPass(lifetime.lastPass);
            oss << " first=" << lifetime.firstPassIndex
                << ":" << (firstPass ? firstPass->name : "<invalid-pass>")
                << " last=" << lifetime.lastPassIndex
                << ":" << (lastPass ? lastPass->name : "<invalid-pass>");
        }
        else {
            oss << " unused";
        }
        oss << "\n";
    }

    const auto& transientDiagnostics = compiled.transientBufferDiagnostics;
    oss << "transientBufferDiagnostics"
        << " logicalCount=" << transientDiagnostics.logicalCount
        << " logicalBytes=" << transientDiagnostics.logicalBytes
        << " usedCount=" << transientDiagnostics.usedCount
        << " usedBytes=" << transientDiagnostics.usedBytes
        << " unusedCount=" << transientDiagnostics.unusedCount
        << " unusedBytes=" << transientDiagnostics.unusedBytes
        << " physicalReuse=not-materialized\n";

    oss << "issues(" << compiled.issues.size() << ")\n";
    for (const auto& issue : compiled.issues) {
        const auto* pass = getPass(issue.pass);
        oss << "  ";
        switch (issue.kind) {
            case RGCompileIssue::EKind::ReadBeforeWrite:
                oss << "ReadBeforeWrite";
                break;
            case RGCompileIssue::EKind::InvalidResource:
                oss << "InvalidResource";
                break;
            case RGCompileIssue::EKind::InvalidUsage:
                oss << "InvalidUsage";
                break;
            case RGCompileIssue::EKind::InvalidPassKind:
                oss << "InvalidPassKind";
                break;
            case RGCompileIssue::EKind::InvalidPersistentIdentity:
                oss << "InvalidPersistentIdentity";
                break;
            case RGCompileIssue::EKind::Cycle:
                oss << "Cycle";
                break;
        }
        if (pass) {
            oss << "@" << pass->name;
        }
        oss << ": " << issue.message << "\n";
    }

    return oss.str();
}

} // namespace ya

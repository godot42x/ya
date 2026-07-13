#include "RenderGraph.h"
#include "RenderGraphResourceRegistry.h"

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

BufferResourceState makeBufferState(const RGBufferResource& resource, ERGPassResourceAccess access)
{
    BufferResourceState state{
        .offset = 0,
        .size   = resource.desc.size,
    };

    switch (access) {
        case ERGPassResourceAccess::Read:
            state.stages = EPipelineStage::AllCommands;
            state.access = EResourceAccess::ShaderRead;
            break;
        case ERGPassResourceAccess::Write:
            state.stages = EPipelineStage::AllCommands;
            state.access = EResourceAccess::ShaderWrite;
            break;
        case ERGPassResourceAccess::ColorAttachment:
        case ERGPassResourceAccess::DepthAttachment:
            break;
    }

    return state;
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
    return _registry.resolveTexture(handle);
}

IBuffer* RGRenderContext::resolveBuffer(RGBufferHandle handle) const
{
    return _registry.resolveBuffer(handle);
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

void RGRenderContext::beginRasterRendering(const RasterRenderingDesc& desc) const
{
    YA_CORE_ASSERT(!desc.colors.empty(), "RGRenderContext pass {} requires at least one color attachment", _pass.name);

    std::vector<RenderingInfo::ImageSpec> colorAttachments;
    std::vector<ClearValue>               colorClearValues;
    colorAttachments.reserve(desc.colors.size());
    colorClearValues.reserve(desc.colors.size());

    for (const auto& colorDesc : desc.colors) {
        const auto* color = resolveTexture(colorDesc.color);
        const auto* colorResource = _graph.getTexture(colorDesc.color);
        YA_CORE_ASSERT(color != nullptr, "RGRenderContext pass {} failed to resolve color target {}", _pass.name, colorDesc.color.index);
        YA_CORE_ASSERT(colorResource != nullptr, "RGRenderContext pass {} failed to resolve color resource {}", _pass.name, colorDesc.color.index);
        YA_CORE_ASSERT(color->getImage() != nullptr && color->getImageView() != nullptr,
                       "RGRenderContext pass {} color target {} is missing image/view", _pass.name, colorDesc.color.index);

        colorAttachments.push_back(RenderingInfo::ImageSpec{
            .image         = color->getImage(),
            .imageView     = color->getImageView(),
            .loadOp        = colorDesc.loadOp,
            .storeOp       = colorDesc.storeOp,
            .initialLayout = EImageLayout::ColorAttachmentOptimal,
            .finalLayout   = colorDesc.finalLayout,
            .subresourceAspectMask = makeImportedViewRange(*colorResource).aspectMask,
            .subresourceBaseMipLevel = makeImportedViewRange(*colorResource).baseMipLevel,
            .subresourceLevelCount = makeImportedViewRange(*colorResource).levelCount,
            .subresourceBaseArrayLayer = makeImportedViewRange(*colorResource).baseArrayLayer,
            .subresourceLayerCount = makeImportedViewRange(*colorResource).layerCount,
            .bHasSubresourceRange = true,
        });
        colorClearValues.push_back(colorDesc.clearValue);
    }

    _activeDepthAttachment.reset();
    if (desc.depth.has_value()) {
        const auto* depth = resolveTexture(desc.depth->depth);
        const auto* depthResource = _graph.getTexture(desc.depth->depth);
        YA_CORE_ASSERT(depth != nullptr, "RGRenderContext pass {} failed to resolve depth target {}", _pass.name, desc.depth->depth.index);
        YA_CORE_ASSERT(depthResource != nullptr, "RGRenderContext pass {} failed to resolve depth resource {}", _pass.name, desc.depth->depth.index);
        YA_CORE_ASSERT(depth->getImage() != nullptr && depth->getImageView() != nullptr,
                       "RGRenderContext pass {} depth target {} is missing image/view", _pass.name, desc.depth->depth.index);

        _activeDepthAttachment = RenderingInfo::ImageSpec{
            .image         = depth->getImage(),
            .imageView     = depth->getImageView(),
            .loadOp        = desc.depth->loadOp,
            .storeOp       = desc.depth->storeOp,
            .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
            .finalLayout   = desc.depth->finalLayout,
            .subresourceAspectMask = makeImportedViewRange(*depthResource).aspectMask,
            .subresourceBaseMipLevel = makeImportedViewRange(*depthResource).baseMipLevel,
            .subresourceLevelCount = makeImportedViewRange(*depthResource).levelCount,
            .subresourceBaseArrayLayer = makeImportedViewRange(*depthResource).baseArrayLayer,
            .subresourceLayerCount = makeImportedViewRange(*depthResource).layerCount,
            .bHasSubresourceRange = true,
        };
    }

    _activeRenderingInfo = RenderingInfo{
        .label = _pass.name,
        .renderArea = desc.renderArea,
        .layerCount = desc.layerCount,
        .colorClearValues = std::move(colorClearValues),
        .depthClearValue = desc.depth.has_value() ? desc.depth->clearValue : ClearValue{},
        .colorAttachments = std::move(colorAttachments),
        .depthAttachment = _activeDepthAttachment ? &*_activeDepthAttachment : nullptr,
    };
    _cmdBuf.beginRendering(*_activeRenderingInfo);
}

void RGRenderContext::endRendering() const
{
    _cmdBuf.endRendering(_activeRenderingInfo.value_or(RenderingInfo{}));
    _activeRenderingInfo.reset();
    _activeDepthAttachment.reset();
}

void RGRenderContext::copyBuffer(RGBufferHandle src, RGBufferHandle dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset) const
{
    auto* srcBuffer = resolveBuffer(src);
    auto* dstBuffer = resolveBuffer(dst);
    YA_CORE_ASSERT(srcBuffer != nullptr, "RGRenderContext pass {} failed to resolve src buffer {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstBuffer != nullptr, "RGRenderContext pass {} failed to resolve dst buffer {}", _pass.name, dst.index);
    _cmdBuf.copyBuffer(srcBuffer, dstBuffer, size, srcOffset, dstOffset);
}

void RGRenderContext::copyTexture(RGTextureHandle src, RGTextureHandle dst, const ImageCopy& region) const
{
    const auto* srcTexture = resolveTexture(src);
    const auto* dstTexture = resolveTexture(dst);
    YA_CORE_ASSERT(srcTexture != nullptr, "RGRenderContext pass {} failed to resolve src texture {}", _pass.name, src.index);
    YA_CORE_ASSERT(dstTexture != nullptr, "RGRenderContext pass {} failed to resolve dst texture {}", _pass.name, dst.index);
    YA_CORE_ASSERT(srcTexture->getImage() != nullptr, "RGRenderContext pass {} src texture {} is missing image", _pass.name, src.index);
    YA_CORE_ASSERT(dstTexture->getImage() != nullptr, "RGRenderContext pass {} dst texture {} is missing image", _pass.name, dst.index);

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
    pass().buffers.push_back({.handle = handle, .access = ERGPassResourceAccess::Read});
}

void RGPassBuilder::write(RGBufferHandle handle)
{
    pass().buffers.push_back({.handle = handle, .access = ERGPassResourceAccess::Write});
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

RGTextureHandle RenderGraph::importTexture(const RGImportedTextureDesc& importedDesc)
{
    YA_CORE_ASSERT(importedDesc.image || importedDesc.importDesc.nativeHandle != nullptr,
                   "Imported texture requires either shared image or native handle");

    auto desc        = importedDesc.desc;
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

    RGTextureHandle handle{
        .index      = static_cast<uint32_t>(_textures.size()),
        .generation = _nextTextureGeneration++,
    };
    _textures.push_back(RGTextureResource{
        .handle   = handle,
        .lifetime = ERGResourceLifetime::Imported,
        .desc     = desc,
        .imported = importedDesc,
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

    std::unordered_map<RGTextureHandle, RGPassHandle> textureWriters;
    std::unordered_map<RGBufferHandle, RGPassHandle>  bufferWriters;
    std::vector<std::vector<uint32_t>> adjacency(_passes.size());
    std::vector<uint32_t> indegree(_passes.size(), 0);

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

    for (const auto& pass : _passes) {
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

            compiled.textureStates.push_back({
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

            const auto requiredUsage = usage.access == ERGPassResourceAccess::Read ? EBufferUsage::StorageBuffer : EBufferUsage::StorageBuffer;
            if (!hasBufferUsage(resource->desc.usage, requiredUsage) &&
                !hasBufferUsage(resource->desc.usage, EBufferUsage::UniformBuffer) &&
                !hasBufferUsage(resource->desc.usage, EBufferUsage::IndirectBuffer) &&
                !hasBufferUsage(resource->desc.usage, EBufferUsage::TransferDst) &&
                !hasBufferUsage(resource->desc.usage, EBufferUsage::TransferSrc)) {
                addIssue(RGCompileIssue::EKind::InvalidUsage, pass.handle,
                         std::format("pass {} uses buffer {} with unsupported usage flags", pass.name, resource->desc.label));
                continue;
            }

            compiled.bufferStates.push_back({
                .pass          = pass.handle,
                .buffer        = usage.handle,
                .requiredState = makeBufferState(*resource, usage.access),
            });

            const bool bWrite = usage.access == ERGPassResourceAccess::Write;
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

    oss << "textureStates(" << compiled.textureStates.size() << ")\n";
    for (const auto& state : compiled.textureStates) {
        const auto* pass    = getPass(state.pass);
        const auto* texture = getTexture(state.texture);
        oss << "  " << (pass ? pass->name : "<invalid-pass>")
            << " -> " << (texture ? texture->desc.label : "<invalid-texture>")
            << " layout=" << static_cast<uint32_t>(state.requiredState.layout) << "\n";
    }

    oss << "bufferStates(" << compiled.bufferStates.size() << ")\n";
    for (const auto& state : compiled.bufferStates) {
        const auto* pass   = getPass(state.pass);
        const auto* buffer = getBuffer(state.buffer);
        oss << "  " << (pass ? pass->name : "<invalid-pass>")
            << " -> " << (buffer ? buffer->desc.label : "<invalid-buffer>")
            << " access=" << static_cast<uint32_t>(state.requiredState.access) << "\n";
    }

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

#include "RenderGraphImportUtils.h"

namespace ya
{

RGPassHandle addBufferCopyPass(RenderGraph& graph, const RGBufferCopyParams& params)
{
    YA_CORE_ASSERT(!params.copies.empty(), "Render graph buffer copy pass requires at least one copy");
    for (const auto& copy : params.copies) {
        YA_CORE_ASSERT(copy.source.isValid(), "Render graph buffer copy source handle must be valid");
        YA_CORE_ASSERT(copy.destination.isValid(), "Render graph buffer copy destination handle must be valid");
        YA_CORE_ASSERT(graph.getBuffer(copy.source) != nullptr,
                       "Render graph buffer copy source handle does not belong to this graph");
        YA_CORE_ASSERT(graph.getBuffer(copy.destination) != nullptr,
                       "Render graph buffer copy destination handle does not belong to this graph");
        YA_CORE_ASSERT(copy.size > 0, "Render graph buffer copy size must be non-zero");
    }

    return graph.addPass(
        std::string(params.label),
        [copies = params.copies,
         dependency = params.dependency](RGPassBuilder& pass) {
            if (dependency.has_value()) {
                pass.dependsOn(*dependency);
            }
            pass.declareCopy();
            for (const auto& copy : copies) {
                pass.transferSrc(copy.source, RGBufferRange{
                    .offset = copy.sourceOffset,
                    .size   = copy.size,
                });
                pass.transferDst(copy.destination, RGBufferRange{
                    .offset = copy.destinationOffset,
                    .size   = copy.size,
                });
            }
        },
        [copies = params.copies](RGRenderContext& ctx) {
            for (const auto& copy : copies) {
                ctx.copyBuffer(
                    copy.source,
                    copy.destination,
                    copy.size,
                    copy.sourceOffset,
                    copy.destinationOffset);
            }
        });
}

namespace
{

std::shared_ptr<ImageResource> cloneImageResourceWithView(
    const std::shared_ptr<ImageResource>& resource,
    const std::shared_ptr<IImageView>& view)
{
    YA_CORE_ASSERT(resource != nullptr, "Render graph import requires a backing image resource");
    YA_CORE_ASSERT(view != nullptr, "Render graph import requires a backing image view");

    auto clone         = std::make_shared<ImageResource>();
    clone->label       = resource->label;
    clone->desc        = resource->desc;
    clone->image       = resource->image;
    clone->defaultView = view;
    clone->retainedResources.reserve(resource->retainedResources.size() + 2);
    clone->retainedResources.push_back(resource);
    clone->retainedResources.insert(
        clone->retainedResources.end(),
        resource->retainedResources.begin(),
        resource->retainedResources.end());
    clone->retainedResources.push_back(view);
    return clone;
}

std::shared_ptr<ImageResource> cloneImageResourceOwner(const ImageResource& resource)
{
    auto clone               = std::make_shared<ImageResource>();
    clone->label             = resource.label;
    clone->desc              = resource.desc;
    clone->image             = resource.image;
    clone->defaultView       = resource.defaultView;
    clone->retainedResources = resource.retainedResources;
    return clone;
}

EImageUsage::T mergeImportedUsage(EImageUsage::T usage, EImageUsage::T requiredUsage)
{
    return static_cast<EImageUsage::T>(usage | requiredUsage);
}

EBufferUsage mergeImportedUsage(EBufferUsage usage, EBufferUsage requiredUsage)
{
    return usage | requiredUsage;
}

EImageLayout::T getImportedInitialLayout(const IImage& image, const ImageSubresourceRange* range)
{
    if (!range) {
        return image.getCompatibilityLayout();
    }

    return image.getCompatibilityLayout(
        range->aspectMask,
        range->baseMipLevel,
        range->baseArrayLayer);
}

RGImportedTextureDesc makeImportedTextureDesc(
    const std::shared_ptr<ImageResource>& resource,
    EFormat::T format,
    Extent3D extent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage)
{
    YA_CORE_ASSERT(resource != nullptr, "Render graph import requires a backing image resource");

    const auto image = resource->getImageShared();
    const auto imageView = resource->getImageViewShared();
    YA_CORE_ASSERT(image != nullptr, "Render graph import requires a backing image");
    YA_CORE_ASSERT(imageView != nullptr, "Render graph import requires a backing image view");

    const EImageUsage::T usage = mergeImportedUsage(image->getUsage(), requiredUsage);
    const auto&          viewRange = imageView->getSubresourceRange();

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = format,
            .extent      = extent,
            .mipLevels   = viewRange.levelCount,
            .arrayLayers = viewRange.layerCount,
            .usage       = usage,
        },
        .importDesc = ImportedImageDesc{
            .label         = std::string(label),
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = format,
            .usage         = usage,
            .extent        = extent,
            .mipLevels     = image->getMipLevels(),
            .arrayLayers   = image->getArrayLayers(),
            .initialLayout = getImportedInitialLayout(*image, imageView ? &imageView->getSubresourceRange() : nullptr),
            .finalLayout   = finalLayout,
        },
        .resource = resource,
        .subresourceRange = imageView->getSubresourceRange(),
    };
}

RGImportedTextureDesc makeImportedSubresourceTextureDesc(
    const std::shared_ptr<ImageResource>& resource,
    EFormat::T format,
    const ImageViewCreateInfo& viewDesc,
    Extent3D logicalExtent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage)
{
    YA_CORE_ASSERT(resource != nullptr, "Render graph subresource import requires a backing image resource");

    const auto image = resource->getImageShared();
    YA_CORE_ASSERT(image != nullptr, "Render graph subresource import requires a backing image");

    const EImageUsage::T usage = mergeImportedUsage(image->getUsage(), requiredUsage);
    const ImageSubresourceRange subresourceRange{
        .aspectMask     = viewDesc.aspectFlags,
        .baseMipLevel   = viewDesc.baseMipLevel,
        .levelCount     = viewDesc.levelCount,
        .baseArrayLayer = viewDesc.baseArrayLayer,
        .layerCount     = viewDesc.layerCount,
    };

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = format,
            .extent      = logicalExtent,
            .mipLevels   = viewDesc.levelCount,
            .arrayLayers = viewDesc.layerCount,
            .usage       = usage,
        },
        .importDesc = ImportedImageDesc{
            .label         = std::string(label),
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = format,
            .usage         = usage,
            .extent        = Extent3D{image->getWidth(), image->getHeight(), 1},
            .mipLevels     = image->getMipLevels(),
            .arrayLayers   = image->getArrayLayers(),
            .initialLayout = getImportedInitialLayout(*image, &subresourceRange),
            .finalLayout   = finalLayout,
        },
        .resource = resource,
        .subresourceRange = subresourceRange,
        .viewDesc = viewDesc,
    };
}

} // namespace

RGImportedBufferDesc makeImportedBufferDesc(const std::shared_ptr<IBuffer>& buffer,
                                            std::string_view label,
                                            BufferResourceState initialState,
                                            EBufferUsage requiredUsage,
                                            std::optional<BufferResourceState> finalState)
{
    YA_CORE_ASSERT(buffer != nullptr, "Render graph import requires a backing buffer");

    return RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = std::string(label),
            .usage = mergeImportedUsage(buffer->getUsage(), requiredUsage),
            .size  = buffer->getSize(),
        },
        .buffer            = buffer.get(),
        .initialState      = initialState,
        .finalState        = finalState,
        .retainedResources = {buffer},
    };
}

RGImportedBufferDesc makeHostWrittenImportedBufferDesc(const std::shared_ptr<IBuffer>& buffer,
                                                       std::string_view label,
                                                       EBufferUsage requiredUsage,
                                                       uint64_t rangeOffset,
                                                       uint64_t rangeSize,
                                                       std::optional<BufferResourceState> finalState)
{
    return makeImportedBufferDesc(
        buffer,
        label,
        BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostWrite,
            .offset = rangeOffset,
            .size   = rangeSize == 0 ? (buffer ? buffer->getSize() : 0) : rangeSize,
        },
        requiredUsage,
        finalState);
}

RGImportedBufferDesc makeReadbackImportedBufferDesc(const std::shared_ptr<IBuffer>& buffer,
                                                    std::string_view label,
                                                    EBufferUsage requiredUsage,
                                                    uint64_t rangeOffset,
                                                    uint64_t rangeSize)
{
    return makeImportedBufferDesc(
        buffer,
        label,
        BufferResourceState{},
        requiredUsage,
        BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostRead,
            .offset = rangeOffset,
            .size   = rangeSize == 0 ? (buffer ? buffer->getSize() : 0) : rangeSize,
        });
}

RGImportedTextureDesc makeImportedTextureDesc(const ImageResource& image,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage)
{
    auto resource = cloneImageResourceOwner(image);
    auto desc = makeImportedTextureDesc(
        resource,
        image.getFormat(),
        Extent3D{image.getWidth(), image.getHeight(), 1},
        label,
        finalLayout,
        requiredUsage);
    desc.retainedResources = resource->getRetainedResources();
    return desc;
}

RGImportedTextureDesc makeImportedTextureDesc(const Texture& texture,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage)
{
    auto desc = makeImportedTextureDesc(
        texture.getResourceShared(),
        texture.getFormat(),
        Extent3D{texture.getWidth(), texture.getHeight(), 1},
        label,
        finalLayout,
        requiredUsage);
    desc.retainedResources = texture.getRetainedResources();
    return desc;
}

RGImportedTextureDesc makeImportedTextureDesc(const RenderTexture& texture,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage)
{
    auto desc = makeImportedTextureDesc(
        texture.getResourceShared(),
        texture.getFormat(),
        Extent3D{texture.getWidth(), texture.getHeight(), 1},
        label,
        finalLayout,
        requiredUsage);
    desc.retainedResources = texture.getRetainedResources();
    return desc;
}

RGImportedTextureDesc makeImportedTextureDesc(const std::shared_ptr<ImageResource>& resource,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage,
                                              std::optional<Extent3D> logicalExtent)
{
    YA_CORE_ASSERT(resource != nullptr, "Render graph import requires a backing image resource");

    const auto image = resource->getImageShared();
    YA_CORE_ASSERT(image != nullptr, "Render graph import requires a backing image");

    const Extent3D resolvedExtent = logicalExtent.value_or(Extent3D{image->getWidth(), image->getHeight(), 1});
    return makeImportedTextureDesc(
        resource,
        image->getFormat(),
        resolvedExtent,
        label,
        finalLayout,
        requiredUsage);
}

RGImportedTextureDesc makeImportedTextureDesc(const std::shared_ptr<ImageResource>& resource,
                                              const std::shared_ptr<IImageView>& view,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage,
                                              std::optional<Extent3D> logicalExtent)
{
    auto importedResource = cloneImageResourceWithView(resource, view);
    auto desc = makeImportedTextureDesc(
        importedResource,
        label,
        finalLayout,
        requiredUsage,
        logicalExtent);
    desc.retainedResources = importedResource->retainedResources;
    return desc;
}

RGImportedTextureDesc makeImportedSubresourceTextureDesc(const std::shared_ptr<ImageResource>& resource,
                                                         const ImageViewCreateInfo& viewDesc,
                                                         Extent3D logicalExtent,
                                                         std::string_view label,
                                                         EImageLayout::T finalLayout,
                                                         EImageUsage::T requiredUsage)
{
    const auto image = resource ? resource->getImageShared() : nullptr;
    return makeImportedSubresourceTextureDesc(
        resource,
        image ? image->getFormat() : EFormat::Undefined,
        viewDesc,
        logicalExtent,
        label,
        finalLayout,
        requiredUsage);
}

} // namespace ya

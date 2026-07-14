#pragma once

#include "Render/Core/Image.h"
#include "Render/Core/RenderImage.h"
#include "Render/RenderDefines.h"

namespace ya
{

inline bool tryResolveImageSpecSubresourceRange(const RenderingInfo::ImageSpec& spec, ImageSubresourceRange& outRange)
{
    if (spec.bHasSubresourceRange) {
        outRange = ImageSubresourceRange{
            .aspectMask     = static_cast<EImageAspect::T>(spec.subresourceAspectMask),
            .baseMipLevel   = spec.subresourceBaseMipLevel,
            .levelCount     = spec.subresourceLevelCount,
            .baseArrayLayer = spec.subresourceBaseArrayLayer,
            .layerCount     = spec.subresourceLayerCount,
        };
        return true;
    }

    if (!spec.imageView) {
        return false;
    }

    outRange = spec.imageView->getSubresourceRange();
    return true;
}

inline RenderingInfo::ImageSpec makeAttachmentImageSpec(
    IImageView*           imageView,
    EAttachmentLoadOp::T  loadOp,
    EAttachmentStoreOp::T storeOp,
    EImageLayout::T       initialLayout,
    EImageLayout::T       finalLayout)
{
    RenderingInfo::ImageSpec spec{
        .image         = imageView ? const_cast<IImage*>(imageView->getImage()) : nullptr,
        .imageView     = imageView,
        .loadOp        = loadOp,
        .storeOp       = storeOp,
        .initialLayout = initialLayout,
        .finalLayout   = finalLayout,
    };

    ImageSubresourceRange range{};
    if (tryResolveImageSpecSubresourceRange(spec, range)) {
        spec.subresourceAspectMask     = range.aspectMask;
        spec.subresourceBaseMipLevel   = range.baseMipLevel;
        spec.subresourceLevelCount     = range.levelCount;
        spec.subresourceBaseArrayLayer = range.baseArrayLayer;
        spec.subresourceLayerCount     = range.layerCount;
        spec.bHasSubresourceRange      = true;
    }

    return spec;
}

inline RenderingInfo::ImageSpec makeAttachmentImageSpec(
    const std::shared_ptr<IImage>&     image,
    const std::shared_ptr<IImageView>& imageView,
    EAttachmentLoadOp::T               loadOp,
    EAttachmentStoreOp::T              storeOp,
    EImageLayout::T                    initialLayout,
    EImageLayout::T                    finalLayout)
{
    RenderingInfo::ImageSpec spec = makeAttachmentImageSpec(imageView.get(), loadOp, storeOp, initialLayout, finalLayout);
    spec.retainedImage            = image;
    spec.retainedImageView        = imageView;
    return spec;
}

inline RenderingInfo::ImageSpec makeAttachmentImageSpec(
    const RenderImage&       image,
    EAttachmentLoadOp::T     loadOp,
    EAttachmentStoreOp::T    storeOp,
    EImageLayout::T          initialLayout,
    EImageLayout::T          finalLayout)
{
    RenderingInfo::ImageSpec spec = makeAttachmentImageSpec(
        image.getImageShared(),
        image.getImageViewShared(),
        loadOp,
        storeOp,
        initialLayout,
        finalLayout);
    spec.retainedResources        = image.getRetainedResources();
    return spec;
}

inline bool imageSpecMatchesImageViewImage(const RenderingInfo::ImageSpec& spec)
{
    return !spec.image || !spec.imageView || spec.imageView->getImage() == spec.image;
}

} // namespace ya

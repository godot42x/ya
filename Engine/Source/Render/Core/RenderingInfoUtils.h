#pragma once

#include "Render/Core/Image.h"
#include "Render/Core/RenderImage.h"
#include "Render/RenderDefines.h"

namespace ya
{

inline bool tryResolveRenderAttachmentSubresourceRange(const RenderAttachment& attachment, ImageSubresourceRange& outRange)
{
    if (attachment.bHasSubresourceRange) {
        outRange = ImageSubresourceRange{
            .aspectMask     = static_cast<EImageAspect::T>(attachment.subresourceAspectMask),
            .baseMipLevel   = attachment.subresourceBaseMipLevel,
            .levelCount     = attachment.subresourceLevelCount,
            .baseArrayLayer = attachment.subresourceBaseArrayLayer,
            .layerCount     = attachment.subresourceLayerCount,
        };
        return true;
    }

    if (!attachment.imageView) {
        return false;
    }

    outRange = attachment.imageView->getSubresourceRange();
    return true;
}

inline RenderAttachment makeRenderAttachment(
    IImageView*           imageView,
    EAttachmentLoadOp::T  loadOp,
    EAttachmentStoreOp::T storeOp,
    EImageLayout::T       initialLayout,
    EImageLayout::T       finalLayout,
    ClearValue            clearValue = {})
{
    RenderAttachment attachment{
        .image         = imageView ? const_cast<IImage*>(imageView->getImage()) : nullptr,
        .imageView     = imageView,
        .loadOp        = loadOp,
        .storeOp       = storeOp,
        .clearValue    = clearValue,
        .initialLayout = initialLayout,
        .finalLayout   = finalLayout,
    };

    ImageSubresourceRange range{};
    if (tryResolveRenderAttachmentSubresourceRange(attachment, range)) {
        attachment.subresourceAspectMask     = range.aspectMask;
        attachment.subresourceBaseMipLevel   = range.baseMipLevel;
        attachment.subresourceLevelCount     = range.levelCount;
        attachment.subresourceBaseArrayLayer = range.baseArrayLayer;
        attachment.subresourceLayerCount     = range.layerCount;
        attachment.bHasSubresourceRange      = true;
    }

    return attachment;
}

inline bool renderAttachmentMatchesImageViewImage(const RenderAttachment& attachment)
{
    return !attachment.image || !attachment.imageView || attachment.imageView->getImage() == attachment.image;
}

} // namespace ya

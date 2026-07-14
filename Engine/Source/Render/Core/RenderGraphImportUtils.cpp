#include "RenderGraphImportUtils.h"

namespace ya
{

namespace
{

EImageUsage::T mergeImportedUsage(EImageUsage::T usage, EImageUsage::T requiredUsage)
{
    return static_cast<EImageUsage::T>(usage | requiredUsage);
}

RGImportedTextureDesc makeImportedTextureDesc(
    const std::shared_ptr<IImage>& image,
    const std::shared_ptr<IImageView>& imageView,
    EFormat::T format,
    Extent3D extent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage)
{
    YA_CORE_ASSERT(image != nullptr, "Render graph import requires a backing image");
    YA_CORE_ASSERT(imageView != nullptr, "Render graph import requires a backing image view");

    const EImageUsage::T usage = mergeImportedUsage(image->getUsage(), requiredUsage);

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = format,
            .extent      = extent,
            .mipLevels   = image->getMipLevels(),
            .arrayLayers = image->getArrayLayers(),
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
            .initialLayout = image->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = image,
        .imageView = imageView,
        .subresourceRange = imageView->getSubresourceRange(),
    };
}

RGImportedTextureDesc makeImportedSubresourceTextureDesc(
    const std::shared_ptr<IImage>& image,
    EFormat::T format,
    const std::shared_ptr<IImageView>& imageView,
    const ImageViewCreateInfo& viewDesc,
    Extent3D logicalExtent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage)
{
    YA_CORE_ASSERT(image != nullptr, "Render graph subresource import requires a backing image");
    YA_CORE_ASSERT(imageView != nullptr, "Render graph subresource import requires a backing image view");

    const EImageUsage::T usage = mergeImportedUsage(image->getUsage(), requiredUsage);

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
            .initialLayout = image->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = image,
        .imageView = imageView,
        .subresourceRange = imageView->getSubresourceRange(),
        .viewDesc = viewDesc,
    };
}

RGImportedTextureDesc makeImportedSubresourceTextureDesc(
    const std::shared_ptr<IImage>& image,
    EFormat::T format,
    const ImageViewCreateInfo& viewDesc,
    Extent3D logicalExtent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage)
{
    YA_CORE_ASSERT(image != nullptr, "Render graph subresource import requires a backing image");

    const EImageUsage::T usage = mergeImportedUsage(image->getUsage(), requiredUsage);

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
            .initialLayout = image->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = image,
        .subresourceRange = ImageSubresourceRange{
            .aspectMask     = viewDesc.aspectFlags,
            .baseMipLevel   = viewDesc.baseMipLevel,
            .levelCount     = viewDesc.levelCount,
            .baseArrayLayer = viewDesc.baseArrayLayer,
            .layerCount     = viewDesc.layerCount,
        },
        .viewDesc = viewDesc,
    };
}

} // namespace

RGImportedTextureDesc makeImportedTextureDesc(const Texture& texture,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage)
{
    return makeImportedTextureDesc(
        texture.getImageShared(),
        texture.getImageViewShared(),
        texture.getFormat(),
        Extent3D{texture.getWidth(), texture.getHeight(), 1},
        label,
        finalLayout,
        requiredUsage);
}

RGImportedTextureDesc makeImportedTextureDesc(const RenderImage& image,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage)
{
    return makeImportedTextureDesc(
        image.getImageShared(),
        image.getImageViewShared(),
        image.getFormat(),
        Extent3D{image.getWidth(), image.getHeight(), 1},
        label,
        finalLayout,
        requiredUsage);
}

RGImportedTextureDesc makeImportedTextureDesc(const std::shared_ptr<IImage>& image,
                                              const std::shared_ptr<IImageView>& imageView,
                                              std::string_view label,
                                              EImageLayout::T finalLayout,
                                              EImageUsage::T requiredUsage,
                                              std::optional<Extent3D> logicalExtent)
{
    YA_CORE_ASSERT(image != nullptr, "Render graph import requires a backing image");

    const Extent3D resolvedExtent = logicalExtent.value_or(Extent3D{image->getWidth(), image->getHeight(), 1});
    return makeImportedTextureDesc(
        image,
        imageView,
        image->getFormat(),
        resolvedExtent,
        label,
        finalLayout,
        requiredUsage);
}

RGImportedTextureDesc makeImportedSubresourceTextureDesc(const std::shared_ptr<IImage>& image,
                                                         const std::shared_ptr<IImageView>& imageView,
                                                         const ImageViewCreateInfo& viewDesc,
                                                         Extent3D logicalExtent,
                                                         std::string_view label,
                                                         EImageLayout::T finalLayout,
                                                         EImageUsage::T requiredUsage)
{
    return makeImportedSubresourceTextureDesc(
        image,
        image ? image->getFormat() : EFormat::Undefined,
        imageView,
        viewDesc,
        logicalExtent,
        label,
        finalLayout,
        requiredUsage);
}

RGImportedTextureDesc makeImportedSubresourceTextureDesc(const std::shared_ptr<IImage>& image,
                                                         const ImageViewCreateInfo& viewDesc,
                                                         Extent3D logicalExtent,
                                                         std::string_view label,
                                                         EImageLayout::T finalLayout,
                                                         EImageUsage::T requiredUsage)
{
    return makeImportedSubresourceTextureDesc(
        image,
        image ? image->getFormat() : EFormat::Undefined,
        viewDesc,
        logicalExtent,
        label,
        finalLayout,
        requiredUsage);
}

} // namespace ya

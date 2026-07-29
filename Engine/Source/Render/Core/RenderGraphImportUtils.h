#pragma once

#include "RenderGraph.h"
#include "ImageResourceRef.h"
#include "RenderImage.h"
#include "Texture.h"
#include "Core/Api.h"

namespace ya
{

[[nodiscard]] ENGINE_API RGImportedTextureDesc makeImportedTextureDesc(
    const Texture& texture,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] ENGINE_API RGImportedTextureDesc makeImportedTextureDesc(
    const RenderImage& image,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] ENGINE_API RGImportedTextureDesc makeImportedTextureDesc(
    const ImageResourceRef& resource,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] ENGINE_API RGImportedTextureDesc makeImportedTextureDesc(
    const std::shared_ptr<IImage>& image,
    const std::shared_ptr<IImageView>& imageView,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None,
    std::optional<Extent3D> logicalExtent = std::nullopt);

[[nodiscard]] ENGINE_API RGImportedTextureDesc makeImportedSubresourceTextureDesc(
    const std::shared_ptr<IImage>& image,
    const std::shared_ptr<IImageView>& imageView,
    const ImageViewCreateInfo& viewDesc,
    Extent3D logicalExtent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] ENGINE_API RGImportedTextureDesc makeImportedSubresourceTextureDesc(
    const std::shared_ptr<IImage>& image,
    const ImageViewCreateInfo& viewDesc,
    Extent3D logicalExtent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

} // namespace ya

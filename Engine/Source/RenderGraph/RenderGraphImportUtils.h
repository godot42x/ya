#pragma once

#include "RenderGraph.h"
#include "RHI/Core/ImageResourceRef.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Core/Texture.h"
#include "Core/Api.h"

namespace ya
{

struct RGBufferCopyRegion
{
    RGBufferHandle source;
    RGBufferHandle destination;
    uint64_t       size = 0;
    uint64_t       sourceOffset = 0;
    uint64_t       destinationOffset = 0;
};

struct RGBufferCopyParams
{
    std::string_view            label;
    std::vector<RGBufferCopyRegion> copies;
    std::optional<RGPassHandle> dependency{};
};

[[nodiscard]] ENGINE_API RGPassHandle addBufferCopyPass(
    RenderGraph& graph,
    const RGBufferCopyParams& params);

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

[[nodiscard]] ENGINE_API RGImportedBufferDesc makeImportedBufferDesc(
    const std::shared_ptr<IBuffer>& buffer,
    std::string_view label,
    BufferResourceState initialState,
    EBufferUsage requiredUsage = EBufferUsage::None,
    std::optional<BufferResourceState> finalState = std::nullopt);

[[nodiscard]] ENGINE_API RGImportedBufferDesc makeHostWrittenImportedBufferDesc(
    const std::shared_ptr<IBuffer>& buffer,
    std::string_view label,
    EBufferUsage requiredUsage = EBufferUsage::None,
    uint64_t rangeOffset = 0,
    uint64_t rangeSize = 0,
    std::optional<BufferResourceState> finalState = std::nullopt);

[[nodiscard]] ENGINE_API RGImportedBufferDesc makeReadbackImportedBufferDesc(
    const std::shared_ptr<IBuffer>& buffer,
    std::string_view label,
    EBufferUsage requiredUsage = EBufferUsage::TransferDst,
    uint64_t rangeOffset = 0,
    uint64_t rangeSize = 0);

} // namespace ya

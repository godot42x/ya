#pragma once

#include "RenderGraph.h"
#include "Foundation/RHI/Core/ImageResourceRef.h"
#include "Foundation/RHI/Core/RenderImage.h"
#include "Foundation/RHI/Core/Texture.h"
#include "Foundation/Core/Api.h"

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

[[nodiscard]] YA_RENDER_GRAPH_API RGPassHandle addBufferCopyPass(
    RenderGraph& graph,
    const RGBufferCopyParams& params);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedTextureDesc makeImportedTextureDesc(
    const Texture& texture,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedTextureDesc makeImportedTextureDesc(
    const RenderImage& image,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedTextureDesc makeImportedTextureDesc(
    const ImageResourceRef& resource,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedTextureDesc makeImportedTextureDesc(
    const std::shared_ptr<IImage>& image,
    const std::shared_ptr<IImageView>& imageView,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None,
    std::optional<Extent3D> logicalExtent = std::nullopt);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedTextureDesc makeImportedSubresourceTextureDesc(
    const std::shared_ptr<IImage>& image,
    const std::shared_ptr<IImageView>& imageView,
    const ImageViewCreateInfo& viewDesc,
    Extent3D logicalExtent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedTextureDesc makeImportedSubresourceTextureDesc(
    const std::shared_ptr<IImage>& image,
    const ImageViewCreateInfo& viewDesc,
    Extent3D logicalExtent,
    std::string_view label,
    EImageLayout::T finalLayout,
    EImageUsage::T requiredUsage = EImageUsage::None);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedBufferDesc makeImportedBufferDesc(
    const std::shared_ptr<IBuffer>& buffer,
    std::string_view label,
    BufferResourceState initialState,
    EBufferUsage requiredUsage = EBufferUsage::None,
    std::optional<BufferResourceState> finalState = std::nullopt);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedBufferDesc makeHostWrittenImportedBufferDesc(
    const std::shared_ptr<IBuffer>& buffer,
    std::string_view label,
    EBufferUsage requiredUsage = EBufferUsage::None,
    uint64_t rangeOffset = 0,
    uint64_t rangeSize = 0,
    std::optional<BufferResourceState> finalState = std::nullopt);

[[nodiscard]] YA_RENDER_GRAPH_API RGImportedBufferDesc makeReadbackImportedBufferDesc(
    const std::shared_ptr<IBuffer>& buffer,
    std::string_view label,
    EBufferUsage requiredUsage = EBufferUsage::TransferDst,
    uint64_t rangeOffset = 0,
    uint64_t rangeSize = 0);

} // namespace ya

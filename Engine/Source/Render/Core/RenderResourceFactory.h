#pragma once

#include "Buffer.h"
#include "Image.h"
#include "Sampler.h"

#include <memory>
#include <string>

namespace ya
{

struct ImageViewCreateInfo
{
    std::string       label;
    EImageViewType::T viewType       = EImageViewType::View2D;
    EImageAspect::T   aspectFlags    = EImageAspect::None;
    uint32_t          baseMipLevel   = 0;
    uint32_t          levelCount     = 1;
    uint32_t          baseArrayLayer = 0;
    uint32_t          layerCount     = 1;
    ComponentMapping  components     = {};
};

struct ImportedImageDesc
{
    std::string    label;
    void*          nativeHandle = nullptr;
    EFormat::T     format       = EFormat::Undefined;
    EImageUsage::T usage        = EImageUsage::None;
    Extent3D       extent       = {};
    uint32_t       mipLevels    = 1;
    uint32_t       arrayLayers  = 1;
    bool           bOwnsNativeResource = false;
    EImageLayout::T initialLayout       = EImageLayout::Undefined;
    EImageLayout::T finalLayout         = EImageLayout::Undefined;
};

struct IRenderResourceFactory
{
    virtual ~IRenderResourceFactory() = default;

    virtual std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) = 0;
    virtual std::shared_ptr<Sampler> createSampler(const SamplerDesc& desc) = 0;
    virtual std::shared_ptr<IImage> createImage(const ImageCreateInfo& desc) = 0;
    virtual std::shared_ptr<IImage> importImage(const ImportedImageDesc& desc) = 0;
    virtual std::shared_ptr<IImageView> createImageView(
        std::shared_ptr<IImage> image,
        const ImageViewCreateInfo& desc) = 0;
};

} // namespace ya

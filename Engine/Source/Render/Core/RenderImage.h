#pragma once

#include "RenderResourceFactory.h"

namespace ya
{

struct RenderImageDesc
{
    ImageCreateInfo     image;
    ImageViewCreateInfo defaultView;
};

struct RenderImage
{
    std::shared_ptr<IImage>     image;
    std::shared_ptr<IImageView> defaultView;

    [[nodiscard]] IImage* getImage() const { return image.get(); }
    [[nodiscard]] IImageView* getImageView() const { return defaultView.get(); }
    [[nodiscard]] std::shared_ptr<IImage> getImageShared() const { return image; }
    [[nodiscard]] std::shared_ptr<IImageView> getImageViewShared() const { return defaultView; }
    [[nodiscard]] uint32_t getWidth() const { return image ? image->getWidth() : 0; }
    [[nodiscard]] uint32_t getHeight() const { return image ? image->getHeight() : 0; }
    [[nodiscard]] EFormat::T getFormat() const { return image ? image->getFormat() : EFormat::Undefined; }
    [[nodiscard]] Extent2D getExtent() const
    {
        return image ? Extent2D{.width = image->getWidth(), .height = image->getHeight()} : Extent2D{};
    }
    [[nodiscard]] bool isValid() const { return image && defaultView; }
};

[[nodiscard]] std::shared_ptr<RenderImage> createRenderImage(
    IRenderResourceFactory& factory,
    const RenderImageDesc& desc);

} // namespace ya

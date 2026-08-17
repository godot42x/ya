#pragma once

#include "RenderResourceFactory.h"

namespace ya
{

struct ImageResourceDesc
{
    ImageCreateInfo     image;
    ImageViewCreateInfo defaultView;
};

struct ImageResource
{
    std::string                     label;
    ImageResourceDesc               desc{};
    std::shared_ptr<IImage>         image;
    std::shared_ptr<IImageView>     defaultView;
    std::vector<std::shared_ptr<void>> retainedResources;

    [[nodiscard]] const std::string& getLabel() const { return label; }
    [[nodiscard]] const ImageResourceDesc& getDesc() const { return desc; }
    [[nodiscard]] IImage* getImage() const { return image.get(); }
    [[nodiscard]] IImageView* getImageView() const { return defaultView.get(); }
    [[nodiscard]] std::shared_ptr<IImage> getImageShared() const { return image; }
    [[nodiscard]] std::shared_ptr<IImageView> getImageViewShared() const { return defaultView; }
    [[nodiscard]] const std::vector<std::shared_ptr<void>>& getRetainedResources() const { return retainedResources; }
    [[nodiscard]] uint32_t getWidth() const { return image ? image->getWidth() : 0; }
    [[nodiscard]] uint32_t getHeight() const { return image ? image->getHeight() : 0; }
    [[nodiscard]] EFormat::T getFormat() const { return image ? image->getFormat() : EFormat::Undefined; }
    [[nodiscard]] Extent2D getExtent() const
    {
        return image ? Extent2D{.width = image->getWidth(), .height = image->getHeight()} : Extent2D{};
    }
    [[nodiscard]] bool isValid() const { return image && defaultView; }
};

[[nodiscard]] YA_RHI_API std::shared_ptr<ImageResource> createImageResource(
    IRenderResourceFactory& factory,
    const ImageResourceDesc& desc);

} // namespace ya

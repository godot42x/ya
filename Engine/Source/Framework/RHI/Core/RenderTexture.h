#pragma once

#include "ImageResource.h"
#include "TextureCreateInfo.h"

namespace ya
{

struct YA_RHI_API RenderTexture
{
    std::shared_ptr<ImageResource> resource;

  public:
    RenderTexture() = default;

    static std::shared_ptr<RenderTexture> create(
        IRenderResourceFactory& factory,
        const RenderTextureCreateInfo& ci);

    static std::shared_ptr<RenderTexture> wrap(
        std::shared_ptr<IImage> image,
        std::shared_ptr<IImageView> view,
        const std::string& label = "");

    static std::shared_ptr<RenderTexture> adopt(
        std::shared_ptr<ImageResource> resource,
        const std::string& label = "");

    [[nodiscard]] std::shared_ptr<ImageResource> getResourceShared() const { return resource; }
    [[nodiscard]] IImage* getImage() const { return resource ? resource->getImage() : nullptr; }
    [[nodiscard]] std::shared_ptr<IImage> getImageShared() const { return resource ? resource->getImageShared() : nullptr; }
    [[nodiscard]] IImageView* getImageView() const { return resource ? resource->getImageView() : nullptr; }
    [[nodiscard]] std::shared_ptr<IImageView> getImageViewShared() const { return resource ? resource->getImageViewShared() : nullptr; }
    [[nodiscard]] const std::vector<std::shared_ptr<void>>& getRetainedResources() const
    {
        static const std::vector<std::shared_ptr<void>> kEmpty;
        return resource ? resource->getRetainedResources() : kEmpty;
    }
    [[nodiscard]] uint32_t getWidth() const { return resource ? resource->getWidth() : 0; }
    [[nodiscard]] uint32_t getHeight() const { return resource ? resource->getHeight() : 0; }
    [[nodiscard]] EFormat::T getFormat() const { return resource ? resource->getFormat() : EFormat::Undefined; }
    [[nodiscard]] Extent2D getExtent() const { return resource ? resource->getExtent() : Extent2D{}; }
    [[nodiscard]] const std::string& getLabel() const
    {
        static const std::string kEmpty;
        return resource ? resource->getLabel() : kEmpty;
    }
    [[nodiscard]] bool isValid() const { return resource && resource->isValid(); }
};

} // namespace ya

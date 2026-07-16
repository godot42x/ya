#pragma once

#include "RenderImage.h"
#include "Texture.h"

namespace ya
{

struct ImageResourceRef
{
    std::shared_ptr<RenderImage> renderImage = nullptr;
    stdptr<Texture>              texture     = nullptr;

    [[nodiscard]] bool isValid() const
    {
        return (renderImage && renderImage->getImageView()) ||
               (texture && texture->getImageView());
    }

    [[nodiscard]] std::shared_ptr<IImage> getImageShared() const
    {
        if (renderImage && renderImage->getImageShared()) {
            return renderImage->getImageShared();
        }

        return texture ? texture->getImageShared() : nullptr;
    }

    [[nodiscard]] std::shared_ptr<IImageView> getImageViewShared() const
    {
        if (renderImage && renderImage->getImageViewShared()) {
            return renderImage->getImageViewShared();
        }

        return texture ? texture->getImageViewShared() : nullptr;
    }

    [[nodiscard]] IImageView* getImageView() const
    {
        if (renderImage && renderImage->getImageView()) {
            return renderImage->getImageView();
        }

        return texture ? texture->getImageView() : nullptr;
    }

    [[nodiscard]] const std::vector<std::shared_ptr<void>>& getRetainedResources() const
    {
        static const std::vector<std::shared_ptr<void>> EMPTY{};

        if (renderImage) {
            return renderImage->getRetainedResources();
        }

        if (texture) {
            return texture->getRetainedResources();
        }

        return EMPTY;
    }
};

} // namespace ya

#pragma once

#include "Core/Math/Geometry.h"

#include <memory>

namespace ya
{

struct RenderImage;
struct Texture;

struct ForwardViewportResources
{
    std::shared_ptr<Texture>     colorCompat   = nullptr;
    std::shared_ptr<Texture>     depthCompat   = nullptr;
    std::shared_ptr<Texture>     resolveCompat = nullptr;
    Texture*                    color        = nullptr;
    Texture*                    depth        = nullptr;
    Texture*                    resolve      = nullptr;
    std::shared_ptr<RenderImage> colorOwner   = nullptr;
    std::shared_ptr<RenderImage> depthOwner   = nullptr;
    std::shared_ptr<RenderImage> resolveOwner = nullptr;
    RenderImage*                colorImage   = nullptr;
    RenderImage*                depthImage   = nullptr;
    RenderImage*                resolveImage = nullptr;
    Extent2D                    extent{};

    [[nodiscard]] bool hasColor() const
    {
        return color != nullptr;
    }

    [[nodiscard]] bool hasColorImage() const
    {
        return colorImage != nullptr;
    }

    [[nodiscard]] bool hasDepth() const
    {
        return depth != nullptr;
    }

    void syncRawViews()
    {
        color       = colorCompat.get();
        depth       = depthCompat.get();
        resolve     = resolveCompat.get();
        colorImage   = colorOwner.get();
        depthImage   = depthOwner.get();
        resolveImage = resolveOwner.get();
    }
};

} // namespace ya

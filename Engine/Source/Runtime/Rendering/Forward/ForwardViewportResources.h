#pragma once

#include "Core/Math/Geometry.h"

#include <memory>

namespace ya
{

struct RenderImage;

struct ForwardViewportResources
{
    std::shared_ptr<RenderImage> colorOwner   = nullptr;
    std::shared_ptr<RenderImage> depthOwner   = nullptr;
    std::shared_ptr<RenderImage> resolveOwner = nullptr;
    RenderImage*                colorImage   = nullptr;
    RenderImage*                depthImage   = nullptr;
    RenderImage*                resolveImage = nullptr;
    Extent2D                    extent{};

    void syncRawViews()
    {
        colorImage   = colorOwner.get();
        depthImage   = depthOwner.get();
        resolveImage = resolveOwner.get();
    }
};

} // namespace ya

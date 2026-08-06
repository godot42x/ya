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

    [[nodiscard]] bool isComplete() const
    {
        return colorImage != nullptr && depthImage != nullptr;
    }

    void reset(Extent2D nextExtent = {})
    {
        colorOwner.reset();
        depthOwner.reset();
        resolveOwner.reset();
        colorImage   = nullptr;
        depthImage   = nullptr;
        resolveImage = nullptr;
        extent       = nextExtent;
    }

    void publish(std::shared_ptr<RenderImage> nextColorOwner,
                 std::shared_ptr<RenderImage> nextDepthOwner,
                 std::shared_ptr<RenderImage> nextResolveOwner,
                 Extent2D                     nextExtent)
    {
        colorOwner   = std::move(nextColorOwner);
        depthOwner   = std::move(nextDepthOwner);
        resolveOwner = std::move(nextResolveOwner);
        colorImage   = colorOwner.get();
        depthImage   = depthOwner.get();
        resolveImage = resolveOwner.get();
        extent       = nextExtent;
    }
};

} // namespace ya

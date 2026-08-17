#pragma once

#include "Core/Math/Geometry.h"

#include <memory>

namespace ya
{

struct RenderTexture;

struct ForwardViewportResources
{
    std::shared_ptr<RenderTexture> colorOwner   = nullptr;
    std::shared_ptr<RenderTexture> depthOwner   = nullptr;
    std::shared_ptr<RenderTexture> resolveOwner = nullptr;
    std::shared_ptr<RenderTexture> entityIdOwner = nullptr;
    RenderTexture*                colorImage   = nullptr;
    RenderTexture*                depthImage   = nullptr;
    RenderTexture*                resolveImage = nullptr;
    RenderTexture*                entityIdImage = nullptr;
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
        entityIdOwner.reset();
        colorImage   = nullptr;
        depthImage   = nullptr;
        resolveImage = nullptr;
        entityIdImage = nullptr;
        extent       = nextExtent;
    }

    void publish(std::shared_ptr<RenderTexture> nextColorOwner,
                 std::shared_ptr<RenderTexture> nextDepthOwner,
                 std::shared_ptr<RenderTexture> nextResolveOwner,
                 std::shared_ptr<RenderTexture> nextEntityIdOwner,
                 Extent2D                     nextExtent)
    {
        colorOwner   = std::move(nextColorOwner);
        depthOwner   = std::move(nextDepthOwner);
        resolveOwner = std::move(nextResolveOwner);
        entityIdOwner = std::move(nextEntityIdOwner);
        colorImage   = colorOwner.get();
        depthImage   = depthOwner.get();
        resolveImage = resolveOwner.get();
        entityIdImage = entityIdOwner.get();
        extent       = nextExtent;
    }
};

} // namespace ya

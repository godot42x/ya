#pragma once

#include "DeferredAttachmentFormats.h"

#include <memory>

namespace ya
{

struct RenderImage;

struct DeferredViewportResources
{
    std::shared_ptr<RenderImage> colorOwner = nullptr;
    std::shared_ptr<RenderImage> depthOwner = nullptr;
    std::shared_ptr<RenderImage> entityIdOwner = nullptr;
    RenderImage*                 color      = nullptr;
    RenderImage*                 depth      = nullptr;
    RenderImage*                 entityId   = nullptr;
    DeferredAttachmentFormats formats{};

    [[nodiscard]] bool isComplete() const
    {
        return color != nullptr && depth != nullptr;
    }

    void reset(const DeferredAttachmentFormats& nextFormats = {})
    {
        colorOwner.reset();
        depthOwner.reset();
        entityIdOwner.reset();
        color   = nullptr;
        depth   = nullptr;
        entityId = nullptr;
        formats = nextFormats;
    }

    void publish(std::shared_ptr<RenderImage> nextColorOwner,
                 std::shared_ptr<RenderImage> nextDepthOwner,
                 std::shared_ptr<RenderImage> nextEntityIdOwner,
                 const DeferredAttachmentFormats& nextFormats)
    {
        colorOwner = std::move(nextColorOwner);
        depthOwner = std::move(nextDepthOwner);
        entityIdOwner = std::move(nextEntityIdOwner);
        color      = colorOwner.get();
        depth      = depthOwner.get();
        entityId   = entityIdOwner.get();
        formats    = nextFormats;
    }
};

} // namespace ya

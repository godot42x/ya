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
    RenderImage*                 color      = nullptr;
    RenderImage*                 depth      = nullptr;
    DeferredAttachmentFormats formats{};

    [[nodiscard]] bool isComplete() const
    {
        return color != nullptr && depth != nullptr;
    }

    void reset(const DeferredAttachmentFormats& nextFormats = {})
    {
        colorOwner.reset();
        depthOwner.reset();
        color   = nullptr;
        depth   = nullptr;
        formats = nextFormats;
    }

    void publish(std::shared_ptr<RenderImage> nextColorOwner,
                 std::shared_ptr<RenderImage> nextDepthOwner,
                 const DeferredAttachmentFormats& nextFormats)
    {
        colorOwner = std::move(nextColorOwner);
        depthOwner = std::move(nextDepthOwner);
        color      = colorOwner.get();
        depth      = depthOwner.get();
        formats    = nextFormats;
    }
};

} // namespace ya

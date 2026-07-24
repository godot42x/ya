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

    void syncRawViews()
    {
        color = colorOwner.get();
        depth = depthOwner.get();
    }
};

} // namespace ya

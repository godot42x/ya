#pragma once

#include "DeferredAttachmentFormats.h"

namespace ya
{

struct RenderImage;

struct DeferredViewportResources
{
    RenderImage* color = nullptr;
    RenderImage* depth = nullptr;
    DeferredAttachmentFormats formats{};

    [[nodiscard]] bool isComplete() const
    {
        return color != nullptr && depth != nullptr;
    }
};

} // namespace ya

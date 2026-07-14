#pragma once

#include "DeferredAttachmentFormats.h"

#include <array>

namespace ya
{

struct RenderImage;

struct DeferredGBufferResources
{
    std::array<RenderImage*, 4> color{};
    RenderImage*                depth = nullptr;
    DeferredAttachmentFormats formats{};

    [[nodiscard]] bool isComplete() const
    {
        return color[0] != nullptr &&
               color[1] != nullptr &&
               color[2] != nullptr &&
               color[3] != nullptr &&
               depth != nullptr;
    }
};

} // namespace ya

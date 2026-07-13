#pragma once

#include "DeferredAttachmentFormats.h"

namespace ya
{

struct Texture;

struct DeferredViewportResources
{
    Texture* color = nullptr;
    Texture* depth = nullptr;
    DeferredAttachmentFormats formats{};

    [[nodiscard]] bool isComplete() const
    {
        return color != nullptr && depth != nullptr;
    }
};

} // namespace ya

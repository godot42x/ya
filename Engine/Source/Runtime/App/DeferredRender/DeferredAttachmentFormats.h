#pragma once

#include "Render/RenderDefines.h"

#include <optional>
#include <vector>

namespace ya
{

struct DeferredAttachmentFormats
{
    std::vector<EFormat::T>     colorFormats;
    std::optional<EFormat::T>   depthFormat{};

    [[nodiscard]] bool hasColor() const
    {
        return !colorFormats.empty();
    }
};

} // namespace ya

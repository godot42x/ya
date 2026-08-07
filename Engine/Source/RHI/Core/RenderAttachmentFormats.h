#pragma once

#include "RHI/RenderDefines.h"

#include <optional>
#include <vector>

namespace ya
{

struct RenderAttachmentFormats
{
    std::vector<EFormat::T>   colorFormats;
    std::optional<EFormat::T> depthFormat{};

    [[nodiscard]] bool hasColor() const
    {
        return !colorFormats.empty();
    }
};

} // namespace ya

#pragma once

#include "Core/Math/Geometry.h"

namespace ya
{

struct Texture;

struct ForwardViewportResources
{
    Texture* color   = nullptr;
    Texture* depth   = nullptr;
    Texture* resolve = nullptr;
    Extent2D extent{};

    [[nodiscard]] bool hasColor() const
    {
        return color != nullptr;
    }

    [[nodiscard]] bool hasDepth() const
    {
        return depth != nullptr;
    }
};

} // namespace ya

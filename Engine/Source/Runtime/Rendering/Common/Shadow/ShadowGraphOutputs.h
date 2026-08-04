#pragma once

#include "Render/Core/RenderGraph.h"

#include <optional>

namespace ya
{

struct ShadowGraphOutputs
{
    std::optional<RGPassHandle>   lastPass{};
    std::optional<RGTextureHandle> shadowDepth{};
};

} // namespace ya

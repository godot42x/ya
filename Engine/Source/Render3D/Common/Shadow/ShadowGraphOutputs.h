#pragma once

#include "RenderGraph/RenderGraph.h"

#include <optional>

namespace ya
{

struct ShadowGraphOutputs
{
    std::optional<RGTextureHandle> shadowDepth{};
};

} // namespace ya

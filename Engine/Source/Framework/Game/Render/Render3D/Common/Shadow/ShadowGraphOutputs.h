#pragma once

#include "Framework/Game/Render/Graph/RenderGraph.h"

#include <optional>

namespace ya
{

struct ShadowGraphOutputs
{
    std::optional<RGTextureHandle> shadowDepth{};
};

} // namespace ya

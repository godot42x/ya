#pragma once

#include "DeferredGBufferResources.h"
#include "DeferredViewportResources.h"
#include "Render/Core/RenderImage.h"

namespace ya
{

struct DeferredPipelineDebugViews
{
    DeferredGBufferResources  gBufferResources{};
    DeferredViewportResources viewportResources{};
    RenderImage*              ssaoTexture = nullptr;
};

} // namespace ya

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
    std::shared_ptr<RenderImage> ssaoTextureOwner = nullptr;
};

} // namespace ya

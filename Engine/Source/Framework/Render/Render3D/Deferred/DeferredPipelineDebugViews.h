#pragma once

#include "DeferredGBufferResources.h"
#include "DeferredViewportResources.h"
#include "RHI/Core/RenderTexture.h"

namespace ya
{

struct DeferredPipelineDebugViews
{
    DeferredGBufferResources  gBufferResources{};
    DeferredViewportResources viewportResources{};
    std::shared_ptr<RenderTexture> ssaoTextureOwner = nullptr;
};

} // namespace ya

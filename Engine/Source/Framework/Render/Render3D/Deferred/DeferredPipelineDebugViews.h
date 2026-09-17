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
    std::shared_ptr<RenderTexture> postprocess      = nullptr;
    std::shared_ptr<RenderTexture> bloomExtract     = nullptr;
    std::shared_ptr<RenderTexture> bloomBlur        = nullptr;
    std::shared_ptr<RenderTexture> bloomComposite   = nullptr;
};

} // namespace ya

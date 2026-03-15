#include "DeferredRenderPipeline.h"
#include "Runtime/App/App.h"

namespace ya

{

void DeferredRenderPipeline::tick(ICommandBuffer* cmdBuf)
{
    // auto scene = getActiveScene();

    // 1. grab all mesh with * material, render to geometry
    cmdBuf->beginRendering(RenderingInfo{
        .label      = "GBuffer Pass",
        .renderArea = Rect2D{
            .pos    = {0, 0},
            .extent = _gBufferRT->getExtent().toVec2(),
        },
        .layerCount       = 1,
        .colorClearValues = {
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
        },
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget    = _gBufferRT.get(),
    });



    cmdBuf->endRendering({.renderTarget = _gBufferRT.get()});
}

} // namespace ya

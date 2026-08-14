#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Render3D/Deferred/DeferredFrameGraphResources.h"

int main()
{
    ya::ShadowGraphOutputs outputs{};
    if (outputs.shadowDepth.has_value()) {
        return 1;
    }

    ya::DeferredFrameGraphResources::Passes passes{};
    if (passes.shadow.shadowDepth.has_value() || passes.gBuffer.has_value() || passes.light.has_value() ||
        passes.skybox.has_value() || passes.sceneOverlay.has_value() || passes.viewportOverlay.has_value()) {
        return 2;
    }

    return 0;
}

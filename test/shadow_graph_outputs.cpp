#include "Runtime/Rendering/Common/Shadow/ShadowGraphOutputs.h"
#include "Runtime/Rendering/Deferred/DeferredFrameGraphResources.h"

int main()
{
    ya::ShadowGraphOutputs outputs{};
    if (outputs.lastPass.has_value() || outputs.shadowDepth.has_value()) {
        return 1;
    }

    ya::DeferredFrameGraphResources::Passes passes{};
    if (passes.shadow.lastPass.has_value() || passes.shadow.shadowDepth.has_value()) {
        return 2;
    }

    return 0;
}

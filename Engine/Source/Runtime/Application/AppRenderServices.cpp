#include "Runtime/Application/AppRenderServices.h"

#include "Runtime/Application/AppRenderState.h"

#include "Core/Log.h"
#include "Runtime/Rendering/Services/DebugRenderSystem.h"

namespace ya
{

IRender* AppRenderServices::getRender() const
{
    return _state && _state->runtime ? _state->runtime->getRender() : nullptr;
}

std::shared_ptr<ShaderStorage> AppRenderServices::getShaderStorage() const
{
    return _state && _state->runtime ? _state->runtime->getShaderStorage() : nullptr;
}

RenderRuntime* AppRenderServices::getRenderRuntime() const
{
    return _state ? _state->runtime.get() : nullptr;
}

ShadowSettings& AppRenderServices::getShadowSettings()
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->shadowSettings;
}

const ShadowSettings& AppRenderServices::getShadowSettings() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->shadowSettings;
}

IRenderPipeline* AppRenderServices::getRenderPipeline() const
{
    return _state && _state->runtime ? _state->runtime->getActivePipeline() : nullptr;
}

DebugRenderSystem& AppRenderServices::getDebugRenderSystem() const
{
    YA_CORE_ASSERT(_state && _state->runtime, "RenderRuntime is not available");
    return _state->runtime->getDebugRenderSystem();
}

bool AppRenderServices::isShadowMappingEnabled() const
{
    return _state && _state->runtime && _state->runtime->isShadowMappingEnabled();
}

IImageView* AppRenderServices::getShadowDirectionalDepthIV() const
{
    return _state && _state->runtime ? _state->runtime->getShadowDirectionalDepthIV() : nullptr;
}

IImageView* AppRenderServices::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    return _state && _state->runtime
             ? _state->runtime->getShadowPointFaceDepthIV(pointLightIndex, faceIndex)
             : nullptr;
}

bool AppRenderServices::isPostprocessingEnabled() const
{
    return _state && _state->runtime && _state->runtime->isPostprocessingEnabled();
}

const AppRenderFrameState& AppRenderServices::getRenderFrameState() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->frameState;
}

void AppRenderServices::setExtensionRenderFrameState(const AppRenderFrameState& state)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->extensionFrameState = state;
}

void AppRenderServices::clearExtensionRenderFrameState()
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->extensionFrameState.reset();
}

} // namespace ya

#include "GameRuntime/AppRenderServices.h"

#include "GameRuntime/AppRenderState.h"

#include "Core/Log.h"
#include "Render3D/Services/DebugRenderSystem.h"

#include <algorithm>

namespace ya
{

IRender* AppRenderServices::getRender() const
{
    return _state && _state->device ? _state->device->getRender() : nullptr;
}

std::shared_ptr<ShaderStorage> AppRenderServices::getShaderStorage() const
{
    return _state && _state->device ? _state->device->getShaderStorage() : nullptr;
}

RenderDeviceState* AppRenderServices::getDeviceState() const
{
    return _state ? _state->device.get() : nullptr;
}

bool AppRenderServices::wasSceneRenderedLastTick(const Scene* scene) const
{
    if (!_state || !scene) {
        return false;
    }
    const auto& rendered = _state->renderedScenesLastTick;
    return std::find(rendered.begin(), rendered.end(), scene) != rendered.end();
}

void AppRenderServices::setRenderScale(float scale)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->hostView.renderScale = scale;
}

float AppRenderServices::getRenderScale() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->hostView.renderScale;
}

void AppRenderServices::setRenderResolution(Extent2D resolution)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->hostView.renderResolution = resolution;
}

Extent2D AppRenderServices::getRenderResolution() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->hostView.renderResolution;
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
    return _state && _state->device ? _state->device->getActivePipeline() : nullptr;
}

DebugRenderSystem& AppRenderServices::getDebugRenderSystem() const
{
    YA_CORE_ASSERT(_state && _state->device, "RenderDeviceState is not available");
    return _state->device->getDebugRenderSystem();
}

bool AppRenderServices::isShadowMappingEnabled() const
{
    return _state && _state->device && _state->device->isShadowMappingEnabled();
}

std::shared_ptr<ImageResource> AppRenderServices::getShadowDirectionalDepthResource() const
{
    return _state && _state->device ? _state->device->getShadowDirectionalDepthResource() : nullptr;
}

std::shared_ptr<ImageResource> AppRenderServices::getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    return _state && _state->device
             ? _state->device->getShadowPointFaceDepthResource(pointLightIndex, faceIndex)
             : nullptr;
}

bool AppRenderServices::isPostprocessingEnabled() const
{
    return _state && _state->device && _state->device->isPostprocessingEnabled();
}

const HostViewState& AppRenderServices::getHostViewState() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->hostView;
}

} // namespace ya

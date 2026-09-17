#include "GameRuntime/AppRenderServices.h"

#include "GameRuntime/AppRenderState.h"

#include "Core/Log.h"
#include "Render3D/Services/DebugRenderSystem.h"

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

RenderFrameCoordinator* AppRenderServices::getFrameCoordinator() const
{
    return _state ? _state->coordinator.get() : nullptr;
}

void AppRenderServices::setWorldSceneRenderEnabled(bool bEnabled)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->bWorldSceneRenderEnabled = bEnabled;
}

bool AppRenderServices::isWorldSceneRenderEnabled() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->bWorldSceneRenderEnabled;
}

void AppRenderServices::setViewportFrameBufferScale(float scale)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->frameState.viewportFrameBufferScale = scale;
}

float AppRenderServices::getViewportFrameBufferScale() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->frameState.viewportFrameBufferScale;
}

void AppRenderServices::setViewportRect(Rect2D rect)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->frameState.viewportRect = rect;
}

Rect2D AppRenderServices::getViewportRect() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->frameState.viewportRect;
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

void AppRenderServices::setCameraPreviewHostOwned(bool bOwned)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->bCameraPreviewHostOwned = bOwned;
}

void AppRenderServices::setCameraPreviewEntityUUID(uint64_t uuid)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->cameraPreviewEntityUUID = uuid;
}

bool AppRenderServices::isCameraPreviewHostOwned() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->bCameraPreviewHostOwned;
}

uint64_t AppRenderServices::getCameraPreviewEntityUUID() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->cameraPreviewEntityUUID;
}

} // namespace ya

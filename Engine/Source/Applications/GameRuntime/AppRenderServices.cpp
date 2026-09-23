#include "GameRuntime/AppRenderServices.h"

#include "GameRuntime/AppRenderState.h"

#include "Core/Log.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/Services/DebugRenderSystem.h"
#include "Render3D/Services/RenderDiagnosticsService.h"

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
    _state->hostSettings.renderScale = scale;
}

float AppRenderServices::getRenderScale() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->hostSettings.renderScale;
}

void AppRenderServices::setRenderResolution(Extent2D resolution)
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    _state->hostSettings.renderResolution = resolution;
}

Extent2D AppRenderServices::getRenderResolution() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->hostSettings.renderResolution;
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

bool AppRenderServices::isGradingEnabled() const
{
    return _state && _state->device && _state->device->isGradingEnabled();
}

const HostRenderSettings& AppRenderServices::getHostRenderSettings() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->hostSettings;
}

const HostViewportView& AppRenderServices::getHostViewportView() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->hostViewport;
}

bool AppRenderServices::hasRenderer() const
{
    return _state != nullptr && _state->device != nullptr;
}

ERenderPipelineKind AppRenderServices::getRenderPipelineKind() const
{
    return _state && _state->device ? _state->device->getRenderPipeline()
                                    : ERenderPipelineKind::Deferred;
}

ERenderPipelineKind AppRenderServices::getPendingRenderPipelineKind() const
{
    return _state && _state->device ? _state->device->getPendingRenderPipeline()
                                    : ERenderPipelineKind::Deferred;
}

void AppRenderServices::setPendingRenderPipelineKind(ERenderPipelineKind kind)
{
    YA_CORE_ASSERT(_state && _state->device, "RenderDeviceState is not available");
    _state->device->setPendingRenderPipeline(kind);
}

void AppRenderServices::requestRenderPipelineReload()
{
    YA_CORE_ASSERT(_state && _state->device, "RenderDeviceState is not available");
    _state->device->requestActivePipelineReload();
}

RenderPipelineSettings AppRenderServices::getRenderPipelineSettings() const
{
    return _state && _state->device ? _state->device->resolveActivePipelineSettings()
                                    : RenderPipelineSettings{};
}

void AppRenderServices::setRenderPipelineSettings(const RenderPipelineSettings& settings)
{
    YA_CORE_ASSERT(_state && _state->device, "RenderDeviceState is not available");
    _state->device->requestActivePipelineSettings(settings);
}

const std::vector<RGTopologyDescription>* AppRenderServices::getFrameGraphTopologies() const
{
    return _state && _state->device ? &_state->device->getFrameGraphTopologies() : nullptr;
}

const RenderViewOutput* AppRenderServices::getHostViewportOutput() const
{
    return getViewOutput(_state ? _state->hostViewport.viewId : 0);
}

SceneViewId AppRenderServices::getHostViewportViewId() const
{
    return _state ? _state->hostViewport.viewId : 0;
}

EFormat::T AppRenderServices::getViewDepthFormat() const
{
    return _state && _state->device ? _state->device->getViewDepthFormat() : EFormat::Undefined;
}

const RenderViewOutput* AppRenderServices::getViewOutput(SceneViewId viewId) const
{
    return (_state && _state->device)
               ? _state->device->getViewOutput(_state->hostViewport.flightIndex, viewId)
               : nullptr;
}

RenderViewportSnapshot AppRenderServices::buildViewportSnapshot(Scene* inspectScene) const
{
    return (_state && _state->device)
               ? _state->device->buildViewportSnapshot(_state->hostViewport.flightIndex,
                                                      _state->hostViewport.viewId,
                                                      inspectScene)
               : RenderViewportSnapshot{};
}

RenderTargetCatalog AppRenderServices::buildRenderTargetCatalog() const
{
    // The window this app presents: which one that is belongs here, at the app,
    // not inside the renderer as an implicit "primary". Picking per display root
    // is AB4-2b, when a frame can present more than one.
    IRender*     render = getRender();
    IRenderSurfaceContext* hostSurface = render ? render->getPrimarySurfaceContext() : nullptr;
    if (!_state || !_state->device || !hostSurface) {
        return RenderTargetCatalog{};
    }
    return _state->device->buildRenderTargetCatalog(*hostSurface);
}

void AppRenderServices::requestRenderTargetFormat(const RenderTargetFormatCommand& command)
{
    if (_state && _state->device) {
        _state->device->requestRenderTargetFormat(command);
    }
}

DebugRenderSystem& AppRenderServices::getDebugRenderSystem() const
{
    YA_CORE_ASSERT(_state && _state->device, "RenderDeviceState is not available");
    return _state->device->getDebugRenderSystem();
}

RenderDiagnosticsService& AppRenderServices::getDiagnosticsService() const
{
    YA_CORE_ASSERT(_state && _state->device, "RenderDeviceState is not available");
    return _state->device->getDiagnosticsService();
}

} // namespace ya

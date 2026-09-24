#include "GameRuntime/AppRenderServices.h"

#include "GameRuntime/AppRenderState.h"

#include "Core/Log.h"
#include "RHI/Core/Swapchain.h"
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

IRenderSurfaceContext* AppRenderServices::getHostSurface() const
{
    IRender* render = getRender();
    if (!render || !_state) {
        return nullptr;
    }
    // The app names the surface it presents through; the renderer has no
    // default window to fall back to.
    return render->findSurface(_state->hostSurfaceId);
}

SurfaceId AppRenderServices::getHostSurfaceId() const
{
    return _state ? _state->hostSurfaceId : SurfaceId{};
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

const DisplayedView& AppRenderServices::getDisplayedView() const
{
    YA_CORE_ASSERT(_state, "Render services are not available");
    return _state->displayedView;
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

const RenderViewOutput* AppRenderServices::getDisplayedViewOutput() const
{
    return getViewOutput(_state ? _state->displayedView.viewId : 0);
}

SceneViewId AppRenderServices::getDisplayedViewId() const
{
    return _state ? _state->displayedView.viewId : 0;
}

EFormat::T AppRenderServices::getViewDepthFormat() const
{
    return _state && _state->device ? _state->device->getViewDepthFormat() : EFormat::Undefined;
}

const RenderViewOutput* AppRenderServices::getViewOutput(SceneViewId viewId) const
{
    return (_state && _state->device)
               ? _state->device->getViewOutput(_state->displayedView.flightIndex, viewId)
               : nullptr;
}

RenderViewportSnapshot AppRenderServices::buildViewportSnapshot(Scene* inspectScene) const
{
    return (_state && _state->device)
               ? _state->device->buildViewportSnapshot(_state->displayedView.flightIndex,
                                                      _state->displayedView.viewId,
                                                      inspectScene)
               : RenderViewportSnapshot{};
}

RenderTargetCatalog AppRenderServices::buildRenderTargetCatalog() const
{
    // The window this app presents: which one that is belongs here, at the app,
    // not inside the renderer as an implicit "primary". Picking per display root
    // is AB4-2b, when a frame can present more than one.
    //
    // Assembled from the renderer's published data, here at the app: the
    // presentation entry from the surface this app names, plus the active
    // strategy's per-View entries. The renderer publishes both and assembles
    // no panel views.
    RenderTargetCatalog catalog{};
    if (!_state || !_state->device) {
        return catalog;
    }
    IRenderSurfaceContext* hostSurface = getHostSurface();
    if (auto presentationImage =
            hostSurface ? _state->device->getPresentationImageShared(*hostSurface) : nullptr) {
        auto* swapchain = hostSurface->getSwapchain();
        catalog.entries.push_back({
            .label            = "Presentation",
            .owner            = RenderTargetCatalog::Entry::EOwner::Presentation,
            .colorFormats     = {presentationImage->getFormat()},
            .colorAttachments = {presentationImage},
            .extent           = presentationImage->getExtent(),
            .frameBufferCount = swapchain ? swapchain->getImageCount() : 1,
            .bSwapChainTarget = true,
            .bEditable        = false,
        });
    }
    _state->device->appendRenderTargetEntries(catalog);
    return catalog;
}

void AppRenderServices::requestRenderTargetFormat(const RenderTargetFormatCommand& command)
{
    if (_state && _state->device) {
        _state->device->requestRenderTargetFormat(command);
    }
}

DebugPrimitives::SettingsSnapshot AppRenderServices::getDebugRenderSettings() const
{
    return _state && _state->device
               ? _state->device->getDebugRenderSystem().buildSettingsSnapshot()
               : DebugPrimitives::SettingsSnapshot{};
}

void AppRenderServices::requestDebugRenderSettings(const DebugPrimitives::SettingsSnapshot& settings)
{
    if (_state && _state->device) {
        _state->device->getDebugRenderSystem().requestSettings(settings);
    }
}

RenderDiagnosticsService::RenderDocPanelState AppRenderServices::getRenderDocPanelState() const
{
    return _state && _state->device
               ? _state->device->getDiagnosticsService().buildRenderDocPanelState()
               : RenderDiagnosticsService::RenderDocPanelState{};
}

void AppRenderServices::requestRenderDocCaptureEnabled(bool bEnabled)
{
    if (_state && _state->device) {
        _state->device->getDiagnosticsService().requestRenderDocCaptureEnabled(bEnabled);
    }
}

void AppRenderServices::requestRenderDocHUDVisible(bool bVisible)
{
    if (_state && _state->device) {
        _state->device->getDiagnosticsService().requestRenderDocHUDVisible(bVisible);
    }
}

void AppRenderServices::requestRenderDocCaptureNextFrame()
{
    if (_state && _state->device) {
        _state->device->getDiagnosticsService().requestRenderDocCaptureNextFrame();
    }
}

void AppRenderServices::requestRenderDocCaptureAfterFrames(uint32_t frameCount)
{
    if (_state && _state->device) {
        _state->device->getDiagnosticsService().requestRenderDocCaptureAfterFrames(frameCount);
    }
}

} // namespace ya

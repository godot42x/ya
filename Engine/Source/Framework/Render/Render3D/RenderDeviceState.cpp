#include "Render3D/RenderDeviceState.h"
#include "Render3D/Common/RenderRuntimeHostServices.h"

#include "Render3D/Services/DebugRenderSystem.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Common/RenderOverlay.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Texture.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "Render2D/Render2D.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "Render3D/Services/PipelineCoordinator.h"

#include <format>
#include <limits>
#include <vector>

namespace ya
{

void RenderDeviceState::prepareDerivedState(std::span<Scene* const> scenes, float dt)
{
    // Each processor resolves work per Scene, so the second Scene of the tick
    // cannot drop or overwrite the first one's.
    if (_environmentLightingProcessor) {
        _environmentLightingProcessor->prepareScenes(scenes, dt);
    }
    if (_terrainProcessor) {
        _terrainProcessor->prepareScenes(scenes, dt);
    }
    if (_gameplayResourceBinding) {
        _gameplayResourceBinding->prepareScenes(scenes, dt);
    }
    for (Scene* scene : scenes) {
        if (!scene) {
            continue;
        }
        (void)_sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
        (void)_sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
    }
}

void RenderDeviceState::prepareComposePipelines()
{
    // The runtime's own UI compose only: the packet that goes onto the display
    // RT after the world graph. The editor's compose kinds (its viewport image
    // and canvas preview) are the editor's pipelines -- it records them onto
    // its own targets and prepares them itself, earlier in the same tick -- so
    // the renderer must not know they exist.
    if (auto* pipeline = getActivePipeline()) {
        // The pipeline's postprocess format is the format of the image the UI
        // composes onto. Asked of the pipeline rather than read back from a View
        // output: the compose pipeline must exist before the graph creates that
        // image, and the format is stable pipeline configuration.
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::RuntimeUIComposite,
            },
            pipeline->getPostprocessColorFormat());
    }
}

IRenderPipeline* RenderDeviceState::getActivePipeline() const
{
    return _pipelineCoordinator.getActivePipeline();
}

uint64_t RenderDeviceState::getHostTick() const
{
    return _clockState ? _clockState->hostTick : 0;
}

double RenderDeviceState::getElapsedTimeSeconds() const
{
    return _clockState ? static_cast<double>(_clockState->elapsedTimeMS) / 1000.0 : 0.0;
}

EnvironmentLightingProcessor* RenderDeviceState::getEnvironmentLightingProcessor() const
{
    return _environmentLightingProcessor.get();
}

bool RenderDeviceState::isShadowMappingEnabled() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isShadowMappingEnabled();
    }
    return false;
}

std::shared_ptr<ImageResource> RenderDeviceState::getShadowDirectionalDepthResource() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowDirectionalDepthResource();
    }
    return nullptr;
}

std::shared_ptr<ImageResource> RenderDeviceState::getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowPointFaceDepthResource(pointLightIndex, faceIndex);
    }
    return nullptr;
}

std::shared_ptr<RenderTexture> RenderDeviceState::getPresentationImageShared(
    IRenderSurfaceContext& surface) const
{
    // Non-creating: a reader asks what the window shows, and a surface no frame
    // has ever presented through shows nothing -- building its images to answer
    // would be a side effect of a query.
    SurfacePresentation* presentation = findSurfacePresentation(surface);
    return presentation ? presentation->currentImageShared() : nullptr;
}

bool RenderDeviceState::isGradingEnabled() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isGradingEnabled();
    }
    return false;
}

RenderPipelineDebugOutputCatalog RenderDeviceState::buildPipelineDebugOutputCatalog(uint32_t   flightIndex,
                                                                                    SceneViewId viewId) const
{
    RenderPipelineDebugOutputCatalog catalog{};
    auto* pipeline = getActivePipeline();
    if (!pipeline) {
        return catalog;
    }

    catalog.bShadowMappingEnabled   = pipeline->isShadowMappingEnabled();
    catalog.shadowDirectionalDepthResource = pipeline->getShadowDirectionalDepthResource();
    catalog.viewDepthImageOwner = nullptr;
    catalog.bPostprocessingEnabled = pipeline->isGradingEnabled();

    if (const auto* output = getViewOutput(flightIndex, viewId)) {
        catalog.viewOutputImageOwner    = output->color;
        catalog.viewDepthImageOwner     = output->depth;
        catalog.postprocessOutputImageOwner = (output->display && output->display != output->color)
            ? output->display
            : nullptr;
        catalog.bloomExtractOwner           = output->bloomExtract;
        catalog.bloomBlurOwner              = output->bloomBlur;
        catalog.bloomCompositeOwner         = output->bloomComposite;
        catalog.ssaoOwner                   = output->ssao;
        catalog.gBufferColorOwners          = output->gBufferColors;
        return catalog;
    }

    return catalog;
}

EFormat::T RenderDeviceState::getViewDepthFormat() const
{
    if (const IRenderPipeline* pipeline = getActivePipeline()) {
        return pipeline->getViewDepthFormat();
    }
    return EFormat::Undefined;
}

ViewportDebugCatalogInput RenderDeviceState::makeViewportDebugCatalogInput(uint32_t   flightIndex,
                                                                          SceneViewId viewId,
                                                                          Scene*      inspectScene) const
{
    // The one place that decides which of this renderer's resources the
    // inspector may show. Everything downstream is a pure function of this
    // value, so "what the panel displays" cannot drift from what the device
    // actually published.
    ViewportDebugCatalogInput input;
    input.bForwardPipeline  = (_pipelineCoordinator.getRenderPipeline() == ERenderPipeline::Forward);
    input.bDeferredPipeline = _pipelineCoordinator.hasDeferredPipeline();
    input.debugOutputs      = buildPipelineDebugOutputCatalog(flightIndex, viewId);
    input.brdfLut           = _sharedResourceProvider.getBrdfLutTextureShared();
    input.environmentLighting = (inspectScene && _environmentLightingProcessor)
        ? _environmentLightingProcessor->findSceneWork(*inspectScene)
        : nullptr;
    input.inspectScene        = inspectScene;

    for (uint32_t pointLightIndex = 0; pointLightIndex < MAX_POINT_LIGHTS; ++pointLightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < ShadowConstants::FACES_PER_POINT_LIGHT; ++faceIndex) {
            const size_t index = static_cast<size_t>(pointLightIndex) * ShadowConstants::FACES_PER_POINT_LIGHT + faceIndex;
            if (index < input.pointShadowFaces.size()) {
                input.pointShadowFaces[index] = getShadowPointFaceDepthResource(pointLightIndex, faceIndex);
            }
        }
    }
    return input;
}

RenderDeviceState::ERenderPipeline RenderDeviceState::resolveActivePipelineKind() const
{
    return _pipelineCoordinator.getRenderPipeline();
}

RenderPipelineSettings RenderDeviceState::resolveActivePipelineSettings() const
{
    // No pipeline (between a switch and its rebuild) is a real answer: the
    // default value names Deferred, which is what the coordinator selects when
    // nothing has been asked for yet.
    if (const IRenderPipeline* pipeline = getActivePipeline()) {
        return pipeline->resolveSettings();
    }
    return {};
}

void RenderDeviceState::requestActivePipelineSettings(const RenderPipelineSettings& settings)
{
    if (IRenderPipeline* pipeline = getActivePipeline()) {
        pipeline->requestSettings(settings);
    }
}

const std::vector<RGTopologyDescription>& RenderDeviceState::getFrameGraphTopologies() const
{
    return _frameGraphTopologies;
}

RenderTargetCatalog RenderDeviceState::buildRenderTargetCatalog() const
{
    RenderTargetCatalog catalog{};

    // The catalog's surface entry has always meant the primary window's. Naming
    // that surface is what keeps the entry honest now that presentations are
    // per-surface; this editor-facing query leaving the renderer entirely is
    // plan AB3.
    IRenderSurfaceContext* primary      = _render ? _render->getPrimarySurfaceContext() : nullptr;
    SurfacePresentation*   presentation = primary ? findSurfacePresentation(*primary) : nullptr;
    if (auto presentationImage = presentation ? presentation->currentImageShared() : nullptr) {
        auto* swapchain = presentation->swapchain();
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
    if (auto* pipeline = getActivePipeline()) {
        pipeline->appendRenderTargetEntries(catalog);
    }

    return catalog;
}

void RenderDeviceState::requestRenderTargetFormat(const RenderTargetFormatCommand& command)
{
    _pipelineCoordinator.requestRenderTargetFormat(command);
}

DebugRenderSystem& RenderDeviceState::getDebugRenderSystem() const
{
    return DebugRenderSystem::get();
}

} // namespace ya

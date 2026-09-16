#include "RenderRuntime.h"
#include "Render3D/Common/RenderRuntimeHostServices.h"
#include "Render3D/Common/ViewCompose.h"

#include "Render3D/Services/DebugRenderSystem.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Common/RenderOverlay.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
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

void RenderRuntime::onViewportResized(Rect2D rect)
{
    _viewportState.setRect(rect);

    if (auto* pipeline = getActivePipeline()) {
        pipeline->onViewportResized(rect);
    }
}

ICommandBuffer* RenderRuntime::renderFrame(const FrameInput& input)
{
    YA_PROFILE_SCOPE("RenderRuntime::renderFrame");
    YA_PERF_SCOPE(perf::sample::renderRuntime(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (!validateSceneRenderInput(input)) {
        return nullptr;
    }

    // One Camera chain (Unity-style). The unit is the camera, not the window:
    //   graphics  → world graph into this camera's offscreen RT
    //   UI        → game UI onto that RT (after post, never into bloom)
    //   view compose → editor overlays onto that RT
    //   display compose → PresentationGraphService onto swapchain[imageIndex]
    // Acquire/present are the host FPresentFrame coordinator's job. Swapchain
    // images are chosen only at display compose, never as the graphics target.

    // Tick owned derived-processing systems (gameplay binding / environment
    // lighting / terrain) inside the render frame; the Host no longer drives
    // them through its generic system list.
    if (_environmentLightingProcessor) {
        _environmentLightingProcessor->onUpdate(input.camera.deltaTime);
    }
    if (_terrainProcessor) {
        _terrainProcessor->onUpdate(input.camera.deltaTime);
    }
    if (_gameplayResourceBinding) {
        _gameplayResourceBinding->onUpdate(input.camera.deltaTime);
    }

    // Skybox / IBL descriptor writes are not UPDATE_AFTER_BIND. Preprocess
    // can publish new cubemap / irradiance / prefilter views in onUpdate
    // above; rewrite those sets before this frame's command buffer begins
    // recording. Tick-time getters then become a no-op when views are stable.
    {
        Scene* scene = getActiveScene();
        (void)_sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
        (void)_sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
    }

    _pipelineCoordinator.applyPendingChanges();

    // All Render2D pipeline changes must happen before command recording. The
    // post-process/viewport target format can differ from the initial viewport
    // format (for example HDR R16G16B16A16_SFLOAT), so resolve it from the
    // active pipeline before beginning this frame's command buffer.
    if (auto* pipeline = getActivePipeline()) {
        // Viewport overlay (screen sprites + world debug lines) owns its own
        // pass slot; keep its screen pipeline in sync with the viewport
        // attachment formats.
        prepareRenderViewportOverlayPipeline(pipeline->getViewportColorFormat(),
                                             pipeline->getViewportDepthFormat());
        // Editor viewport compose records after applyPendingChanges in this
        // same function. Logic-tick prep still used the previous pipeline's
        // depth, and switching Deferred/Forward must not wait until the next
        // onLogic (that would be after this frame's command recording).
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::EditorViewportCompose,
            },
            kEditorViewportComposeColorFormat,
            pipeline->getViewportDepthFormat());
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::EditorCanvasPreview,
            },
            kEditorViewportComposeColorFormat);
    }
    // The Runtime UI composite target (viewport display image) is created
    // during the world render below, so on the first frame the gated prep
    // cannot see it yet while the record later in this function still runs.
    // Guarantee the UI slot pipeline exists up front with the display image's
    // deterministic format; preparePassPipeline is cached per format and
    // re-creates the pipeline for the actual image format if it ever differs.
    if (auto* pipeline = getActivePipeline()) {
        prepareRender2DComposePassPipeline(
            FRender2DComposePassDesc{
                .kind = ERender2DComposePassKind::RuntimeUIComposite,
            },
            getViewportDisplayImageFormat());
    }
    if (input.camera.uiFrameSnapshot) {
        auto uiTarget = getViewportDisplayImageShared();
        if (uiTarget) {
            prepareRender2DComposePassPipeline(
                FRender2DComposePassDesc{
                    .kind = ERender2DComposePassKind::RuntimeUIComposite,
                },
                uiTarget->getFormat());
        }
    }

    std::shared_ptr<ICommandBuffer> cmdBuf;
    if (!prepareFrame(input, cmdBuf)) {
        return nullptr;
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (_viewportState.isWorldSceneRenderEnabled()) {
            renderWorldFrame(input, cmdBuf.get());
        }
    }
    // View compose writes this Camera's offscreen display RT (UI + gizmos +
    // extra View insets). Display compose then writes swapchain[imageIndex].
    std::vector<ViewDisplayInset> composeInsets = input.viewCompose.insets;
    if (input.sceneRender.plan) {
        for (const auto& inset : viewDisplayInsetsFromPlan(*input.sceneRender.plan)) {
            bool bExists = false;
            for (const auto& existing : composeInsets) {
                if (existing.viewId == inset.viewId) {
                    bExists = true;
                    break;
                }
            }
            if (!bExists) {
                composeInsets.push_back(inset);
            }
        }
    }
    std::vector<ViewDisplayInsetImage> insetImages;
    insetImages.reserve(composeInsets.size());
    for (const auto& inset : composeInsets) {
        const RenderViewOutput* output = getViewOutput(inset.viewId);
        if (!output || inset.viewId == 0) {
            continue;
        }
        auto display = output->displayImage();
        if (!display || !display->getImageShared() || !display->getImageViewShared()) {
            continue;
        }
        cmdBuf->transitionImageLayoutAuto(display->getImage(), EImageLayout::ShaderReadOnlyOptimal);
        auto texture = Texture::wrap(display->getImageShared(),
                                     display->getImageViewShared(),
                                     std::format("ViewDisplayInset.view{}", inset.viewId));
        _submissions.retain(input.camera.flightIndex, display);
        _submissions.retain(input.camera.flightIndex, texture);
        cmdBuf->retireResource(display);
        cmdBuf->retireResource(texture);
        insetImages.push_back(ViewDisplayInsetImage{
            .texture  = std::move(texture),
            .destRect = inset.destRect,
        });
    }
    recordCameraViewCompose(cmdBuf.get(),
                            getViewportDisplayImageShared().get(),
                            input.camera,
                            input.viewCompose,
                            insetImages);
    _presentationGraphService.recordDisplayCompose(input.camera.deltaTime,
                                                   input.displayCompose.extensions,
                                                   cmdBuf.get());

    const uint32_t flightIndex = input.camera.flightIndex;
    retainPublishedViewOutputs(flightIndex, cmdBuf.get());
    auto retain = [&](auto resource) {
        if (!resource) {
            return;
        }
        _submissions.retain(flightIndex, resource);
        cmdBuf->retireResource(resource);
    };
    retain(getViewportDisplayImageShared());
    retain(getActiveViewportImageShared());
    retain(getPostprocessOutputImageShared());

    endFrameCommandBuffer(cmdBuf.get());
    _submissions.markRecordingComplete(flightIndex);
    return cmdBuf.get();
}

bool RenderRuntime::validateSceneRenderInput(const FrameInput& input) const
{
    if (input.sceneRender.empty()) {
        return true;
    }
    if (!input.sceneRender.complete()) {
        YA_CORE_ERROR("Scene render input must provide a plan and one recording per viewport task");
        return false;
    }
    return true;
}

bool RenderRuntime::prepareFrame(const FrameInput& input, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PROFILE_FUNCTION()
    ensureViewportRectInitialized(input);
    _viewportState.setFrameBufferScale(input.camera.viewportFrameBufferScale);
    return beginFrameCommandBuffer(input, cmdBuf);
}

IRenderPipeline* RenderRuntime::getActivePipeline() const
{
    return _pipelineCoordinator.getActivePipeline();
}

uint64_t RenderRuntime::getFrameIndex() const
{
    return _clockState ? _clockState->frameIndex : 0;
}

double RenderRuntime::getElapsedTimeSeconds() const
{
    return _clockState ? static_cast<double>(_clockState->elapsedTimeMS) / 1000.0 : 0.0;
}

Scene* RenderRuntime::getActiveScene() const
{
    return _activeSceneProvider ? _activeSceneProvider() : nullptr;
}

GameplayResourceBinding* RenderRuntime::getGameplayResourceBinding() const
{
    return _gameplayResourceBinding.get();
}

EnvironmentLightingProcessor* RenderRuntime::getEnvironmentLightingProcessor() const
{
    return _environmentLightingProcessor.get();
}

bool RenderRuntime::isShadowMappingEnabled() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isShadowMappingEnabled();
    }
    return false;
}

std::shared_ptr<ImageResource> RenderRuntime::getShadowDirectionalDepthResource() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowDirectionalDepthResource();
    }
    return nullptr;
}

std::shared_ptr<ImageResource> RenderRuntime::getShadowPointFaceDepthResource(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getShadowPointFaceDepthResource(pointLightIndex, faceIndex);
    }
    return nullptr;
}

std::shared_ptr<RenderTexture> RenderRuntime::getPostprocessOutputImageShared() const
{
    if (auto* pipeline = _pipelineCoordinator.getSelectedForwardPipeline()) {
        return pipeline->getPostprocessOutputImageShared();
    }
    if (auto* pipeline = _pipelineCoordinator.getSelectedDeferredPipeline()) {
        return pipeline->getPostprocessOutputImageShared();
    }
    return nullptr;
}

std::shared_ptr<RenderTexture> RenderRuntime::getPresentationImageShared() const
{
    return _presentationGraphService.getCurrentPresentationImageShared();
}

bool RenderRuntime::isPostprocessingEnabled() const
{
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->isPostprocessingEnabled();
    }
    return false;
}

RenderPipelineDebugOutputCatalog RenderRuntime::buildPipelineDebugOutputCatalog() const
{
    RenderPipelineDebugOutputCatalog catalog{};
    auto* pipeline = getActivePipeline();
    if (!pipeline) {
        return catalog;
    }

    catalog.bShadowMappingEnabled   = pipeline->isShadowMappingEnabled();
    catalog.shadowDirectionalDepthResource = pipeline->getShadowDirectionalDepthResource();
    catalog.viewportDepthImageOwner = pipeline->getViewportDepthImageShared();
    catalog.bPostprocessingEnabled = pipeline->isPostprocessingEnabled();

    if (auto* selectedForward = _pipelineCoordinator.getSelectedForwardPipeline()) {
        catalog.viewportOutputImageOwner    = selectedForward->getViewportOutputImageShared();
        catalog.postprocessOutputImageOwner = selectedForward->getPostprocessOutputImageShared();
        catalog.bloomExtractOwner           = selectedForward->getBloomExtractImageShared();
        catalog.bloomBlurOwner              = selectedForward->getBloomBlurImageShared();
        catalog.bloomCompositeOwner         = selectedForward->getBloomCompositeImageShared();
        return catalog;
    }

    if (auto* selectedDeferred = _pipelineCoordinator.getSelectedDeferredPipeline()) {
        catalog.viewportOutputImageOwner    = selectedDeferred->getViewportOutputImageShared();
        catalog.postprocessOutputImageOwner = selectedDeferred->getPostprocessOutputImageShared();
        catalog.bloomExtractOwner           = selectedDeferred->getBloomExtractImageShared();
        catalog.bloomBlurOwner              = selectedDeferred->getBloomBlurImageShared();
        catalog.bloomCompositeOwner         = selectedDeferred->getBloomCompositeImageShared();
    }

    return catalog;
}

Extent2D RenderRuntime::getViewportExtent() const
{
    if (const auto* output = publishedViewOutput()) {
        if (output->desc.hasExtent()) {
            return output->desc.extent;
        }
        if (auto image = output->displayImage()) {
            return image->getExtent();
        }
    }
    if (_viewportState.getRect().extent.x > 0 && _viewportState.getRect().extent.y > 0) {
        return Extent2D::fromVec2(_viewportState.getRect().extent);
    }
    if (auto* pipeline = getActivePipeline()) {
        return pipeline->getViewportExtent();
    }
    return {};
}

DeferredPipelineDebugViews RenderRuntime::getDeferredPipelineDebugViews() const
{
    if (auto* pipeline = _pipelineCoordinator.getSelectedDeferredPipeline()) {
        return pipeline->buildDebugViews();
    }
    return {};
}

RenderTargetCatalog RenderRuntime::buildRenderTargetCatalog() const
{
    RenderTargetCatalog catalog{};

    if (auto presentationImage = _presentationGraphService.getCurrentPresentationImageShared()) {
        auto* swapchain = _presentationGraphService.getSwapchain();
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

void RenderRuntime::requestRenderTargetFormat(const RenderTargetFormatCommand& command)
{
    _pipelineCoordinator.requestRenderTargetFormat(command);
}

DebugRenderSystem& RenderRuntime::getDebugRenderSystem() const
{
    return DebugRenderSystem::get();
}

} // namespace ya

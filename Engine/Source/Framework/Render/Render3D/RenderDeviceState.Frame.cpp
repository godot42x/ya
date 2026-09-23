#include "Render3D/RenderDeviceState.h"

#include "Core/Log.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Core/Texture.h"
#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "Render3D/Common/ViewCompose.h"
#include "utility.cc/ranges.h"
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace ya
{

bool RenderDeviceState::beginFrameCommandBuffer(const RenderFramePlan& plan, std::shared_ptr<ICommandBuffer>& cmdBuf)
{
    YA_PROFILE_SCOPE("RenderDeviceState::beginFrameCommandBuffer");
    YA_PERF_SCOPE(perf::sample::renderPrepareFrame(), perf::metric::cpuTimeMs(), perf::domain::render());

    if (!plan.present.surface || plan.present.imageIndex < 0) {
        // Host skipped acquire (unpresentable / failed begin). One cmdBuf still
        // couples world record to present, so there is nothing to record here.
        // Camera skip is host policy, not a swapchain query inside the coordinator.
        return false;
    }

    const uint32_t flightIndex = plan.frame.flightIndex;
    if (flightIndex >= _commandBuffers.size() || !_commandBuffers[flightIndex]) {
        YA_CORE_ERROR("Recording flight {} has no command buffer", flightIndex);
        return false;
    }

    cmdBuf = _commandBuffers[flightIndex];
    cmdBuf->reset();
    cmdBuf->begin();
    if (YA_PERF_IS_ENABLED()) {
        _render->beginFrameGpuTiming(cmdBuf.get());
    }

    if (!_submissions.acquire(flightIndex, plan.frame.frameIndex, cmdBuf.get(), plan.present.surface)) {
        YA_CORE_ERROR("Recording flight {} failed to begin a live submission", flightIndex);
        return false;
    }
    if (!_viewOutputs.beginSubmission(flightIndex, plan.frame.frameIndex)) {
        YA_CORE_ERROR("Recording flight {} failed to begin view outputs", flightIndex);
        return false;
    }
    return true;
}

void RenderDeviceState::recordViewFamilies(const RenderFramePlan& plan)
{
    YA_PROFILE_FUNCTION();

    ISceneViewFamilyRenderer* pipeline = getActivePipeline();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while recording a view family");

    RenderSubmission* live = _submissions.get(plan.frame.flightIndex);
    YA_CORE_ASSERT(live && live->isRecording(), "Family record requires a recording submission");

    const auto recordOneFamily = [&](const SceneViewFamilyPlan* family, std::vector<SceneViewRecording> views) {
        ViewFamilyRecordContext ctx{
            .cmdBuf     = live->commandBuffer(),
            .frame      = &plan.frame,
            .submission = live,
            .plan       = &plan.sceneRender.plan(),
            .family     = family,
            .views      = std::move(views),
        };
        publishFamilyResult(plan.frame.flightIndex, pipeline->recordFamily(ctx));
    };

    // The plan arrives extracted, so its views hold one recording per view
    // task and the family indices always land inside them.
    const std::vector<SceneViewRecording>& views = plan.sceneRender.views();
    for (const SceneViewFamilyPlan& family : plan.sceneRender.plan().viewFamilies) {
        std::vector<SceneViewRecording> familyViews;
        familyViews.reserve(family.viewTaskIndices.size());
        for (uint32_t index : family.viewTaskIndices) {
            familyViews.push_back(views[index]);
        }
        recordOneFamily(&family, std::move(familyViews));
    }
}

void RenderDeviceState::prepareFrameRecord(const RenderFramePlan& plan)
{
    const std::vector<Scene*> scenes = renderedScenes(plan.sceneRender.plan());
    if (scenes.empty()) {
        prepareDerivedState(nullptr, plan.frame.deltaTime);
    }
    else {
        for (Scene* scene : scenes) {
            prepareDerivedState(scene, plan.frame.deltaTime);
        }
    }
    // One call for this frame's safe-point mutations: pending pipeline
    // switch/reload and the queued render-target format commands. A View's
    // geometry is not pushed here -- it is that View's own declaration and its
    // resources are keyed by it. See PipelineCoordinator::applyPendingChanges.
    _pipelineCoordinator.applyPendingChanges();
    // The other safe-point mutation, and the only one that is about a View
    // *leaving*: a View this tick does not declare is not one of the Views the
    // last recorded tick left behind, so the pipeline drops what it published
    // for it here -- before the command buffer opens. The whole plan is the
    // input, so this is one answer for the tick, not one per recorded family,
    // and a tick that declares no View at all still reconciles.
    if (IRenderPipeline* pipeline = getActivePipeline()) {
        pipeline->reconcilePublishedViews(plan.sceneRender.plan());
    }
    // Prepares the runtime UI-compose pass from the pipeline's own postprocess
    // format. There used to be a second, conditional call right below that read
    // the *previous* frame's published display image to learn the same format --
    // a stale read whose only answer this one already has.
    prepareComposePipelines();
    // Pre-record preparation: resolve each View's Scene-keyed GPU bindings now,
    // while the View's own declaration still names its Scene, so recording never
    // has to ask which Scene is current. Skipping this would leave every pass
    // with empty IBL/skybox bindings rather than an obviously wrong one.
    for (const SceneViewRecording& recording : plan.sceneRender.views()) {
        if (recording.frameData) {
            resolveViewSceneResources(recording.task ? recording.task->desc.scene : nullptr,
                                      recording.frameData->sceneResources);
        }
    }
}

RecordedFrame RenderDeviceState::record(const RenderFramePlan& plan)
{
    YA_PROFILE_SCOPE("RenderDeviceState::record");
    YA_PERF_SCOPE(perf::sample::renderRuntime(), perf::metric::cpuTimeMs(), perf::domain::render());

    // The whole record order is spelled out here, in the order it happens, and
    // the host's contribution enters through `recordExtensions` at the points
    // below. The order used to be split: the host assembled callbacks into the
    // plan and this function decided where they ran, so neither file showed the
    // sequence. Everything that mutates state or prepares GPU resources is in
    // `prepareFrameRecord` above; what follows is recording and nothing else.
    //
    //   graphics       → world graph into the display root's offscreen RT
    //   UI             → game UI onto that RT (after post, never into bloom)
    //   view compose   → View insets, then the host's View-compose stage
    //   display compose→ this surface's SurfacePresentation onto
    //                    swapchain[imageIndex],
    //                    running the host's display stages inside it. The
    //                    surface's backdrop is the host's declaration: a View
    //                    display image, or only the pass clear when the host's
    //                    content fills the surface (plan.present).
    //   capture        → the host's appendDisplayCapture, inside display
    //                    compose (automation screenshots)
    //
    // Every stage is called unconditionally; what happens at each is the
    // host's, and a host that records nothing there is a legitimate frame
    // (headless, or UI-only). The host also cannot reorder these, because the
    // plan no longer names an order.
    //
    // Acquire/present stay on the host FPresentFrame coordinator.

    // The View whose output the host view shows. It is the plan's answer
    // (V1/V2) and it also supplies the host-level geometry below: the viewport
    // rect a freshly built pipeline is sized from.
    // The present target, resolved before anything is recorded: a surface the
    // renderer has never presented through has no images and no write pass yet,
    // and building them here is the pre-record section where pipeline
    // construction already happens. The plan names the surface, so which window
    // this frame presents is the host's answer, not a primary-surface default.
    SurfacePresentation* presentation = nullptr;
    if (plan.present.surface) {
        presentation = &acquireSurfacePresentation(*plan.present.surface);
    }

    const SceneViewTask* displayRoot = plan.sceneRender.displayRootTask();
    prepareFrameRecord(plan);

    std::shared_ptr<ICommandBuffer> cmdBuf;
    if (!beginFrameCommandBuffer(plan, cmdBuf)) {
        return {};
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!plan.sceneRender.empty()) {
            recordViewFamilies(plan);
        }
    }

    // This frame's display root, resolved once from the plan and after the
    // families published their outputs. Everything below -- the UI compose
    // target, the insets, the surface backdrop -- reads this View's output. The
    // renderer does not remember it between frames: which View the host window
    // shows is the app's arrangement, and the app asks for it by id
    // (`getViewOutput(flight, viewId)`).
    const RenderViewOutput* displayOutput =
        displayRoot ? getViewOutput(plan.frame.flightIndex, displayRoot->desc.viewId) : nullptr;

    std::vector<ViewDisplayInset> composeInsets = plan.viewCompose.insets;
    if (!plan.sceneRender.empty()) {
        for (const auto& inset : viewDisplayInsetsFromPlan(plan.sceneRender.plan())) {
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
        // Insets name their own Views, so each is read by id from this frame's
        // flight -- not from "the current View".
        const RenderViewOutput* output = getViewOutput(plan.frame.flightIndex, inset.viewId);
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
        if (RenderSubmission* submission = _submissions.get(plan.frame.flightIndex)) {
            submission->retain(display);
            submission->retain(texture);
        }
        cmdBuf->retireResource(display);
        cmdBuf->retireResource(texture);
        insetImages.push_back(ViewDisplayInsetImage{
            .texture  = std::move(texture),
            .destRect = inset.destRect,
        });
    }
    // The game-UI compose lands on the display root's image, so its logical
    // viewport is that View's declared geometry rather than a host camera copy.
    const Extent2D logicalViewExtent = displayRoot ? displayRoot->output.extent : Extent2D{};
    recordCameraViewCompose(cmdBuf.get(),
                            displayOutput ? displayOutput->displayImage().get() : nullptr,
                            plan.frame.uiFrameSnapshot,
                            logicalViewExtent,
                            insetImages);
    if (plan.recordExtensions) {
        plan.recordExtensions->recordViewCompose(*cmdBuf, plan.frame.deltaTime);
    }
    if (RenderSubmission* submission = _submissions.get(plan.frame.flightIndex)) {
        if (presentation) {
            presentation->recordDisplayCompose(
                plan.present.backdrop == ESurfaceBackdrop::ViewDisplayImage ? surfaceImageFor(displayOutput)
                                                                            : FSurfaceImage{},
                *submission,
                plan.frame.deltaTime,
                plan.recordExtensions,
                cmdBuf.get());
        }
    }

    const uint32_t flightIndex = plan.frame.flightIndex;
    retainPublishedViewOutputs(flightIndex, cmdBuf.get());
    // No second keepalive pass here: retainPublishedViewOutputs already retains
    // every live View's display image, colour, depth and entity-id -- including
    // the display root's, which is what the three extra calls used to repeat.

    endFrameCommandBuffer(cmdBuf.get());
    RenderSubmission* submission = _submissions.get(flightIndex);
    if (!submission || !submission->finish()) {
        // Sealing failed, so the command buffer must not be submitted: its
        // kept resources and finish state are what the fence slot expects, and
        // an empty present still legalizes the image the host acquired.
        YA_CORE_ERROR("Recording flight {} failed to seal its submission", flightIndex);
        return {};
    }

    return RecordedFrame{
        .commandBuffer = cmdBuf.get(),
        .flightIndex   = submission->flightIndex(),
        .frameToken    = submission->frameToken(),
    };
}

void RenderDeviceState::resolveViewSceneResources(Scene* scene, RenderViewSceneResources& out)
{
    // Same calls the passes used to make through the services interface, moved
    // to the point where the View's Scene is known and recording has not begun.
    // Nothing here reads "the current Scene": the caller names the Scene.
    out.clear();
    out.environmentLighting            = _environmentLightingProcessor.get();
    out.environmentLightingResources   = _sharedResourceProvider.resolveSceneEnvironmentLightingResources(scene);
    out.skyboxDescriptorSet            = _sharedResourceProvider.getSceneSkyboxDescriptorSet(scene);
    out.environmentLightingDescriptorSet =
        _sharedResourceProvider.getSceneEnvironmentLightingDescriptorSet(scene);
}

FSurfaceImage RenderDeviceState::surfaceImageFor(const RenderViewOutput* output) const
{
    if (!output) {
        return {};
    }
    auto image = output->displayImage();
    if (!image) {
        return {};
    }

    // `displayImage()` falls back to the raw colour attachment when the finalize
    // output is absent, and that image is the renderer's linear colour rather
    // than a display image. Naming it as linear is the difference between the
    // surface reporting the mismatch and the window quietly going dark.
    if (!output->display) {
        return FSurfaceImage{.image = std::move(image), .encoding = EImageEncoding::Linear};
    }

    EImageEncoding encoding = EImageEncoding::DisplayEncoded;
    if (auto* pipeline = _pipelineCoordinator.getActivePipeline()) {
        encoding = pipeline->getDisplayImageEncoding();
    }
    return FSurfaceImage{.image = std::move(image), .encoding = encoding};
}

const RenderViewOutput* RenderDeviceState::getViewOutput(uint32_t flightIndex, SceneViewId viewId) const
{
    // The flight is the caller's answer, not this object's memory. Scanning
    // other flights would answer with a View from another frame that happens to
    // share an id -- which is exactly how a stale image gets shown as if it were
    // current.
    return _viewOutputs.find(flightIndex, viewId);
}

void RenderDeviceState::publishFamilyResult(uint32_t flightIndex, ViewFamilyRenderResult familyResult)
{
    for (RenderViewOutput& output : familyResult.views) {
        const uint64_t viewId = output.desc.viewId;
        if (viewId == 0) {
            continue;
        }
        if (!_viewOutputs.publish(flightIndex, std::move(output))) {
            YA_CORE_ERROR("Failed to publish view output for view {}", viewId);
        }
    }
}

void RenderDeviceState::retainPublishedViewOutputs(uint32_t flightIndex, ICommandBuffer* cmdBuf)
{
    auto retain = [&](auto resource) {
        if (!resource) {
            return;
        }
        if (RenderSubmission* submission = _submissions.get(flightIndex)) {
            submission->retain(resource);
        }
        cmdBuf->retireResource(resource);
    };

    const uint32_t liveCount = _viewOutputs.liveViewCount(flightIndex);
    for (uint32_t slot = 0; slot < liveCount; ++slot) {
        const RenderViewOutput* output = _viewOutputs.get(flightIndex, slot);
        if (!output) {
            continue;
        }
        retain(output->displayImage());
        retain(output->color);
        retain(output->depth);
        retain(output->entityId);
    }
}

void RenderDeviceState::endFrameCommandBuffer(ICommandBuffer* cmdBuf)
{
    YA_PROFILE_FUNCTION();

    if (YA_PERF_IS_ENABLED()) {
        YA_PROFILE_SCOPE("RenderDeviceState::endGpuTiming");
        _render->endFrameGpuTiming(cmdBuf);
    }

    {
        YA_PROFILE_SCOPE("RenderDeviceState::endCommandBuffer");
        cmdBuf->end();
    }

    if (YA_PERF_IS_ENABLED()) {
        YA_PROFILE_SCOPE("RenderDeviceState::publishGpuMetrics");
        PerfState::get().setValue(
            perf::sample::hostTick(),
            perf::metric::gpuTimeMs(),
            _render->getLastCompletedFrameGpuTimeMs(),
            perf::domain::gpu());
    }
}

} // namespace ya

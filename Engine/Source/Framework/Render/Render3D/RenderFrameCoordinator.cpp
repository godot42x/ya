#include "Render3D/RenderFrameCoordinator.h"

#include "Render3D/RenderDeviceState.h"

#include "Core/Log.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/ViewCompose.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <format>
#include <vector>

namespace ya
{

RenderFrameCoordinator::RenderFrameCoordinator(RenderDeviceState& device)
    : _device(&device)
{
}

void RenderFrameCoordinator::recordViewFamilies(
    const RenderFramePlan& plan,
    ICommandBuffer* cmdBuf)
{
    YA_PROFILE_FUNCTION();

    ISceneViewFamilyRenderer* pipeline = _device->getActivePipeline();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while recording a view family");

    RenderSubmission* live = _device->_submissions.get(plan.frame.flightIndex);
    YA_CORE_ASSERT(live && live->isRecording(), "Family record requires a recording submission");

    const auto recordOneFamily = [&](const SceneViewFamilyPlan* family, std::vector<SceneViewRecording> views) {
        ViewFamilyRecordContext ctx{
            .cmdBuf          = live->commandBuffer(),
            .frame           = &plan.frame,
            .submission      = live,
            .plan            = &plan.sceneRender.plan(),
            .family          = family,
            .views           = std::move(views),
        };
        _device->publishFamilyResult(plan.frame.flightIndex, pipeline->recordFamily(ctx));
    };

    // The plan arrives extracted, so its views hold one recording per viewport
    // task and the family indices always land inside them.
    const std::vector<SceneViewRecording>& views = plan.sceneRender.views();
    for (const SceneViewFamilyPlan& family : plan.sceneRender.plan().viewFamilies) {
        std::vector<SceneViewRecording> familyViews;
        familyViews.reserve(family.viewportTaskIndices.size());
        for (uint32_t index : family.viewportTaskIndices) {
            familyViews.push_back(views[index]);
        }
        recordOneFamily(&family, std::move(familyViews));
    }
}

RecordedFrame RenderFrameCoordinator::record(const RenderFramePlan& plan)
{
    YA_PROFILE_SCOPE("RenderFrameCoordinator::record");
    YA_PERF_SCOPE(perf::sample::renderRuntime(), perf::metric::cpuTimeMs(), perf::domain::render());

    // The whole record order is spelled out here, in the order it happens, and
    // the host's contribution enters through `recordExtensions` at the points
    // below. Before, the order was split: the host assembled callbacks into
    // the plan, and this function decided where they ran, so neither file
    // showed the sequence.
    //
    //   graphics       → world graph into the display root's offscreen RT
    //   UI             → game UI onto that RT (after post, never into bloom)
    //   view compose   → View insets, then the host's View-compose stage
    //   display compose→ PresentationGraphService onto swapchain[imageIndex],
    //                    running the host's display stages inside it
    //   capture        → the host's appendDisplayCapture, inside display
    //                    compose (automation screenshots)
    //
    // Every stage is called unconditionally; what happens at each is the
    // host's, and a host that records nothing there is a legitimate frame
    // (headless, or UI-only). The host also cannot reorder these, because the
    // plan no longer names an order.
    //
    // Acquire/present stay on the host FPresentFrame coordinator.

    // The View whose output the host viewport shows. It is the plan's answer
    // (V1/V2) and it also supplies the host-level geometry below: the viewport
    // rect a freshly built pipeline is sized from.
    const SceneViewportTask* displayRoot = plan.sceneRender.displayRootTask();

    const std::vector<Scene*> scenes = renderedScenes(plan.sceneRender.plan());
    if (scenes.empty()) {
        _device->prepareDerivedState(nullptr, plan.frame.deltaTime);
    }
    else {
        for (Scene* scene : scenes) {
            _device->prepareDerivedState(scene, plan.frame.deltaTime);
        }
    }
    _device->applyPendingMutations();
    _device->applyViewportResize(displayRoot ? displayRoot->desc.viewportRect : Rect2D{});
    _device->prepareComposePipelines();
    // Pre-record preparation: resolve each View's Scene-keyed GPU bindings now,
    // while the View's own declaration still names its Scene, so recording never
    // has to ask which Scene is current. Skipping this would leave every pass
    // with empty IBL/skybox bindings rather than an obviously wrong one.
    for (const SceneViewRecording& recording : plan.sceneRender.views()) {
        if (recording.frameData) {
            _device->resolveViewSceneResources(recording.task ? recording.task->desc.scene : nullptr,
                                               recording.frameData->sceneResources);
        }
    }
    if (plan.frame.uiFrameSnapshot) {
        if (auto uiTarget = _device->getViewportDisplayImageShared()) {
            prepareRender2DComposePassPipeline(
                FRender2DComposePassDesc{
                    .kind = ERender2DComposePassKind::RuntimeUIComposite,
                },
                uiTarget->getFormat());
        }
    }

    std::shared_ptr<ICommandBuffer> cmdBuf;
    if (!_device->beginFrameCommandBuffer(plan, cmdBuf)) {
        return {};
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!plan.sceneRender.empty()) {
            recordViewFamilies(plan, cmdBuf.get());
        }
        // Everything below -- the compose insets, the display target, the
        // host's later reads -- resolves through this one identity, so it is set
        // before any of them, and a tick with no display root clears it.
        _device->publishViewOutputIdentity(plan.frame.flightIndex,
                                           displayRoot ? displayRoot->desc.viewId : 0);
    }

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
        const RenderViewOutput* output = _device->getViewOutput(inset.viewId);
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
        if (RenderSubmission* submission = _device->_submissions.get(plan.frame.flightIndex)) {
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
    const Extent2D logicalViewportExtent = displayRoot ? displayRoot->output.extent : Extent2D{};
    recordCameraViewCompose(cmdBuf.get(),
                            _device->getViewportDisplayImageShared().get(),
                            plan.frame.uiFrameSnapshot,
                            logicalViewportExtent,
                            insetImages);
    if (plan.recordExtensions) {
        plan.recordExtensions->recordViewCompose(*cmdBuf, plan.frame.deltaTime);
    }
    _device->_presentationGraphService.recordDisplayCompose(plan.frame.deltaTime,
                                                            plan.recordExtensions,
                                                            cmdBuf.get());

    const uint32_t flightIndex = plan.frame.flightIndex;
    _device->retainPublishedViewOutputs(flightIndex, cmdBuf.get());
    auto retain = [&](auto resource) {
        if (!resource) {
            return;
        }
        if (RenderSubmission* submission = _device->_submissions.get(flightIndex)) {
            submission->retain(resource);
        }
        cmdBuf->retireResource(resource);
    };
    retain(_device->getViewportDisplayImageShared());
    retain(_device->getActiveViewportImageShared());
    retain(_device->getPostprocessOutputImageShared());

    _device->endFrameCommandBuffer(cmdBuf.get());
    RenderSubmission* submission = _device->_submissions.get(flightIndex);
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

} // namespace ya

#include "RenderFrameCoordinator.h"

#include "RenderDeviceState.h"

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

namespace
{

std::shared_ptr<RenderViewportOverlaySnapshot> buildViewportOverlaySnapshot(const CameraFrameInput::OverlayInput& overlay)
{
    auto snapshot = std::make_shared<RenderViewportOverlaySnapshot>();
    if (overlay.screenSprites) {
        snapshot->screenSprites = *overlay.screenSprites;
    }
    if (overlay.worldSprites) {
        snapshot->worldSprites = *overlay.worldSprites;
    }
    if (overlay.screenTexts) {
        snapshot->screenTexts = *overlay.screenTexts;
    }
    if (overlay.worldLines) {
        snapshot->worldLines = *overlay.worldLines;
    }
    return snapshot->empty() ? nullptr : snapshot;
}

} // namespace

RenderFrameCoordinator::RenderFrameCoordinator(RenderDeviceState& device)
    : _device(&device)
{
}

void RenderFrameCoordinator::recordViewFamilies(
    const RenderFramePlan& plan,
    ICommandBuffer* cmdBuf,
    std::shared_ptr<RenderViewportOverlaySnapshot> overlaySnapshot)
{
    YA_PROFILE_FUNCTION();

    ISceneViewFamilyRenderer* pipeline = _device->getActivePipeline();
    YA_CORE_ASSERT(pipeline, "Active render pipeline is null while recording a view family");

    RenderSubmission* live = _device->_submissions.get(plan.camera.flightIndex);
    YA_CORE_ASSERT(live && live->isRecording(), "Family record requires a recording submission");

    if (overlaySnapshot) {
        live->retain(overlaySnapshot);
        cmdBuf->retireResource(overlaySnapshot);
    }

    const auto recordOneFamily = [&](const SceneViewFamilyPlan* family, std::vector<SceneViewRecording> views) {
        ViewFamilyRecordContext ctx{
            .cmdBuf          = live->commandBuffer(),
            .hostCamera      = plan.camera,
            .submission      = live,
            .plan            = &plan.sceneRender.plan(),
            .family          = family,
            .views           = std::move(views),
            .overlaySnapshot = overlaySnapshot,
        };
        _device->publishFamilyResult(plan.camera.flightIndex, pipeline->recordFamily(ctx));
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

ICommandBuffer* RenderFrameCoordinator::record(const RenderFramePlan& plan)
{
    YA_PROFILE_SCOPE("RenderFrameCoordinator::record");
    YA_PERF_SCOPE(perf::sample::renderRuntime(), perf::metric::cpuTimeMs(), perf::domain::render());

    // graphics → world graph into this camera's offscreen RT
    // UI → game UI onto that RT (after post, never into bloom)
    // view compose → editor overlays onto that RT
    // display compose → PresentationGraphService onto swapchain[imageIndex]
    // Acquire/present stay on the host FPresentFrame coordinator.

    const std::vector<Scene*> scenes = renderedScenes(plan.sceneRender.plan());
    if (scenes.empty()) {
        _device->prepareDerivedState(nullptr, plan.camera.deltaTime);
    }
    else {
        for (Scene* scene : scenes) {
            _device->prepareDerivedState(scene, plan.camera.deltaTime);
        }
    }
    _device->applyPendingMutations();
    _device->applyViewportResize(plan.camera.viewportRect);
    _device->prepareComposePipelines();
    if (plan.camera.uiFrameSnapshot) {
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
        return nullptr;
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!plan.sceneRender.empty()) {
            auto overlaySnapshot = buildViewportOverlaySnapshot(plan.camera.overlay);
            recordViewFamilies(plan, cmdBuf.get(), overlaySnapshot);
            if (const SceneViewportTask* displayRoot = plan.sceneRender.primaryTask()) {
                if (displayRoot->desc.viewId != 0) {
                    _device->_publishedOutputViewId = displayRoot->desc.viewId;
                    _device->_publishedOutputFlight = plan.camera.flightIndex;
                }
            }
        }
        else {
            _device->clearPublishedViewOutputs();
        }
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
        if (RenderSubmission* submission = _device->_submissions.get(plan.camera.flightIndex)) {
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
    recordCameraViewCompose(cmdBuf.get(),
                            _device->getViewportDisplayImageShared().get(),
                            plan.camera,
                            plan.viewCompose,
                            insetImages);
    _device->_presentationGraphService.recordDisplayCompose(plan.camera.deltaTime,
                                                            plan.displayCompose.extensions,
                                                            cmdBuf.get());

    const uint32_t flightIndex = plan.camera.flightIndex;
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
    if (RenderSubmission* submission = _device->_submissions.get(flightIndex)) {
        if (!submission->finish()) {
            YA_CORE_ERROR("Recording flight {} failed to finish submission", flightIndex);
        }
    }
    return cmdBuf.get();
}

} // namespace ya

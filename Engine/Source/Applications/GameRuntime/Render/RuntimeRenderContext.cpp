#include "GameRuntime/Render/RuntimeRenderContext.h"

#include "Core/Log.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "Render3D/Common/ViewCompose.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/Services/SurfacePresentation.h"

#include <algorithm>
#include <format>
#include <memory>
#include <vector>

namespace ya
{

RecordedFrame RuntimeRenderContext::record(const RenderFramePlan& plan)
{
    YA_PROFILE_SCOPE("RuntimeRenderContext::record");
    YA_PERF_SCOPE(perf::sample::renderRuntime(), perf::metric::cpuTimeMs(), perf::domain::render());

    YA_CORE_ASSERT(_device, "RuntimeRenderContext::record without a render device");

    const uint32_t flightIndex = plan.frame.flightIndex;

    // The present target, resolved before anything is recorded: a surface this
    // renderer has never presented through has no images and no write pass yet,
    // and building them is the pre-record section where pipeline construction
    // already happens. The plan names the surface, so which window this frame
    // presents is the host's answer, not a primary-surface default. A plan with
    // no present target is a legal frame -- offscreen View work records and
    // publishes exactly the same way, only the surface compose below is skipped.
    SurfacePresentation* presentation = nullptr;
    if (plan.present.surface) {
        presentation = &_device->acquireSurfacePresentation(plan.present.surfaceId, *plan.present.surface);
    }

    // The View whose output the host window shows. It is the plan's answer, and
    // below it also supplies the host-level geometry: the logical viewport the
    // game UI composes against.
    const SceneViewTask* displayRoot = plan.sceneRender.displayRootTask();

    _device->prepareFrameRecord(plan);

    std::shared_ptr<ICommandBuffer> cmdBuf;
    if (!_device->beginFrameCommandBuffer(plan, cmdBuf)) {
        return {};
    }

    {
        YA_PERF_SCOPE(perf::sample::renderWorld(), perf::metric::cpuTimeMs(), perf::domain::render());
        if (!plan.sceneRender.empty()) {
            _device->recordViewFamilies(plan);
        }
    }

    // This frame's display root, resolved once from the plan and after the
    // families published their outputs. Everything below -- the UI compose
    // target, the insets, the surface backdrop -- reads this View's output, and
    // the application asks for it by id rather than remembering it: the renderer
    // publishes every View and names none of them "the current one".
    const RenderViewOutput* displayOutput =
        displayRoot ? _device->getViewOutput(flightIndex, displayRoot->desc.viewId) : nullptr;

    // The insets are this host's arrangement: what it declared by hand, plus one
    // per View the plan composed onto the display root. Deduplicated by View,
    // because a View is either declared once or not at all.
    std::vector<ViewDisplayInset> composeInsets = plan.viewCompose.insets;
    if (!plan.sceneRender.empty()) {
        for (const ViewDisplayInset& inset : viewDisplayInsetsFromPlan(plan.sceneRender.plan())) {
            const bool bDeclared = std::any_of(composeInsets.begin(),
                                               composeInsets.end(),
                                               [&inset](const ViewDisplayInset& existing)
                                               { return existing.viewId == inset.viewId; });
            if (!bDeclared) {
                composeInsets.push_back(inset);
            }
        }
    }

    RenderSubmission* submission = _device->getLiveSubmission(flightIndex);

    std::vector<ViewDisplayInsetImage> insetImages;
    insetImages.reserve(composeInsets.size());
    for (const ViewDisplayInset& inset : composeInsets) {
        // An inset names its own View, so it is read by id from this frame's
        // flight -- not from "the current View".
        const RenderViewOutput* output = _device->getViewOutput(flightIndex, inset.viewId);
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
        // The wrapper and the image it lifts are this recording's only owners:
        // the pass reads them until the queue submit, so the flight keeps them.
        if (submission) {
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

    if (submission && presentation) {
        // What the window starts from is this host's declaration: the display
        // root's image, or only the pass clear when the host's own content fills
        // the surface (the editor's chrome). The image written is the plan's
        // acquired token -- the same answer the submission's sync pair came
        // from -- never a swapchain query at record time.
        presentation->recordDisplayCompose(
            plan.present.backdrop == ESurfaceBackdrop::ViewDisplayImage ? _device->surfaceImageFor(displayOutput)
                                                                        : FSurfaceImage{},
            *submission,
            plan.frame.deltaTime,
            plan.recordExtensions,
            cmdBuf.get(),
            plan.present.imageIndex);
    }

    _device->endFrameCommandBuffer(cmdBuf.get());
    return _device->sealFrame(flightIndex, cmdBuf.get());
}

} // namespace ya

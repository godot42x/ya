#include "GameRuntime/Render/RuntimeRenderContext.h"

#include "GameRuntime/App.h"
#include "GameRuntime/AppRenderState.h"
#include "GameRuntime/Render/HostSceneExtract.h"
#include "GameRuntime/Render/RenderFrameExtractor.h"

#include "Core/Async/TaskQueue.h"
#include "Core/Log.h"
#include "Core/Math/Math.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "RHI/Render.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "Render3D/Common/FrameRecordExtensions.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/Services/RenderDiagnosticsService.h"
#include "Render3D/Services/SurfacePresentation.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

#include <algorithm>
#include <format>
#include <memory>
#include <vector>

namespace ya
{

RecordedFrame RuntimeRenderContext::record(const RenderFramePlan& plan,
                                           IFrameRecordExtensions* extensions,
                                           const UIFrameSnapshot* uiSnapshot)
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

    // The runtime's own UI compose pass, prepared before recording begins: the
    // packet that goes onto the display RT after the world graph. This is a
    // host call now -- the editor's compose kinds are the editor's pipelines
    // and it prepares them itself, so the renderer carries no GUI headers for
    // either. The format is the active strategy's postprocess output, asked
    // after `prepareFrameRecord` applied any pending pipeline switch.
    prepareRender2DComposePassPipeline(
        FRender2DComposePassDesc{
            .kind = ERender2DComposePassKind::RuntimeUIComposite,
        },
        _device->getPostprocessColorFormat());

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
    // target, the surface backdrop -- reads this View's output, and
    // the application asks for it by id rather than remembering it: the renderer
    // publishes every View and names none of them "the current one".
    const RenderViewOutput* displayOutput =
        displayRoot ? _device->getViewOutput(flightIndex, displayRoot->desc.viewId) : nullptr;

    RenderSubmission* submission = _device->getLiveSubmission(flightIndex);

    // The game-UI compose lands on the display root's image, so its logical
    // viewport is that View's declared geometry rather than a host camera copy.
    const Extent2D logicalViewExtent = displayRoot ? displayRoot->output.extent : Extent2D{};

    // Game UI compose onto the display root's image, inside the app's record
    // order (after the world graph, before display compose): Render3D only
    // publishes View outputs and never learns the UI snapshot exists. The
    // logical viewport is that View's declared geometry rather than a host
    // camera copy. Skipped when the world image isn't built yet (first frame)
    // or the tick has no Game UI.
    if (displayOutput && uiSnapshot) {
        recordRender2DComposePass(cmdBuf.get(),
                                  *displayOutput->displayImage(),
                                  nullptr,
                                  uiSnapshot,
                                  FRender2DComposePassDesc{
                                      .kind = ERender2DComposePassKind::RuntimeUIComposite,
                                      .logicalExtent = logicalViewExtent,
                                  });
    }
    if (extensions) {
        extensions->recordViewCompose(*cmdBuf, plan.frame.deltaTime);
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
            extensions,
            cmdBuf.get(),
            plan.present.imageIndex);
    }

    _device->endFrameCommandBuffer(cmdBuf.get());
    return _device->sealFrame(flightIndex, cmdBuf.get());
}

void RuntimeRenderContext::tick(App& app, float dt)
{
    YA_PROFILE_SCOPE("Render/Frame");
    // The context is created with the device and destroyed before it, so this
    // is the same question as "is there a renderer this tick" -- and the
    // orchestrator has already answered it before calling.
    YA_CORE_ASSERT(_device, "RuntimeRenderContext::tick without a render device");
    RenderDeviceState* device = _device;

    auto& diagnostics = device->getDiagnosticsService();
    diagnostics.onFrameBegin();

    struct DiagnosticsGuard
    {
        RenderDiagnosticsService* diagnostics = nullptr;

        ~DiagnosticsGuard()
        {
            if (diagnostics) {
                diagnostics->onFrameEnd();
            }
        }
    } diagnosticsGuard{.diagnostics = &diagnostics};

    // The one call that makes the previous paragraph true. It used to be
    // inlined here while `pumpOffscreenTasks` called only itself, so the named
    // step existed in the header and as an infinite recursion, and the real
    // call was anonymous. One step, one body, one call site.
    pumpOffscreenTasks(app);

    const uint32_t flightIndex = resolveFlightIndex(app);

    // This tick's declaration collector and the plan sealed from it. It is a
    // local, not frame state: `beginTick -> submit -> seal` describes one
    // tick's arrangement, so its lifetime is this scope, and a scheduler that
    // outlived the tick could only hold declarations the tick already resolved.
    SceneRenderScheduler sceneScheduler;
    sceneScheduler.beginTick(App::_hostTick);

    declareViews(app, dt, sceneScheduler);
    ExtractedSceneRender sceneRender = extractScenes(app, sceneScheduler);
    prepareViews(app, sceneRender);

    TickFrame gameFrame = buildGameRenderFrame(app, dt, flightIndex, sceneRender);
    adoptDisplayedView(app, sceneRender, flightIndex);

    IRender*      render       = device->getRender();
    FPresentFrame presentFrame{.surface = app.getRenderServices().getHostSurface()};

    // Frame bookkeeping first, acquire second. The wait that retires the
    // previous frame's GPU work (and frees the flight slot, the transient
    // buffers and this window's acquire semaphore for reuse) is the DEVICE's,
    // and it has to happen before this frame starts acquiring images -- the
    // acquire reuses the semaphore the previous frame's submission waited on.
    // It runs on every frame the app reaches this point, including one whose
    // surface is unpresentable (minimized) or whose acquire fails: that is
    // exactly the frame where nothing else would retire the resources this
    // tick drops.
    if (render) {
        render->beginRecordedFrame();
    }

    bool bAcquireAttempted = false;
    {
        YA_PERF_SCOPE(perf::sample::renderBegin(), perf::metric::cpuTimeMs(), perf::domain::render());
        bAcquireAttempted = acquirePresentFrame(presentFrame);
    }
    // Host policy, not a renderer rule: when this window shows nothing (no
    // surface, or acquire refused), no consumer reads its Views this frame, so
    // that recording is skipped. Other windows of this tick still record into
    // the same submission. The renderer itself records a present-less plan fine.
    FFrameSubmission submission;
    // Lives through the submit below: the command-buffer handle it names is
    // what that submission queues.
    RecordedFrame recorded;
    if (bAcquireAttempted && presentFrame.acquired()) {
        recorded = recordFrame(app, dt, std::move(sceneRender), gameFrame, presentFrame);
        std::vector<void*> commands;
        if (recorded.valid()) {
            commands.push_back(recorded.commandBuffer->getHandle());
        }
        (void)submission.add(presentFrame, std::move(commands));
    }
    else if (bAcquireAttempted) {
        (void)submission.add(presentFrame, {});
    }

    // Extra windows acquire and record here, before the one submit, so their
    // sync pairs join the host's instead of starting a later submission.
    app.recordModuleExtraSurfaces(dt, submission);
    if (render) {
        YA_PERF_SCOPE(perf::sample::renderSubmit(), perf::metric::cpuTimeMs(), perf::domain::render());
        (void)submission.submitAndPresent(*render);
    }
}

/// The app's arrangement for this frame: which View it displays, in which
/// flight its output is published, and the camera it renders from. Written
/// once, from the plan -- before the recording below reads it, because the
/// editor's compose and chrome stages run *inside* the recording. The renderer
/// never learns it: it publishes every View and names none of them "the
/// current one". Which declaration is the displayed one comes from the same
/// structural predicate the plan uses for its display root, so a well-known
/// view id here would be a second definition that can disagree with it.
void RuntimeRenderContext::adoptDisplayedView(App&                    app,
                                              const ExtractedSceneRender& sceneRender,
                                              uint32_t                    flightIndex)
{
    const SceneViewTask* displayRoot = sceneRender.displayRootTask();
    app._renderState->displayedView = DisplayedView{
        .viewId      = displayRoot ? displayRoot->desc.viewId : 0,
        .flightIndex = flightIndex,
        .view        = displayRoot ? displayRoot->desc.view : glm::mat4(1.0f),
        .projection  = displayRoot ? displayRoot->desc.projection : glm::mat4(1.0f),
        .cameraPos   = displayRoot ? displayRoot->desc.cameraPos : glm::vec3(0.0f),
    };
}

void RuntimeRenderContext::pumpOffscreenTasks(App& app)
{
    YA_PROFILE_SCOPE("Render/PumpOffscreenTasks");
    // Pre-record prerequisite. Named as a step because it is one: the derived
    // resources this tick's Views bind were produced by offscreen jobs queued on
    // earlier ticks, and reading them before their fence has been waited on is a
    // use-before-ready, not a slower frame.
    _device->getOffscreenTaskService().tick(app.getTaskManager());
}

uint32_t RuntimeRenderContext::resolveFlightIndex(App& app) const
{
    auto* render = app.getRenderServices().getRender();
    if (!render) {
        return 0;
    }

    // Which slot of the DEVICE's per-frame ring this recording may use: a pure
    // function of the device frame ordinal. The ring size is the device's
    // (`framesInFlight`); `MAX_FLIGHTS_IN_FLIGHT` is only that ring's table
    // capacity, asserted to fit in RenderDefines.h -- there is no second ring
    // to stay in sync with. Today the device keeps one frame in flight, so this
    // is constant 0: a ring of size 1, not a bug. Per-surface rings (acquire
    // slots) and swapchain image indices live on the surfaces and never enter
    // this function.
    return static_cast<uint32_t>(render->recordedFrameIndex() % render->framesInFlight());
}

void RuntimeRenderContext::declareViews(App& app, float dt, SceneRenderScheduler& scheduler)
{
    const HostRenderSettings& hostSettings = app._renderState->hostSettings;

    // Declare this tick's views. Every owner declares its own (the game
    // viewport, the editor's authoring viewport, the camera preview), so a view
    // exists exactly because somebody asked for it: no layer has to flip a
    // global switch to hide a view another layer declared.
    SceneViewCollector collector;
    auto* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    const SceneViewCollectContext collectContext{
        .activeScene      = scene,
        .renderResolution = hostSettings.renderResolution,
        .hostTick         = App::_hostTick,
        .deltaTime        = dt,
    };
    for (ISceneViewProducer* producer : app._renderState->viewProducers) {
        if (producer) {
            producer->collectSceneViews(collectContext, collector);
        }
    }

    // submit() stores a copy, so from here on the declarations live in the
    // scheduler's frame and this collector has no readers.
    for (const SceneViewDesc& view : collector.views()) {
        (void)scheduler.submit(view);
    }

    // This step declares and submits, and adopts nothing: which of these Views
    // this app displays, and its camera, is read off the sealed plan once in
    // tick (see the displayedView assignment there). Adopting it here meant
    // two writers for one fact, and a reader had to know which of them had run.
}

ExtractedSceneRender RuntimeRenderContext::extractScenes(App& app, SceneRenderScheduler& scheduler)
{
    // Extraction is its own step: seal() only grouped the declarations, so
    // Scene/ECS content is read here and nowhere earlier.
    ExtractedSceneRender sceneRender =
        extractHostSceneSnapshots(scheduler.seal(), _device->getTerrainProcessor());

    // Animation policy input: poses are consumed by the world pipeline, so the
    // honest question is "did the renderer produce content for this Scene", not
    // "is some viewport's world switch on". One tick of lag by construction --
    // systems run before views are declared.
    app._renderState->renderedScenesLastTick = renderedScenes(sceneRender.plan());
    return sceneRender;
}

void RuntimeRenderContext::prepareViews(App& app, ExtractedSceneRender& sceneRender)
{
    sceneRender.pairViewFrames();
    if (sceneRender.empty()) {
        return;
    }

    YA_PERF_SCOPE(perf::sample::renderExtract(), perf::metric::cpuTimeMs(), perf::domain::render());
    YA_PROFILE_SCOPE("RenderFrameExtractor::sceneSnapshot");
    for (const SceneViewRecording& recording : sceneRender.views()) {
        const SceneViewTask& task      = *recording.task;
        const SceneViewDesc&     desc      = task.desc;
        RenderFrameData&         frameData = *recording.frameData;
        RenderFrameExtractor::prepareView(
            RenderFrameExtractor::ViewPrepareInput{
                .view = desc.view,
                .projection = desc.projection,
                .viewProjection = desc.viewProjection(),
                .cameraPos = desc.cameraPos,
                .viewExtent = Extent2D::fromVec2(desc.outputRect.extent),
                .viewOwner = desc.viewOwner,
                .viewFeatures = desc.features,
                .shadowSettings = &app.getRenderServices().getShadowSettings(),
            },
            sceneRender.snapshotFor(task),
            frameData);
    }
}

RuntimeRenderContext::TickFrame RuntimeRenderContext::buildGameRenderFrame(
    App&                        app,
    float                       dt,
    uint32_t                    flightIndex,
    const ExtractedSceneRender& sceneRender)
{
    const HostRenderSettings& hostSettings = app._renderState->hostSettings;

    TickFrame tickFrame;
    // Frame-level only: which cameras draw and where their outputs go is on
    // each View's declaration and prepared data. The host contributes the
    // tick's clock and its render scale.
    tickFrame.frame = FramePacket{
        .flightIndex   = flightIndex,
        // The app's clock, not the device's frame ordinal: the UBO `frameIdx`
        // and animations run on the product tick axis.
        .hostTick      = App::_hostTick,
        .deltaTime     = dt,
        // The shader-facing frame UBO's `time`, one answer per frame -- it used
        // to be copied onto every View's prepared data as well.
        .elapsedTimeSeconds = hostSettings.clock.elapsedTimeMS / 1000.0f,
        .renderScale = hostSettings.renderScale,
        .shadowSettings = &app.getRenderServices().getShadowSettings(),
    };

    // Game UI: build the immutable frame snapshot BEFORE the RenderGraph.
    // Command recording consumes only this packet; the live WidgetTree is
    // never touched while recording. Runtime/simulation only (standalone game
    // and PIE); the editor's 3D authoring viewport has no game UI.
    //
    // The UI is composed onto the displayed View, so its logical viewport
    // is that View's declared rect. Deriving it from the plan rather than from a
    // host copy is what makes PIE correct: there the displayed View is the
    // editor's authoring panel, not the game's render resolution. A tick that
    // declares no displayed View presents no Game UI, so the tree keeps the size
    // it was last presented at instead of being resized to a target that does
    // not exist.
    const Scene* scene = app._sceneManager ? app._sceneManager->getActiveScene() : nullptr;
    const SceneViewTask* displayRoot = sceneRender.displayRootTask();
    if ((app.isRuntimeMode() || app.isSimulationMode()) && scene && displayRoot) {
        if (auto* gameUIHost = app.getGameUIHost()) {
            gameUIHost->setPresentation(displayRoot->desc.outputRect,
                                        glm::vec2(tickFrame.frame.renderScale));
            // Advance the tree, then freeze the result into this frame's packet.
            // Paired deliberately: a tree the host presents must be ticked, and
            // buildSnapshot only lays out and paints, so a host that skipped
            // this would show a frozen first frame forever. Sitting on the
            // render side (not the logic side, which pause gates) is what keeps
            // a pause menu alive -- paused frames still present.
            //
            // Both clocks are named here because this is the only place that
            // knows the pause decision: a paused frame still renders `dt` of
            // wall time, but the game advanced by none of it. The host picks
            // which one its tree follows (`updateClock`), so a HUD on GameTime
            // freezes with the game while a pause menu on RealTime keeps
            // animating, without either call site guessing.
            const FUIFrameClock uiClock{
                .gameDelta = app.isPaused() ? 0.0f : dt,
                .realDelta = dt,
            };
            gameUIHost->update(uiClock);
            tickFrame.uiSnapshot = gameUIHost->buildSnapshot();
        }
    }
    return tickFrame;
}

RecordedFrame RuntimeRenderContext::recordFrame(App&                 app,
                                                float                dt,
                                                ExtractedSceneRender sceneRender,
                                                TickFrame&           frame,
                                                const FPresentFrame& presentFrame)
{
    (void)dt;
    // The recording order is the application's and lives in this context; this
    // step only states this frame's facts, so the sequence stays readable in
    // one place instead of being assembled here and re-decided there. The app
    // is the record-stage contributor, passed as an explicit call argument --
    // the plan stays values.
    return record(RenderFramePlan{
        .sceneRender = std::move(sceneRender),
        .frame = frame.boundFrame(),
        .present = {
            .surface    = presentFrame.surface,
            // The app's own binding, carried so the renderer files this frame's
            // present target under the window's identity rather than its
            // address (see PresentFrameInput::surfaceId).
            .surfaceId  = app.getRenderServices().getHostSurfaceId(),
            .imageIndex = presentFrame.imageIndex,
            // What the window shows is a host fact, so the host declares it
            // rather than leaving the surface pass to infer it from a View's
            // compose structure. The module that fills the surface (the editor
            // chrome) is what answers; a standalone runtime has none, so the
            // surface is the View.
            // The surface is non-null here as a matter of app policy: this
            // app records only for an acquired frame, because nothing consumes
            // its Views when its one window is unpresentable. The renderer no
            // longer imposes that -- an offscreen-only plan records fine.
            .backdrop = app.presentsViewDisplayImage(*presentFrame.surface) ? ESurfaceBackdrop::ViewDisplayImage
                                                                            : ESurfaceBackdrop::HostContent,
        },
    },
    /* extensions = */ &app,
    &frame.uiSnapshot);
}

} // namespace ya

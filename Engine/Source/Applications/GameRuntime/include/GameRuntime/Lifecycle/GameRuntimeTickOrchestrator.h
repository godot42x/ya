#pragma once

#include "RHI/Render.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RecordedFrame.h"

namespace ya
{

struct App;
struct Entity;
struct RenderDeviceState;
struct RenderFrameCoordinator;
class SceneRenderScheduler;
struct FramePacket;
struct FPresentFrame;
struct SceneViewDesc;
struct UIFrameSnapshot;
struct RenderFrameData;
class SceneViewCollector;
class ExtractedSceneRender;

class GameRuntimeTickOrchestrator
{
  public:
    /// Run one product frame. Direct callers retain the legacy native event
    /// pump; AppKernel-backed run() passes false because its event source has
    /// already delivered the events for this frame.
    static int      iterate(App& app, float dt);

  private:
    static void     tickLogic(App& app, float dt);
    static void     prepareHostViewState(App& app, float dt);
    static void     tickRender(App& app, float dt);
    /// Pre-record prerequisite, before anything reads a derived resource: waits
    /// for the previous tick's offscreen submission and finalizes it, then
    /// records and submits whatever earlier ticks queued (IBL preprocess,
    /// cubemap conversions, terrain rebuilds). A job submitted here becomes
    /// readable on the next pump, which is why this runs before this tick's View
    /// preparation instead of after it.
    static void     pumpOffscreenTasks(App& app, RenderDeviceState* device);
    static uint32_t resolveFlightIndex(const App& app);

    /// The steps tickRender runs, in order. Each one is a named phase of the
    /// product frame, so the sequence is readable without following every
    /// branch of a single long function.
    ///
    /// Declares this tick's views from every registered producer into
    /// `collector` (which the caller owns, because the declarations it hands
    /// back are referenced later in the tick), and adopts the host viewport
    /// View's camera and rect into the host view state. Returns that
    /// declaration, or null when nobody declared a View for the host viewport.
    static const SceneViewDesc* declareViews(App&                     app,
                                            float                    dt,
                                            RenderDeviceState*       device,
                                            SceneViewCollector&      collector);
    /// Groups the declarations and extracts Scene content for them. Grouping
    /// and extraction stay separate: seal() reads no ECS, this step does.
    static ExtractedSceneRender extractScenes(App& app, SceneRenderScheduler& scheduler, RenderDeviceState* device);
    /// Pairs this tick's View recordings with their frame data and prepares one
    /// View's camera-dependent packet each.
    static void prepareViews(App& app, float dt, uint32_t flightIndex, ExtractedSceneRender& sceneRender);
    /// Builds the camera packet the pipelines record from, plus the UI snapshot
    /// they must consume instead of the live WidgetTree (caller-owned storage,
    /// because the packet points into it). `hostFrameData` is the host viewport
    /// View's preparation from this tick's plan, or null when the tick declared
    /// no such View.
    static FramePacket buildGameRenderFrame(App&                                       app,
                                            float                                      dt,
                                            uint32_t                                   flightIndex,
                                            UIFrameSnapshot&                           outUiSnapshot);
    /// Records the tick in one renderer call and returns what the host submits.
    static RecordedFrame recordFrame(App&                        app,
                                     RenderFrameCoordinator&     coordinator,
                                     float                       dt,
                                     ExtractedSceneRender        sceneRender,
                                     const FramePacket&          frame,
                                     const FPresentFrame&        presentFrame);
    /// Submits the recording (or an empty frame) and presents the surface.
    static void submitRecordedFrame(App& app, FPresentFrame& presentFrame, const RecordedFrame& recorded);
};

} // namespace ya

#pragma once

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "RHI/Render.h"
#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RecordedFrame.h"

namespace ya
{

struct App;
struct Entity;
struct RenderDeviceState;
class RuntimeRenderContext;
class SceneRenderScheduler;
struct FPresentFrame;
struct RenderFrameData;
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
    /// Declares this tick's views from every registered producer, submits each
    /// declaration to `scheduler` (which stores a copy of it), and adopts the
    /// host viewport View's camera and rect into the host view state. The
    /// collector is this step's own storage: nothing downstream refers to it, so
    /// no caller has to keep it alive.
    /// The scheduler is a parameter because it is the tick's own arrangement:
    /// `tickRender` owns it for the duration of one tick, and this step is one
    /// of the two steps that write into it.
    static void declareViews(App& app, float dt, SceneRenderScheduler& scheduler);
    /// Groups the declarations and extracts Scene content for them. Grouping
    /// and extraction stay separate: seal() reads no ECS, this step does.
    static ExtractedSceneRender extractScenes(App& app, SceneRenderScheduler& scheduler, RenderDeviceState* device);
    /// Pairs this tick's View recordings with their frame data and prepares one
    /// View's camera-dependent packet each.
    static void prepareViews(App& app, float dt, ExtractedSceneRender& sceneRender);
    /// One tick's frame facts together with the UI snapshot the packet borrows.
    /// They are built as one value because the packet points into the snapshot:
    /// returning them separately made the caller declare a local before the call
    /// and keep it alive by convention.
    struct TickFrame
    {
        UIFrameSnapshot uiSnapshot{};
        FramePacket     frame{};

        /// The packet with `uiFrameSnapshot` bound to this value's snapshot.
        /// Bound on read instead of stored, because this value is returned by
        /// value and a stored pointer would be left behind on the moved-from
        /// object.
        [[nodiscard]] const FramePacket& boundFrame()
        {
            frame.uiFrameSnapshot = &uiSnapshot;
            return frame;
        }
    };

    /// Builds the frame facts the pipelines record from, plus the UI snapshot
    /// they consume instead of the live WidgetTree. The snapshot stays empty on
    /// frames with no Game UI (UI-only frames and the editor's authoring
    /// viewport), which is what the packet reports as "no UI to compose".
    /// `sceneRender` supplies the View the UI will be composed onto, which is
    /// what sizes its logical viewport.
    static TickFrame buildGameRenderFrame(App&                        app,
                                          float                       dt,
                                          uint32_t                    flightIndex,
                                          const ExtractedSceneRender& sceneRender);
    /// States this tick's frame facts and hands them to the context, which owns
    /// the recording order: prepare → begin → graphics → insets/UI → view and
    /// display compose → retain → end → seal. Returns what the host submits (or
    /// an invalid frame when the tick's present was not acquired).
    static RecordedFrame recordFrame(App&                        app,
                                     RuntimeRenderContext&       context,
                                     float                       dt,
                                     ExtractedSceneRender        sceneRender,
                                     TickFrame&                  frame,
                                     const FPresentFrame&        presentFrame);
    /// Submits the recording (or an empty frame) and presents the surface.
    static void submitRecordedFrame(App& app, FPresentFrame& presentFrame, const RecordedFrame& recorded);
};

} // namespace ya

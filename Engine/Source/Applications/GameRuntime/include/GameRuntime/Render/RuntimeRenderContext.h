#pragma once

#include "Core/Api.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "RHI/Core/PresentFrame.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RecordedFrame.h"

namespace ya
{

struct App;
struct RenderDeviceState;
struct IFrameRecordExtensions;
class SceneRenderScheduler;
class ExtractedSceneRender;

/// The application's frame render context: the order one product frame runs in.
///
/// This is the application's arrangement, not a reusable pipeline. It answers
/// only questions about *this* application's frame: which Views this tick
/// declares, which of them the displayed View is, how the plan's composed Views
/// become insets on that root, where the host's own stages sit in the record
/// sequence, and when the frame's surface is acquired, submitted and presented.
/// `RenderDeviceState` owns the machinery each step is written with and has no
/// opinion about this order -- it exposes the steps, this file spells out the
/// sequence. See `.agent/plan/render-application-boundary/plan.md` (AB7).
///
/// The order, once, in the order it happens (`tick`):
///
///   offscreen pump  → earlier ticks' queued offscreen jobs submitted, previous
///                     tick's offscreen submission finalized
///   flight          → which flight slot this recording uses
///   declare         → every registered producer declares its Views
///   extract         → the sealed plan's Scenes extracted into snapshots
///   prepare         → one camera-dependent packet per View
///   build           → frame facts + the UI snapshot the packet borrows
///   acquire         → the device's frame bookkeeping first, then the frame's
///                     surface acquired; a frame whose surface cannot be
///                     acquired still ran everything above (host policy: skip
///                     record, run module extras)
///   record          → present target → prepare → begin → graphics → insets +
///                     UI → view compose → display compose → end + seal
///   submit + extras → the recording (or an empty frame) submitted, the image
///                     presented, then the modules' own windows
///
/// `record` is that sequence's middle, spelled out on its own because it is
/// what a test pins and a reader traces: the plan is values, the host's own
/// record stages are the explicit call argument.
class YA_GAME_RUNTIME_API RuntimeRenderContext
{
  public:
    RuntimeRenderContext() = default;
    explicit RuntimeRenderContext(RenderDeviceState& device)
        : _device(&device)
    {
    }

    /// The renderer this frame records through. Bound when the device is
    /// created; the context is only meaningful while that device lives.
    void bind(RenderDeviceState& device) { _device = &device; }

    /// One product frame's render phase, from the offscreen pump to the
    /// modules' own present. `app` is this context's owner; everything the
    /// steps need (scene manager, producers, UI host, render services) is read
    /// off it, and the per-tick state is tick-local, never stored here.
    void tick(App& app, float dt);

    /// The recording step of that order. `extensions` is the host's
    /// contribution at the stages the order defines (view compose, display
    /// compose, capture); null means the host contributes nothing, which is
    /// what a headless or UI-only frame wants. It is a call argument, not plan
    /// data: a sealed plan is values.
    [[nodiscard]] RecordedFrame record(const RenderFramePlan& plan,
                                       IFrameRecordExtensions* extensions);

  private:
    /// Pre-record prerequisite: waits for the previous tick's offscreen
    /// submission and finalizes it, then records and submits whatever earlier
    /// ticks queued (IBL preprocess, cubemap conversions, terrain rebuilds). A
    /// job submitted here becomes readable on the next pump, which is why this
    /// runs before this tick's View preparation instead of after it.
    void pumpOffscreenTasks(App& app);

    /// Which slot of a per-frame ring this recording may use.
    [[nodiscard]] uint32_t resolveFlightIndex(App& app) const;

    /// Declares this tick's views from every registered producer and submits
    /// each declaration to `scheduler` (which stores a copy of it). The
    /// collector is this step's own storage: nothing downstream refers to it,
    /// so no caller has to keep it alive. The scheduler is a parameter because
    /// it is the tick's own arrangement: `tick` owns it for the duration of
    /// one tick, and this is one of the two steps that write into it.
    void declareViews(App& app, float dt, SceneRenderScheduler& scheduler);

    /// Groups the declarations and extracts Scene content for them. Grouping
    /// and extraction stay separate: seal() reads no ECS, this step does.
    [[nodiscard]] ExtractedSceneRender extractScenes(App& app, SceneRenderScheduler& scheduler);

    /// Pairs this tick's View recordings with their frame data and prepares
    /// one View's camera-dependent packet each. Frame-level constants are not
    /// part of it: they live on the frame packet (see `buildGameRenderFrame`).
    void prepareViews(App& app, ExtractedSceneRender& sceneRender);

    /// One tick's frame facts together with the UI snapshot the packet borrows.
    /// They are built as one value because the packet points into the snapshot:
    /// returning them separately made the caller declare a local before the
    /// call and keep it alive by convention.
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
    [[nodiscard]] TickFrame buildGameRenderFrame(App&                        app,
                                                 float                       dt,
                                                 uint32_t                    flightIndex,
                                                 const ExtractedSceneRender& sceneRender);

    /// States this tick's frame facts and hands them to `record`. Returns what
    /// the host submits (or an invalid frame when the tick's present was not
    /// acquired).
    [[nodiscard]] RecordedFrame recordFrame(App&                 app,
                                            float                dt,
                                            ExtractedSceneRender sceneRender,
                                            TickFrame&           frame,
                                            const FPresentFrame& presentFrame);

    /// Submits the recording (or an empty frame) and presents the surface.
    void submitRecordedFrame(App& app, FPresentFrame& presentFrame, const RecordedFrame& recorded);

    RenderDeviceState* _device = nullptr;
};

} // namespace ya

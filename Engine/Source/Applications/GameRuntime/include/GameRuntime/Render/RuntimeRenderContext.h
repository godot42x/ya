#pragma once

#include "Core/Api.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RecordedFrame.h"

namespace ya
{

struct RenderDeviceState;
struct IFrameRecordExtensions;

/// The application's frame render context: the order one product frame is
/// recorded in.
///
/// This is the application's arrangement, not a reusable pipeline. It answers
/// only questions about *this* application's frame: which surface the frame
/// presents through, which of the plan's Views is the display root the window
/// shows, how the plan's composed Views become insets on that root, and where
/// the host's own stages sit in the sequence -- passed to `record` explicitly,
/// never carried on the plan. `RenderDeviceState` owns the machinery each step
/// is written with and has no opinion about this order -- it exposes the
/// steps, this file spells out the sequence. See
/// `.agent/plan/render-application-boundary/plan.md` (AB7).
///
/// The order, once, in the order it happens:
///
///   present target  → this frame's present target exists before any command is
///                     recorded; building one is a safe-point action
///   prepare         → derived scene state, pending mutations, View target
///                     allocation, compose pipelines and Scene-keyed bindings
///   begin           → the flight's submission and view-output table open, and
///                     its command buffer begins
///   graphics        → the world graph into the display root's offscreen RT
///   insets + UI     → the plan's composed Views lifted onto that RT, then the
///                     game UI composed onto it (after post, never into bloom)
///   view compose    → the host's view-compose stage
///   display compose → the surface's pass onto swapchain[imageIndex], running
///                     the host's display stages and its capture inside it
///   end + seal      → close the command buffer and seal the submission
///
/// Acquire and present stay with the host's present coordinator: this records
/// what the host then submits, or refuses to record (an invalid `RecordedFrame`)
/// when the host did not acquire this frame, in which case the host submits an
/// empty frame and the acquired image is still legalized.
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

    /// Record one frame. `extensions` is the host's contribution at the stages
    /// this order defines (view compose, display compose, capture); null means
    /// the host contributes nothing, which is what a headless or UI-only frame
    /// wants. It is a call argument, not plan data: a sealed plan is values.
    [[nodiscard]] RecordedFrame record(const RenderFramePlan& plan,
                                       IFrameRecordExtensions* extensions);

  private:
    RenderDeviceState* _device = nullptr;
};

} // namespace ya

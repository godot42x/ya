#pragma once

#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ShadowSettings.h"
#include "RHI/Core/SurfaceId.h"

#include <cstddef>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace ya
{

struct ICommandBuffer;
struct IRenderSurfaceContext;
struct RenderFrameData;
class RenderSubmission;
class ViewTargetStore;
struct Scene;

/// Frame-level facts of one recorded host frame.
///
/// This deliberately holds no camera: which Views are drawn, from which
/// cameras and into which outputs is declared by their owners and carried on
/// each View's plan entry (`SceneViewDesc` / `SceneViewTask`) and its
/// prepared data (`RenderFrameData`). A camera packet here would be a second
/// spelling of that, and every consumer would have to ask which View a given
/// field came from.
struct FramePacket
{
    uint32_t flightIndex = 0;
    /// The product tick this frame belongs to (`App::_hostTick`): the app's
    /// clock, the axis UBO `frameIdx` and animations run on. Not a GPU sync
    /// fact and not the device's frame ordinal (`IRender::recordedFrameIndex`)
    /// -- the two counters advance 1:1 in a live window today, but they answer
    /// different questions and diverge the moment rendering is skipped or
    /// headless.
    uint64_t hostTick    = 0;
    float    deltaTime   = 0.0f;
    /// Seconds since the host clock started -- the value the shader-facing
    /// frame UBO carries as `time`. Not the same as `deltaTime`.
    float    elapsedTimeSeconds = 0.0f;

    /// Host render scale for this tick: a View's rect is divided by it to get
    /// the render target extent. One host setting, not a per-View property, so
    /// it stays here next to the frame facts.
    float renderScale = 1.0f;

    const ShadowSettings* shadowSettings = nullptr;
};

/// Acquire / present destination for this frame. The host/present coordinator
/// must call `acquirePresentFrame` before the application records the frame
/// (`RuntimeRenderContext::record`) and `submitPresentFrame` after. The renderer
/// does not acquire or present.
/// `imageIndex < 0` means this surface is not presenting this frame.
///
/// What the window starts from is a host fact, not something the renderer may
/// infer: an editor authoring View is a display root (`bDisplayRoot`) and is
/// still not what the window shows, because the editor's chrome samples it
/// inside a viewport widget. The host says which of the two this frame is.
enum class ESurfaceBackdrop : uint8_t
{
    /// The window is the View's display image this frame published. The
    /// renderer resolves that image inside `record`, where it exists, together
    /// with the encoding its format cannot express (`FSurfaceImage`).
    ViewDisplayImage,
    /// The host's own passes fill the surface; the surface pass contributes its
    /// clear and nothing else.
    HostContent,
};

struct PresentFrameInput
{
    IRenderSurfaceContext* surface    = nullptr;
    /// Which surface that is, in the device's registry. The renderer files this
    /// frame's present target under this id, so a window closed and reopened on
    /// the same address is a different surface rather than the previous one's
    /// imported images. The host knows it (it binds windows to surfaces); the
    /// renderer would have to guess it from the pointer.
    SurfaceId              surfaceId{};
    int32_t                imageIndex = -1;
    /// Defaults to the plain path: a host that says nothing gets the window
    /// showing the View. The host's answer is `App::presentsViewDisplayImage`,
    /// which asks the modules whether one of them fills THIS surface -- the
    /// answer belongs to the window, so it is carried per present target rather
    /// than decided once for the frame. See plan
    /// `.agent/plan/display-compose-encoding/plan.md` (F2).
    ESurfaceBackdrop backdrop = ESurfaceBackdrop::ViewDisplayImage;
};

/// Sealed host frame value consumed by the application's recording order
/// (`RuntimeRenderContext::record`).
/// `sceneRender` owns the extracted plan together with the recordings paired
/// with it, so the Scene a view renders is already on that view's own task.
/// Not an active-Scene query and not swapchain ownership. Immutable data
/// throughout: the host's own record stages are not on the plan -- they are
/// explicit arguments of the record call, so a sealed plan can never grow
/// behavior after it was built.
struct RenderFramePlan
{
    ExtractedSceneRender sceneRender{};
    FramePacket          frame{};
    PresentFrameInput    present{};
};

/// One Scene family to record into a single graph on the live submission.
struct ViewFamilyRecordContext
{
    ICommandBuffer*                                      cmdBuf     = nullptr;
    /// Frame-level facts for this recording. Each View carries its own camera
    /// on its own recording (`SceneViewRecording.task` / `frameData`).
    const FramePacket*                                   frame      = nullptr;
    RenderSubmission*                                    submission = nullptr;
    const SceneRenderPlan*                               plan       = nullptr;
    const SceneViewFamilyPlan*                           family     = nullptr;
    const ViewTargetStore*                               targets    = nullptr;
    /// The family's views, each carrying its own task and therefore its own
    /// Scene; a family exists only for one (Scene, revision, policy).
    std::vector<SceneViewRecording>                      views;
};

/// One View being recorded, plus the frame-level facts it shares with its
/// siblings. Pipelines read matrices and extent from `view` -- the View's own
/// declaration and prepared data -- and the tick/flight/clock facts from
/// `frame`. They never query swapchain or NativeWindow.
struct RenderPipelineFrameContext
{
    ICommandBuffer*    cmdBuf = nullptr;
    const FramePacket* frame  = nullptr;
    RenderSubmission*          submission = nullptr;
    RenderViewRecordingContext view{};
    /// The Scene `view` was declared against, taken from that view's task.
    Scene*                     derivedScene = nullptr;
};

} // namespace ya

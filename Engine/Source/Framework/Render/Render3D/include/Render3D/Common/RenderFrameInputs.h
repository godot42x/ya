#pragma once

#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Common/FrameRecordExtensions.h"

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
struct Scene;
struct UIFrameSnapshot;

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
    uint64_t frameIndex  = 0;
    float    deltaTime   = 0.0f;

    /// Host render scale for this tick: a View's rect is divided by it to get
    /// the render target extent. One host setting, not a per-View property, so
    /// it stays here next to the frame facts.
    float renderScale = 1.0f;

    const ShadowSettings* shadowSettings = nullptr;
    /// Game UI snapshot for this tick, consumed by the display compose. Built
    /// before graph build; the live WidgetTree is never read while recording.
    const UIFrameSnapshot* uiFrameSnapshot = nullptr;
};

/// Blit a published View onto the primary Camera display RT. `destRect` is in
/// that RT's pixel space (origin at the RT top-left).
struct ViewDisplayInset
{
    SceneViewId viewId = 0;
    Rect2D      destRect{};
};

[[nodiscard]] inline std::vector<ViewDisplayInset> viewDisplayInsetsFromPlan(const SceneRenderPlan& plan)
{
    std::vector<ViewDisplayInset> insets;
    for (const auto& task : plan.viewTasks) {
        const SceneViewDesc& desc = task.desc;
        if (desc.isDisplayRoot() || desc.composeRect.extent.x <= 0.0f || desc.composeRect.extent.y <= 0.0f) {
            continue;
        }
        insets.push_back(ViewDisplayInset{
            .viewId   = desc.viewId,
            .destRect = desc.composeRect,
        });
    }
    return insets;
}

/// Host-declared insets: extra Views blitted onto the display root's RT after
/// graphics + UI. Not a second Surface, and not behavior -- the host code that
/// records overlays lives behind `IFrameRecordExtensions::recordViewCompose`.
struct ViewComposeInput
{
    std::vector<ViewDisplayInset> insets;

    [[nodiscard]] bool empty() const { return insets.empty(); }
};

/// Acquire / present destination for this frame. The host/present coordinator
/// must call `acquirePresentFrame` before `RenderDeviceState::record` and
/// `submitPresentFrame` after. The renderer does not acquire or present.
/// `imageIndex < 0` means this surface is not presenting this frame.
struct PresentFrameInput
{
    IRenderSurfaceContext* surface    = nullptr;
    int32_t                imageIndex = -1;
};

/// Sealed host frame value consumed by `RenderDeviceState::record`.
/// `sceneRender` owns the extracted plan together with the recordings paired
/// with it, so the Scene a view renders is already on that view's own task.
/// Not an active-Scene query and not swapchain ownership.
struct RenderFramePlan
{
    ExtractedSceneRender sceneRender{};
    FramePacket          frame{};
    ViewComposeInput     viewCompose{};
    PresentFrameInput    present{};
    /// What the host records at the stages the coordinator defines. Null means
    /// the host contributes nothing; the renderer's own order is unaffected
    /// either way, and no stage can be installed out of order because the plan
    /// no longer names an order at all.
    IFrameRecordExtensions* recordExtensions = nullptr;
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

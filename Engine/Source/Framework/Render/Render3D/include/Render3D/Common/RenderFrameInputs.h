#pragma once

#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Services/PresentationGraphService.h"

#include <cstddef>
#include <functional>
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
/// each View's plan entry (`SceneViewDesc` / `SceneViewportTask`) and its
/// prepared data (`RenderFrameData`). A camera packet here would be a second
/// spelling of that, and every consumer would have to ask which View a given
/// field came from.
struct FramePacket
{
    struct OverlayInput
    {
        const std::vector<RenderOverlaySprite2D>* screenSprites = nullptr;
        const std::vector<RenderOverlaySprite3D>* worldSprites  = nullptr;
        const std::vector<RenderOverlayText2D>*   screenTexts   = nullptr;
        const std::vector<RenderOverlayLine3D>*   worldLines    = nullptr;
    };

    uint32_t flightIndex = 0;
    uint64_t frameIndex  = 0;
    float    deltaTime   = 0.0f;

    /// Host render scale for this tick: a View's rect is divided by it to get
    /// the render target extent. One host setting, not a per-View property, so
    /// it stays here next to the frame facts.
    float viewportFrameBufferScale = 1.0f;

    const ShadowSettings* shadowSettings = nullptr;
    OverlayInput          overlay{};
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
    for (const auto& task : plan.viewportTasks) {
        const SceneViewDesc& desc = task.desc;
        if (desc.ownsHostViewport() || desc.composeRect.extent.x <= 0.0f || desc.composeRect.extent.y <= 0.0f) {
            continue;
        }
        insets.push_back(ViewDisplayInset{
            .viewId   = desc.viewId,
            .destRect = desc.composeRect,
        });
    }
    return insets;
}

/// Overlay / gizmos onto this camera's offscreen RT (after graphics + UI).
/// Not display compose; must not recreate GPU resources. Insets are extra
/// Views on the primary display, not a second Surface.
struct ViewComposeInput
{
    std::function<void(ICommandBuffer*)> recordCompose;
    std::vector<ViewDisplayInset>        insets;

    [[nodiscard]] bool empty() const
    {
        return !recordCompose && insets.empty();
    }
};

/// Images onto the present surface's swapchain[imageIndex] via
/// `PresentationGraphService::recordDisplayCompose`. Not Camera view compose.
struct DisplayComposeInput
{
    PresentationGraphService::Extensions extensions{};
};

/// Acquire / present destination for this frame. The host/present coordinator
/// must call `acquirePresentFrame` before `RenderFrameCoordinator::record` and
/// `submitPresentFrame` after. Device/coordinator do not acquire or present.
/// `imageIndex < 0` means this surface is not presenting this frame.
struct PresentFrameInput
{
    IRenderSurfaceContext* surface    = nullptr;
    int32_t                imageIndex = -1;
};

/// Sealed host frame value consumed by `RenderFrameCoordinator::record`.
/// `sceneRender` owns the extracted plan together with the recordings paired
/// with it, so the Scene a view renders is already on that view's own task.
/// Not an active-Scene query and not swapchain ownership.
struct RenderFramePlan
{
    ExtractedSceneRender sceneRender{};
    FramePacket          frame{};
    ViewComposeInput     viewCompose{};
    DisplayComposeInput  displayCompose{};
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
    /// The family's views, each carrying its own task and therefore its own
    /// Scene; a family exists only for one (Scene, revision, policy).
    std::vector<SceneViewRecording>                      views;
    std::shared_ptr<const RenderViewportOverlaySnapshot> overlaySnapshot;
};

/// One View being recorded, plus the frame-level facts it shares with its
/// siblings. Pipelines read matrices and extent from `view` -- the View's own
/// declaration and prepared data -- and the tick/flight/clock facts from
/// `frame`. They never query swapchain or NativeWindow.
struct RenderPipelineFrameContext
{
    ICommandBuffer*    cmdBuf = nullptr;
    const FramePacket* frame  = nullptr;
    std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
    RenderSubmission*          submission = nullptr;
    RenderViewRecordingContext view{};
    /// The Scene `view` was declared against, taken from that view's task.
    Scene*                     derivedScene = nullptr;
};

} // namespace ya

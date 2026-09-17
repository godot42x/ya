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

/// Immutable Camera / WorldView data for one graphics frame. The camera owner
/// fills view, projection, viewProjection and offscreen extent before graph
/// build. This is not a present surface and not swapchain size.
struct CameraFrameInput
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

    /// Which features this view draws (see RenderFeatures.h). Per view, not
    /// per scene: the editor world view draws gizmos, a game view does not.
    FRenderFeatureMask viewFeatures = toMask(ERenderFeature::Game);

    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::mat4 viewProjection = glm::mat4(1.0f);
    glm::vec3 cameraPos      = glm::vec3(0.0f);
    /// Offscreen WorldView extent, not swapchain.
    Rect2D viewportRect             = {};
    float  viewportFrameBufferScale = 1.0f;

    RenderFrameData*      frameData      = nullptr;
    const ShadowSettings* shadowSettings = nullptr;
    OverlayInput          overlay{};
    const UIFrameSnapshot* uiFrameSnapshot = nullptr;

    [[nodiscard]] bool hasOffscreenExtent() const
    {
        return viewportRect.extent.x > 0.0f && viewportRect.extent.y > 0.0f;
    }
};

[[nodiscard]] inline CameraFrameInput cameraForViewRecording(
    const CameraFrameInput&   host,
    const SceneViewRecording& recording)
{
    CameraFrameInput camera = host;
    if (recording.task) {
        const SceneViewportTask& task = *recording.task;
        const SceneViewDesc&     desc = task.desc;
        camera.view           = desc.view;
        camera.projection     = desc.projection;
        camera.viewProjection = desc.viewProjection();
        camera.cameraPos      = desc.cameraPos;
        const glm::vec2 outputExtent = task.output.hasExtent()
                                           ? glm::vec2{
                                                 static_cast<float>(task.output.extent.width),
                                                 static_cast<float>(task.output.extent.height),
                                             }
                                           : desc.viewportRect.extent;
        if (desc.ownsHostViewport()) {
            camera.viewportRect = desc.viewportRect;
            if (outputExtent.x > 0.0f && outputExtent.y > 0.0f) {
                camera.viewportRect.extent = outputExtent;
            }
        }
        else {
            // Overlay Views record into their own RT. Compose dest lives on
            // the task, not in the camera rect that drives host resize.
            camera.viewportRect = Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = outputExtent,
            };
        }
    }
    camera.frameData = recording.frameData;
    // The view's own feature set, not the host camera's: each recording may
    // draw a different subset (editor world view vs camera preview).
    if (recording.frameData) {
        camera.viewFeatures = recording.frameData->viewFeatures;
    }
    return camera;
}

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
    CameraFrameInput     camera{};
    ViewComposeInput     viewCompose{};
    DisplayComposeInput  displayCompose{};
    PresentFrameInput    present{};
};

/// One Scene family to record into a single graph on the live submission.
struct ViewFamilyRecordContext
{
    ICommandBuffer*                                      cmdBuf     = nullptr;
    CameraFrameInput                                     hostCamera{};
    RenderSubmission*                                    submission = nullptr;
    const SceneRenderPlan*                               plan       = nullptr;
    const SceneViewFamilyPlan*                           family     = nullptr;
    /// The family's views, each carrying its own task and therefore its own
    /// Scene; a family exists only for one (Scene, revision, policy).
    std::vector<SceneViewRecording>                      views;
    std::shared_ptr<const RenderViewportOverlaySnapshot> overlaySnapshot;
};

/// Recording extras plus the camera packet consumed by Forward/Deferred.
/// Pipelines read `camera` for matrices and extent; they do not query
/// swapchain or NativeWindow. `submission` is the live RenderSubmission
/// owner; `view` is the View being recorded. A missing view packet still
/// means the current single-View path.
struct RenderPipelineFrameContext
{
    ICommandBuffer*  cmdBuf = nullptr;
    CameraFrameInput camera{};
    std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
    RenderSubmission*          submission = nullptr;
    RenderViewRecordingContext view{};
    /// The Scene `view` was declared against, taken from that view's task.
    Scene*                     derivedScene = nullptr;
};

} // namespace ya

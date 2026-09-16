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
struct UIFrameSnapshot;

/// One View inside a sealed plan: the task must point at `plan.viewportTasks[i]`,
/// and `frameData` is the host's View-owned preparation for that task.
struct SceneViewRecording
{
    const SceneViewportTask* task      = nullptr;
    RenderFrameData*         frameData = nullptr;
};

/// Sealed SceneRenderPlan input for one host render call. The plan owns the
/// immutable Scene snapshot table; `views` is parallel to `plan.viewportTasks`.
/// RenderRuntime records every view in this list. It does not own the plan,
/// tasks, or frameData. Graph-exported image and overlay handles used while
/// recording are retained on the live submission until that flight is reused
/// after its fence.
struct SceneRenderPlanInput
{
    const SceneRenderPlan*           plan = nullptr;
    std::vector<SceneViewRecording>  views;

    [[nodiscard]] bool empty() const { return plan == nullptr && views.empty(); }

    [[nodiscard]] bool complete() const
    {
        if (!plan || views.size() != plan->viewportTasks.size()) {
            return false;
        }
        for (size_t index = 0; index < views.size(); ++index) {
            const SceneViewRecording& recording = views[index];
            if (!recording.task || recording.task != &plan->viewportTasks[index] || !recording.frameData) {
                return false;
            }
            if (!plan->snapshotFor(*recording.task)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] const SceneViewportTask* primaryTask() const
    {
        if (plan) {
            if (const SceneViewportTask* root = plan->displayRootTask()) {
                return root;
            }
        }
        return views.empty() ? nullptr : views.front().task;
    }
};

[[nodiscard]] inline glm::mat4 makeCameraViewProjection(const glm::mat4& projection, const glm::mat4& view)
{
    return projection * view;
}

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
    bool     bAppStopped = false;
    float    deltaTime   = 0.0f;

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
        camera.view           = task.view;
        camera.projection     = task.projection;
        camera.viewProjection = task.viewProjection;
        camera.cameraPos      = task.cameraPos;
        const glm::vec2 outputExtent = task.output.hasExtent()
                                           ? glm::vec2{
                                                 static_cast<float>(task.output.extent.width),
                                                 static_cast<float>(task.output.extent.height),
                                             }
                                           : task.viewportRect.extent;
        if (task.ownsHostViewport()) {
            camera.viewportRect = task.viewportRect;
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
        if (task.ownsHostViewport() || task.composeRect.extent.x <= 0.0f || task.composeRect.extent.y <= 0.0f) {
            continue;
        }
        insets.push_back(ViewDisplayInset{
            .viewId   = task.viewId,
            .destRect = task.composeRect,
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
/// must call `acquirePresentFrame` before `renderFrame` and
/// `submitPresentFrame` after. RenderRuntime does not acquire or present.
/// `imageIndex < 0` means this surface is not presenting this frame.
struct PresentFrameInput
{
    IRenderSurfaceContext* surface    = nullptr;
    int32_t                imageIndex = -1;
};

/// Recording extras plus the camera packet consumed by Forward/Deferred.
/// Pipelines read `camera` for matrices and extent; they do not query
/// swapchain or NativeWindow. `submission` / `view` are the explicit
/// lifetime contexts for resource-set beginSubmission/beginView; a missing
/// view packet still means the current single-View path.
struct RenderPipelineFrameContext
{
    ICommandBuffer*  cmdBuf = nullptr;
    CameraFrameInput camera{};
    std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
    RenderSubmissionContext    submission{};
    RenderViewRecordingContext view{};
};

} // namespace ya

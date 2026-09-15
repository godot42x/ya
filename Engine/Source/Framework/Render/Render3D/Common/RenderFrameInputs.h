#pragma once

#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Services/PresentationGraphService.h"

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

/// Sealed SceneRenderPlan input for one host render call. The plan owns the
/// immutable Scene snapshot table; the task identifies the view being recorded.
/// RenderRuntime does not build or retain either object beyond the call.
struct SceneRenderPlanInput
{
    const SceneRenderPlan*   plan = nullptr;
    const SceneViewportTask* task = nullptr;

    [[nodiscard]] bool empty() const { return plan == nullptr && task == nullptr; }
    [[nodiscard]] bool complete() const { return plan != nullptr && task != nullptr; }
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
    };

    uint32_t flightIndex = 0;
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

/// Overlay / gizmos onto this camera's offscreen RT (after graphics + UI).
/// Not display compose; must not recreate GPU resources.
struct ViewComposeInput
{
    std::function<void(ICommandBuffer*)> recordCompose;

    [[nodiscard]] bool empty() const
    {
        return !recordCompose;
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
/// swapchain or NativeWindow.
struct RenderPipelineFrameContext
{
    ICommandBuffer*  cmdBuf = nullptr;
    CameraFrameInput camera{};
    std::shared_ptr<const RenderViewportOverlaySnapshot> viewportOverlaySnapshot = nullptr;
};

} // namespace ya

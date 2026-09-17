#pragma once

#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Stage/IRenderStage.h"
#include "GameRuntime/HostViewState.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/RenderFrameCoordinator.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace ya
{

struct AppRenderState
{
    std::unique_ptr<RenderDeviceState>                 device;
    std::unique_ptr<RenderFrameCoordinator>            coordinator;
    ShadowSettings                                     shadowSettings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    bool                                               bRenderMirror  = false;
    HostViewState                                      hostView;
    std::optional<HostViewState>                       extensionHostView;
    /// Host policy: skip Scene family record for UI-only frames (editor 2D canvas).
    bool                                               bWorldSceneRenderEnabled = true;
    /// When true, `cameraPreviewEntityUUID` is the host's explicit choice
    /// (editor selection). UUID 0 then means "do not preview". When false,
    /// GameRuntime may auto-pick the first non-primary scene camera.
    bool                                               bCameraPreviewHostOwned = false;
    uint64_t                                           cameraPreviewEntityUUID = 0;
    /// Global editor-gizmo override (`View > Show Editor Gizmos`). Off by
    /// default: generated editor companions exist in every mode but are drawn
    /// only by views that ask for them. A debug aid, so it is deliberately one
    /// global switch instead of a per-object flag on every companion.
    bool                                               bShowEditorGizmos = false;
    SceneRenderScheduler                               sceneRenderScheduler;
    std::array<std::vector<RenderFrameData>, MAX_FLIGHTS_IN_FLIGHT> viewFrameDataPerFlight{};
};

} // namespace ya

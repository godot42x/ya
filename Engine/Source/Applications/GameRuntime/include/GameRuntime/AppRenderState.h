#pragma once

#include "Render3D/Common/SceneViewProducer.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Stage/IRenderStage.h"
#include "GameRuntime/HostRenderSettings.h"
#include "GameRuntime/HostViewportView.h"
#include "GameRuntime/Render/RuntimeRenderContext.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/RenderDeviceState.h"

#include <memory>
#include <optional>
#include <vector>

namespace ya
{

/// The host's render settings and registrations. It holds no per-tick
/// arrangement: which Views this tick draws is decided inside the tick
/// (`SceneRenderScheduler` lives there), so nothing here is a leftover of the
/// previous tick that a reader has to reason about.
struct AppRenderState
{
    std::unique_ptr<RenderDeviceState>      device;
    /// The application's frame render context (see RuntimeRenderContext): the
    /// order one product frame is recorded in. Created with `device` and
    /// destroyed before it, because it records through that device -- "which
    /// surface this frame presents, which View the window shows" is the app's
    /// arrangement and lives here, not on the renderer.
    std::unique_ptr<RuntimeRenderContext>   runtimeRender;
    /// The surface this app presents its display root through. Written once at
    /// init, from the window the host created before the device existed; the
    /// renderer keeps every surface in one registry and privileges none, so
    /// "which window this app shows its image in" has to be the app's answer.
    SurfaceId                               hostSurfaceId{};
    ShadowSettings                          shadowSettings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    bool                                    bRenderMirror  = false;
    /// The host's render settings: the clock, and the resolution its viewport
    /// renders at. Settings only -- no camera, no View geometry.
    HostRenderSettings                      hostSettings;
    /// Who may declare views this tick. Registration, not state: a producer that
    /// wants nothing drawn simply declares nothing.
    std::vector<ISceneViewProducer*>        viewProducers;
    /// Scenes whose content the renderer produced in the previous tick. Written
    /// by the render path and read by systems that only need to work for what
    /// gets drawn (SkeletonAnimationSystem), so they stop depending on a switch
    /// that describes a viewport instead of a Scene.
    std::vector<Scene*>                     renderedScenesLastTick;
    /// The frame's host-viewport arrangement: which View the window shows, in
    /// which flight, and that View's camera. Written once per tick from the plan
    /// (see GameRuntimeTickOrchestrator::tickRender) and read by every consumer
    /// that asks "the image the window shows" -- the panels, automation
    /// screenshots and the editor's viewport.
    HostViewportView                        hostViewport{};
};

} // namespace ya

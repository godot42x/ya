#pragma once

#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/SceneViewProducer.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Stage/IRenderStage.h"
#include "GameRuntime/HostViewState.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/RenderDeviceState.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace ya
{

/// The host's render settings and registrations. It holds no per-tick
/// arrangement: which Views this tick draws is decided inside the tick
/// (`SceneRenderScheduler` lives there), so nothing here is a leftover of the
/// previous tick that a reader has to reason about.
/// Which View the host window shows for the frame that was just recorded, and in
/// which flight its output was published.
///
/// This is an arrangement, not a renderer fact: the app derives it from the
/// plan's display root (the View whose `composeOntoViewId == 0`), and the
/// renderer only ever stores every View's output. Keeping it here is what lets
/// `RenderDeviceState` publish Views without naming one of them "the current
/// one".
struct HostViewportBinding
{
    SceneViewId viewId      = 0;
    /// `MAX_FLIGHTS_IN_FLIGHT` means "no frame has been recorded yet", the same
    /// way `viewId == 0` means "this frame showed no View".
    uint32_t    flightIndex = MAX_FLIGHTS_IN_FLIGHT;
};

struct AppRenderState
{
    std::unique_ptr<RenderDeviceState>      device;
    ShadowSettings                          shadowSettings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    bool                                    bRenderMirror  = false;
    /// Host geometry for the product view: the surface area a view renders into
    /// plus the clock. The world camera is not host state any more; it comes from
    /// whichever producer declared the primary view.
    HostViewState                           hostView;
    /// Who may declare views this tick. Registration, not state: a producer that
    /// wants nothing drawn simply declares nothing.
    std::vector<ISceneViewProducer*>        viewProducers;
    /// Scenes whose content the renderer produced in the previous tick. Written
    /// by the render path and read by systems that only need to work for what
    /// gets drawn (SkeletonAnimationSystem), so they stop depending on a switch
    /// that describes a viewport instead of a Scene.
    std::vector<Scene*>                     renderedScenesLastTick;
    std::array<std::vector<RenderFrameData>, MAX_FLIGHTS_IN_FLIGHT> viewFrameDataPerFlight{};
    /// The frame's host-viewport arrangement. Written by the tick when a frame
    /// was recorded, read by every consumer that asks "the image the window
    /// shows" -- the panels, automation screenshots and the editor's viewport.
    HostViewportBinding hostViewport{};
};

} // namespace ya

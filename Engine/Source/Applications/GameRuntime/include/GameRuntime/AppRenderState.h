#pragma once

#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/SceneRenderScheduler.h"
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
    SceneRenderScheduler                    sceneRenderScheduler;
    std::array<std::vector<RenderFrameData>, MAX_FLIGHTS_IN_FLIGHT> viewFrameDataPerFlight{};
};

} // namespace ya

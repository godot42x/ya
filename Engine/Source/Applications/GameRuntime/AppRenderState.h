#pragma once

#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Stage/IRenderStage.h"
#include "GameRuntime/AppRenderFrameState.h"
#include "Render3D/Common/RenderOverlay.h"
#include "Render3D/RenderRuntime.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace ya
{

struct AppRenderState
{
    std::unique_ptr<RenderRuntime>                     runtime;
    ShadowSettings                                     shadowSettings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    bool                                               bRenderMirror  = false;
    AppRenderFrameState                                frameState;
    std::optional<AppRenderFrameState>                 extensionFrameState;
    std::array<RenderFrameData, MAX_FLIGHTS_IN_FLIGHT> frameDataPerFlight{};
};

} // namespace ya

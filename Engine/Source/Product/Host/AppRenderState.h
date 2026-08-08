#pragma once

#include "Framework/Game/Render/Render3D/RenderFrameData.h"
#include "Framework/Game/Render/Render3D/Shadow/ShadowSettings.h"
#include "Framework/Game/Render/Render3D/Stage/IRenderStage.h"
#include "Product/Host/AppRenderFrameState.h"
#include "Framework/Game/Render/Render3D/Common/RenderOverlay.h"
#include "Framework/Game/Render/Render3D/RenderRuntime.h"

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

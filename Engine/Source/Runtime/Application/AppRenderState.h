#pragma once

#include "Render/RenderFrameData.h"
#include "Render/Shadow/ShadowSettings.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/Application/AppRenderFrameState.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"
#include "Runtime/Rendering/RenderRuntime.h"

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

#pragma once

#include "AppServices/ShadowSettings.h"
#include "AppServices/PostProcessingState.h"

#include <array>
#include <optional>

namespace ya
{

/// Automation shadow overrides applied on top of ShadowSettings. Lives with
/// the shared render contract so framework modules can consume it without
/// reaching the Host.
struct AppAutomationShadowOverrides
{
    std::optional<EShadowQuality::T> quality;
    std::optional<uint32_t>          resolution;
    std::optional<bool>              directionalEnabled;
    std::optional<bool>              pointLightEnabled;
    std::optional<bool>              pointLightUseIndirect;
    std::optional<bool>              pointLightIndirectCullEnabled;
    std::optional<uint32_t>          maxPointLightShadows;
    std::optional<EShadowFilter::T>  filter;
    std::optional<float>             bias;
    std::optional<float>             normalBias;
    std::optional<float>             directionalDistance;
    std::optional<uint32_t>          directionalCascades;
    std::optional<std::array<float, MAX_DIRECTIONAL_CASCADES - 1>> directionalCascadeSplitRatios;
    std::optional<float>             directionalDepthRangeMultiplier;
};

} // namespace ya

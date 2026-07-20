#pragma once

#include "Render/RenderDefines.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// Shadow Quality Presets
// ═══════════════════════════════════════════════════════════════════════════

namespace EShadowQuality
{
enum T : uint32_t
{
    Off    = 0,
    Low    = 1,
    Medium = 2,
    High   = 3,
    Ultra  = 4,
};
} // namespace EShadowQuality

namespace EShadowFilter
{
enum T : uint32_t
{
    Hard     = 0, // no filtering, aliased edges
    PCF_Low  = 1, // 4-tap PCF
    PCF_High = 2, // 16-tap PCF or Poisson disk
};
} // namespace EShadowFilter

// ═══════════════════════════════════════════════════════════════════════════
// ShadowSettings — authoritative shadow configuration (owned by App layer)
//
// The game/app layer writes these settings; the render pipeline reads them
// each frame to drive the shadow system. Individual stages never store
// independent shadow config — they receive it from the pipeline.
// ═══════════════════════════════════════════════════════════════════════════

struct ShadowSettings
{
    EShadowQuality::T quality = EShadowQuality::Medium;

    // Resolution of shadow map atlas (per face/layer)
    uint32_t resolution = 1024;

    // Directional light shadow
    bool     directionalEnabled   = true;
    float    directionalDistance  = 50.0f; // max shadow distance from camera
    uint32_t directionalCascades  = 1;     // 1 = legacy single map, 2..4 = CSM
    bool     directionalStableFit = true;
    std::array<float, MAX_DIRECTIONAL_CASCADES - 1> directionalCascadeSplitRatios{0.02f, 0.1f, 0.5f};
    float directionalDepthRangeMultiplier = 10.0f;

    // Point light shadow
    bool     pointLightEnabled             = true;
    bool     pointLightUseIndirect         = false;
    bool     pointLightIndirectCullEnabled = true;
    uint32_t maxPointLightShadows          = 1; // how many point lights get shadows

    // Filtering
    EShadowFilter::T filter = EShadowFilter::Hard;

    // Bias
    float bias       = 0.0005f;
    float normalBias = 0.02f;

    // ─── Helpers ─────────────────────────────────────────────────────

    [[nodiscard]] bool isEnabled() const { return quality != EShadowQuality::Off; }

    [[nodiscard]] uint32_t getEffectivePointLightCount() const
    {
        if (!isEnabled() || !pointLightEnabled) return 0;
        return std::min(maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));
    }

    [[nodiscard]] uint32_t getEffectiveDirectionalCascadeCount() const
    {
        if (!isEnabled() || !directionalEnabled) return 0;
        return std::clamp(directionalCascades,
                          1u,
                          static_cast<uint32_t>(MAX_DIRECTIONAL_CASCADES));
    }

    void resetDirectionalCascadeSplitRatios()
    {
        switch (std::clamp(directionalCascades, 1u, static_cast<uint32_t>(MAX_DIRECTIONAL_CASCADES))) {
        case 2:
            directionalCascadeSplitRatios = {0.1f, 0.35f, 0.7f};
            break;
        case 3:
            directionalCascadeSplitRatios = {0.05f, 0.25f, 0.65f};
            break;
        case 4:
            directionalCascadeSplitRatios = {0.02f, 0.1f, 0.5f};
            break;
        default:
            directionalCascadeSplitRatios = {0.1f, 0.3f, 0.6f};
            break;
        }
    }

    void sanitizeDirectionalCascadeSplitRatios()
    {
        constexpr float MIN_SPLIT_GAP = 0.001f;
        float previous = 0.0f;
        for (uint32_t splitIndex = 0; splitIndex < directionalCascadeSplitRatios.size(); ++splitIndex) {
            const float maximum = 1.0f - MIN_SPLIT_GAP * static_cast<float>(directionalCascadeSplitRatios.size() - splitIndex);
            directionalCascadeSplitRatios[splitIndex] = std::clamp(
                directionalCascadeSplitRatios[splitIndex], previous + MIN_SPLIT_GAP, maximum);
            previous = directionalCascadeSplitRatios[splitIndex];
        }
    }

    void applyQualityPreset(EShadowQuality::T q)
    {
        const ShadowSettings preset = fromQuality(q);
        quality                     = q;
        resolution                  = preset.resolution;
        directionalEnabled          = preset.directionalEnabled;
        directionalCascades         = preset.directionalCascades;
        directionalStableFit        = preset.directionalStableFit;
        directionalCascadeSplitRatios = preset.directionalCascadeSplitRatios;
        directionalDepthRangeMultiplier = preset.directionalDepthRangeMultiplier;
        pointLightEnabled           = preset.pointLightEnabled;
        maxPointLightShadows        = preset.maxPointLightShadows;
        filter                      = preset.filter;
    }

    // ─── Presets ─────────────────────────────────────────────────────

    static ShadowSettings fromQuality(EShadowQuality::T q)
    {
        ShadowSettings s;
        s.quality = q;
        switch (q) {
        case EShadowQuality::Off:
            s.resolution           = 0;
            s.directionalEnabled   = false;
            s.directionalCascades  = 0;
            s.directionalStableFit = true;
            s.pointLightEnabled    = false;
            s.maxPointLightShadows = 0;
            s.filter               = EShadowFilter::Hard;
            break;
        case EShadowQuality::Low:
            s.resolution           = 512;
            s.directionalCascades  = 1;
            s.directionalStableFit = true;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = 1;
            s.filter               = EShadowFilter::Hard;
            break;
        case EShadowQuality::Medium:
            s.resolution           = 1024;
            s.directionalCascades  = 1;
            s.directionalStableFit = true;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = 1;
            s.filter               = EShadowFilter::PCF_Low;
            break;
        case EShadowQuality::High:
            s.resolution           = 2048;
            s.directionalCascades  = 2;
            s.directionalStableFit = true;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = 4;
            s.filter               = EShadowFilter::PCF_High;
            break;
        case EShadowQuality::Ultra:
            s.resolution           = 4096;
            s.directionalCascades  = 4;
            s.directionalStableFit = true;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = MAX_POINT_LIGHTS;
            s.filter               = EShadowFilter::PCF_High;
            break;
        }
        s.resetDirectionalCascadeSplitRatios();
        return s;
    }
};

} // namespace ya

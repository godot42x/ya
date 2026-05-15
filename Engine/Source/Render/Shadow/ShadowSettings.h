#pragma once

#include "Render/RenderDefines.h"

#include <algorithm>
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
    uint32_t directionalCascades = 1;     // cascade count (1 = no cascading, future: 2/4)

    // Point light shadow
    bool     pointLightEnabled              = true;
    bool     pointLightUseIndirect          = false;
    bool     pointLightIndirectCullEnabled  = true;
    uint32_t maxPointLightShadows           = 1; // how many point lights get shadows

    // Filtering
    EShadowFilter::T filter = EShadowFilter::Hard;

    // Bias
    float bias       = 0.005f;
    float normalBias = 0.01f;

    // ─── Helpers ─────────────────────────────────────────────────────

    [[nodiscard]] bool isEnabled() const { return quality != EShadowQuality::Off; }

    [[nodiscard]] uint32_t getEffectivePointLightCount() const
    {
        if (!isEnabled() || !pointLightEnabled) return 0;
        return std::min(maxPointLightShadows, static_cast<uint32_t>(MAX_POINT_LIGHTS));
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
            s.pointLightEnabled    = false;
            s.maxPointLightShadows = 0;
            s.filter               = EShadowFilter::Hard;
            break;
        case EShadowQuality::Low:
            s.resolution           = 512;
            s.directionalCascades  = 1;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = 1;
            s.filter               = EShadowFilter::Hard;
            break;
        case EShadowQuality::Medium:
            s.resolution           = 1024;
            s.directionalCascades  = 1;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = 1;
            s.filter               = EShadowFilter::PCF_Low;
            break;
        case EShadowQuality::High:
            s.resolution           = 2048;
            s.directionalCascades  = 2;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = 4;
            s.filter               = EShadowFilter::PCF_High;
            break;
        case EShadowQuality::Ultra:
            s.resolution           = 4096;
            s.directionalCascades  = 4;
            s.pointLightEnabled    = true;
            s.maxPointLightShadows = MAX_POINT_LIGHTS;
            s.filter               = EShadowFilter::PCF_High;
            break;
        }
        return s;
    }
};

} // namespace ya

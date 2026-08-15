#pragma once

#include "Render3D/Common/ShadowSettings.h"

#include <array>

namespace ya
{

struct IImageView;
struct Sampler;

struct ShadowRuntimeState
{
    bool     bEnableShadowMapping    = true;
    bool     bEnablePointLightShadow = true;
    uint32_t maxShadowedPointLights  = 1;
    uint32_t shadowMapResolution     = 1024;
    EShadowFilter::T filter          = EShadowFilter::Hard;
    float            bias            = 0.0005f;
    float            normalBias      = 0.02f;

    IImageView* directionalDepthIV = nullptr;
    std::array<IImageView*, MAX_POINT_LIGHTS> pointCubeDepthIVs{};
    Sampler*                                  sampler = nullptr;

    [[nodiscard]] bool hasShadowResources() const
    {
        return directionalDepthIV && sampler;
    }
};

} // namespace ya

#pragma once

#include "Render/Shadow/ShadowSettings.h"

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

    ShadowSettings settings{};

    IImageView* directionalDepthIV = nullptr;
    std::array<IImageView*, MAX_POINT_LIGHTS> pointCubeDepthIVs{};
    Sampler*                                  sampler = nullptr;

    [[nodiscard]] bool hasShadowResources() const
    {
        return directionalDepthIV && sampler;
    }
};

} // namespace ya

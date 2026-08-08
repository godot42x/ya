#pragma once

#include "Foundation/RHI/RenderDefines.h"
#include "Foundation/Core/Api.h"

#include <array>

namespace ya::DirectionalShadowMath
{

struct CascadeData
{
    std::array<glm::mat4, MAX_DIRECTIONAL_CASCADES> viewProjections{};
    std::array<float, MAX_DIRECTIONAL_CASCADES>     splits{};
    uint32_t                                        count = 1;
};

[[nodiscard]] YA_RENDER_3D_API CascadeData buildCascades(const glm::vec3& lightDirection,
                                        const glm::mat4& cameraView,
                                        const glm::mat4& cameraProjection,
                                        float            shadowDistance,
                                        uint32_t         shadowResolution,
                                        uint32_t         cascadeCount,
                                        bool             bStableFit,
                                        const std::array<float, MAX_DIRECTIONAL_CASCADES - 1>& splitRatios,
                                        float depthRangeMultiplier);

} // namespace ya::DirectionalShadowMath

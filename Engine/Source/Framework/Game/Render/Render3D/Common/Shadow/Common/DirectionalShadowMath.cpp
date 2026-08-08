#include "DirectionalShadowMath.h"

#include "Foundation/Core/Math/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ya::DirectionalShadowMath
{

namespace
{

glm::vec3 unproject(const glm::mat4& inverseViewProjection, float x, float y, float z)
{
    const glm::vec4 world = inverseViewProjection * glm::vec4(x, y, z, 1.0f);
    return glm::vec3(world) / world.w;
}

glm::mat4 buildCascadeViewProjection(const std::array<glm::vec3, 8>& corners,
                                     const glm::vec3&                lightDirection,
                                     uint32_t                       shadowResolution,
                                     bool                           bStableFit,
                                     float                          depthRangeMultiplier)
{
    glm::vec3 center(0.0f);
    for (const glm::vec3& corner : corners) {
        center += corner;
    }
    center /= static_cast<float>(corners.size());

    float radius = 0.0f;
    for (const glm::vec3& corner : corners) {
        radius = std::max(radius, glm::length(corner - center));
    }
    radius = std::max(radius, 0.5f);
    if (bStableFit) {
        radius = std::ceil(radius * 16.0f) / 16.0f;
    }

    const glm::vec3 direction = glm::normalize(lightDirection);
    const glm::vec3 worldUp = std::abs(glm::dot(direction, glm::vec3(0, 1, 0))) > 0.98f
                                  ? glm::vec3(0, 0, 1)
                                  : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = FMath::lookAt(center - direction * (radius * 2.0f), center, worldUp);

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : corners) {
        const glm::vec3 lightSpace = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        minimum                    = glm::min(minimum, lightSpace);
        maximum                    = glm::max(maximum, lightSpace);
    }

    if (bStableFit) {
        glm::vec2 centerXY = glm::vec2(lightView * glm::vec4(center, 1.0f));
        const float texelSize = (radius * 2.0f) / static_cast<float>(std::max(1u, shadowResolution));
        centerXY             = glm::floor(centerXY / texelSize) * texelSize;
        minimum.x            = centerXY.x - radius;
        maximum.x            = centerXY.x + radius;
        minimum.y            = centerXY.y - radius;
        maximum.y            = centerXY.y + radius;
    }

    const float zMultiplier = std::max(depthRangeMultiplier, 1.0f);
    const float baseNear     = std::max(0.1f, -maximum.z);
    const float baseFar      = std::max(baseNear + 1.0f, -minimum.z);
    const float nearPlane    = std::max(0.1f, baseNear / zMultiplier);
    const float farPlane     = std::max(nearPlane + 1.0f, baseFar * zMultiplier);
    const glm::mat4 lightProjection = FMath::orthographic(
        minimum.x, maximum.x, minimum.y, maximum.y, nearPlane, farPlane);
    return lightProjection * lightView;
}

} // namespace

CascadeData buildCascades(const glm::vec3& lightDirection,
                          const glm::mat4& cameraView,
                          const glm::mat4& cameraProjection,
                          float            shadowDistance,
                          uint32_t         shadowResolution,
                          uint32_t         cascadeCount,
                          bool             bStableFit,
                          const std::array<float, MAX_DIRECTIONAL_CASCADES - 1>& splitRatios,
                          float depthRangeMultiplier)
{
    CascadeData result{};
    result.count = std::clamp(cascadeCount, 1u, static_cast<uint32_t>(MAX_DIRECTIONAL_CASCADES));

    const glm::mat4 inverseViewProjection = glm::inverse(cameraProjection * cameraView);
    std::array<glm::vec3, 4> cameraNearCorners{};
    std::array<glm::vec3, 4> cameraFarCorners{};
    uint32_t cornerIndex = 0;
    for (float y : {-1.0f, 1.0f}) {
        for (float x : {-1.0f, 1.0f}) {
            cameraNearCorners[cornerIndex] = unproject(inverseViewProjection, x, y, 0.0f);
            cameraFarCorners[cornerIndex]  = unproject(inverseViewProjection, x, y, 1.0f);
            ++cornerIndex;
        }
    }

    const float cameraNear = std::max(
        0.01f,
        std::abs((cameraView * glm::vec4(cameraNearCorners[0], 1.0f)).z));
    const float cameraFar = std::max(
        cameraNear + 0.1f,
        std::abs((cameraView * glm::vec4(cameraFarCorners[0], 1.0f)).z));
    const float effectiveFar = std::clamp(shadowDistance, cameraNear + 0.1f, cameraFar);

    constexpr float MIN_SPLIT_GAP = 0.001f;
    float previousRatio = 0.0f;
    for (uint32_t cascadeIndex = 0; cascadeIndex + 1 < result.count; ++cascadeIndex) {
        const float maxRatio = 1.0f - MIN_SPLIT_GAP * static_cast<float>(result.count - cascadeIndex - 1);
        const float ratio = std::clamp(
            splitRatios[cascadeIndex], previousRatio + MIN_SPLIT_GAP, maxRatio);
        result.splits[cascadeIndex] = cameraNear + (effectiveFar - cameraNear) * ratio;
        previousRatio = ratio;
    }
    result.splits[result.count - 1] = effectiveFar;

    float splitNear = cameraNear;
    for (uint32_t cascadeIndex = 0; cascadeIndex < result.count; ++cascadeIndex) {
        const float splitFar = result.splits[cascadeIndex];
        const float nearFactor = (splitNear - cameraNear) / (cameraFar - cameraNear);
        const float farFactor  = (splitFar - cameraNear) / (cameraFar - cameraNear);

        std::array<glm::vec3, 8> cascadeCorners{};
        for (uint32_t corner = 0; corner < cameraNearCorners.size(); ++corner) {
            const glm::vec3 ray = cameraFarCorners[corner] - cameraNearCorners[corner];
            cascadeCorners[corner]     = cameraNearCorners[corner] + ray * nearFactor;
            cascadeCorners[corner + 4] = cameraNearCorners[corner] + ray * farFactor;
        }

        result.viewProjections[cascadeIndex] = buildCascadeViewProjection(
            cascadeCorners,
            lightDirection,
            shadowResolution,
            bStableFit,
            depthRangeMultiplier);
        splitNear = splitFar;
    }

    return result;
}

} // namespace ya::DirectionalShadowMath

#include "Render3D/Common/CameraFrustumOverlay.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace ya
{

namespace
{

glm::vec3 unprojectClip(const glm::mat4& inverseViewProjection, const glm::vec3& clip)
{
    const glm::vec4 world = inverseViewProjection * glm::vec4(clip, 1.0f);
    if (std::abs(world.w) <= 1e-8f) {
        return glm::vec3(world);
    }
    return glm::vec3(world) / world.w;
}

glm::vec3 pointAlongFovRay(const glm::vec3& eye, const glm::vec3& ndcNear, float distance)
{
    glm::vec3 dir = ndcNear - eye;
    const float length = glm::length(dir);
    if (length <= 1e-8f || !std::isfinite(length)) {
        dir = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    else {
        dir /= length;
    }
    return eye + dir * distance;
}

void appendLine(std::vector<RenderOverlayLine3D>& lines,
                const glm::vec3&                  from,
                const glm::vec3&                  to,
                const glm::vec4&                  color)
{
    lines.push_back(RenderOverlayLine3D{
        .from  = from,
        .to    = to,
        .color = color,
    });
}

} // namespace

void appendCameraFrustumOverlayLines(std::vector<RenderOverlayLine3D>& lines,
                                     const glm::mat4&                  view,
                                     const glm::mat4&                  projection,
                                     const glm::vec4&                  color,
                                     float                             visualDepth)
{
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    const glm::vec3 clipNear00 = unprojectClip(inverseViewProjection, {-1.0f, -1.0f, 0.0f});
    const glm::vec3 clipNear10 = unprojectClip(inverseViewProjection, {1.0f, -1.0f, 0.0f});
    const glm::vec3 clipNear11 = unprojectClip(inverseViewProjection, {1.0f, 1.0f, 0.0f});
    const glm::vec3 clipNear01 = unprojectClip(inverseViewProjection, {-1.0f, 1.0f, 0.0f});

    const glm::mat4 inverseView = glm::inverse(view);
    const glm::vec3 eye         = glm::vec3(inverseView[3]);
    const float     farLen      = std::max(visualDepth, 0.05f);
    const float     nearLen     = farLen * 0.2f;

    const glm::vec3 n00 = pointAlongFovRay(eye, clipNear00, nearLen);
    const glm::vec3 n10 = pointAlongFovRay(eye, clipNear10, nearLen);
    const glm::vec3 n11 = pointAlongFovRay(eye, clipNear11, nearLen);
    const glm::vec3 n01 = pointAlongFovRay(eye, clipNear01, nearLen);
    const glm::vec3 f00 = pointAlongFovRay(eye, clipNear00, farLen);
    const glm::vec3 f10 = pointAlongFovRay(eye, clipNear10, farLen);
    const glm::vec3 f11 = pointAlongFovRay(eye, clipNear11, farLen);
    const glm::vec3 f01 = pointAlongFovRay(eye, clipNear01, farLen);

    appendLine(lines, n00, n10, color);
    appendLine(lines, n10, n11, color);
    appendLine(lines, n11, n01, color);
    appendLine(lines, n01, n00, color);

    appendLine(lines, f00, f10, color);
    appendLine(lines, f10, f11, color);
    appendLine(lines, f11, f01, color);
    appendLine(lines, f01, f00, color);

    appendLine(lines, n00, f00, color);
    appendLine(lines, n10, f10, color);
    appendLine(lines, n11, f11, color);
    appendLine(lines, n01, f01, color);

    appendLine(lines, eye, n00, color);
    appendLine(lines, eye, n10, color);
    appendLine(lines, eye, n11, color);
    appendLine(lines, eye, n01, color);
}

} // namespace ya

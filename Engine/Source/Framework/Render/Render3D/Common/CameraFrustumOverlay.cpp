#include "Render3D/Common/CameraFrustumOverlay.h"

#include <algorithm>
#include <cmath>
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

Rect2D makeBottomRightViewInset(const glm::vec2& hostExtent, float widthFraction, float marginFraction)
{
    if (hostExtent.x <= 1.0f || hostExtent.y <= 1.0f) {
        return {};
    }

    const float safeWidthFraction  = std::clamp(widthFraction, 0.05f, 0.5f);
    const float safeMarginFraction = std::clamp(marginFraction, 0.0f, 0.2f);
    const float margin             = std::max(hostExtent.x, hostExtent.y) * safeMarginFraction;
    float       width              = hostExtent.x * safeWidthFraction;
    float       height             = width * (hostExtent.y / hostExtent.x);
    if (height + margin * 2.0f > hostExtent.y) {
        height = std::max(1.0f, hostExtent.y - margin * 2.0f);
        width  = height * (hostExtent.x / hostExtent.y);
    }
    width  = std::max(1.0f, std::min(width, hostExtent.x - margin));
    height = std::max(1.0f, std::min(height, hostExtent.y - margin));

    return Rect2D{
        .pos    = {hostExtent.x - margin - width, hostExtent.y - margin - height},
        .extent = {width, height},
    };
}

void appendCameraFrustumOverlayLines(std::vector<RenderOverlayLine3D>& lines,
                                     const glm::mat4&                  view,
                                     const glm::mat4&                  projection,
                                     const glm::vec4&                  color)
{
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    // perspectiveRH_ZO: clip Z is 0 at near, 1 at far.
    const glm::vec3 n00 = unprojectClip(inverseViewProjection, {-1.0f, -1.0f, 0.0f});
    const glm::vec3 n10 = unprojectClip(inverseViewProjection, {1.0f, -1.0f, 0.0f});
    const glm::vec3 n11 = unprojectClip(inverseViewProjection, {1.0f, 1.0f, 0.0f});
    const glm::vec3 n01 = unprojectClip(inverseViewProjection, {-1.0f, 1.0f, 0.0f});
    const glm::vec3 f00 = unprojectClip(inverseViewProjection, {-1.0f, -1.0f, 1.0f});
    const glm::vec3 f10 = unprojectClip(inverseViewProjection, {1.0f, -1.0f, 1.0f});
    const glm::vec3 f11 = unprojectClip(inverseViewProjection, {1.0f, 1.0f, 1.0f});
    const glm::vec3 f01 = unprojectClip(inverseViewProjection, {-1.0f, 1.0f, 1.0f});

    const glm::mat4 inverseView = glm::inverse(view);
    const glm::vec3 eye         = glm::vec3(inverseView[3]);

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

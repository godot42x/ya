#include "Core/Math/Math.h"
#include "Render3D/Common/CameraFrustumOverlay.h"

#include <cmath>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <vector>

namespace ya
{
namespace
{

TEST(CameraFrustumOverlayTest, FrustumGizmoIsCompactAndFollowsFov)
{
    const glm::mat4 view       = glm::mat4(1.0f);
    const glm::mat4 projection = FMath::perspective(glm::radians(90.0f), 1.0f, 0.1f, 1000.0f);
    const glm::vec4 color{0.2f, 0.8f, 1.0f, 1.0f};
    constexpr float kVisualDepth = 1.0f;

    std::vector<RenderOverlayLine3D> lines;
    appendCameraFrustumOverlayLines(lines, view, projection, color, kVisualDepth);

    ASSERT_EQ(lines.size(), 16u);
    for (const auto& line : lines) {
        EXPECT_EQ(line.color, color);
    }

    const glm::vec3 n00 = lines[0].from;
    const glm::vec3 n10 = lines[0].to;
    const glm::vec3 f00 = lines[4].from;
    const glm::vec3 eye = lines[12].from;
    EXPECT_NEAR(eye.x, 0.0f, 1e-4f);
    EXPECT_NEAR(eye.y, 0.0f, 1e-4f);
    EXPECT_NEAR(eye.z, 0.0f, 1e-4f);
    EXPECT_NEAR(glm::length(n00), kVisualDepth * 0.2f, 1e-4f);
    EXPECT_NEAR(glm::length(f00), kVisualDepth, 1e-4f);
    EXPECT_LT(glm::length(f00), 2.0f);
    EXPECT_GT(glm::length(f00), glm::length(n00));
    EXPECT_NEAR(std::abs(n00.x / n00.z), 1.0f, 1e-4f);
    EXPECT_NEAR(std::abs(n10.x / n10.z), 1.0f, 1e-4f);
    EXPECT_EQ(lines[12].to, n00);
}

} // namespace
} // namespace ya

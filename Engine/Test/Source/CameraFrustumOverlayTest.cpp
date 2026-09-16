#include "Core/Math/Math.h"
#include "Render3D/Common/CameraFrustumOverlay.h"

#include <gtest/gtest.h>
#include <vector>

namespace ya
{
namespace
{

TEST(CameraFrustumOverlayTest, BottomRightInsetKeepsHostAspectInsideHost)
{
    const Rect2D empty = makeBottomRightViewInset({0.0f, 720.0f});
    EXPECT_FLOAT_EQ(empty.extent.x, 0.0f);
    EXPECT_FLOAT_EQ(empty.extent.y, 0.0f);

    const Rect2D inset = makeBottomRightViewInset({1280.0f, 720.0f}, 0.28f, 0.02f);
    EXPECT_GT(inset.extent.x, 0.0f);
    EXPECT_GT(inset.extent.y, 0.0f);
    EXPECT_FLOAT_EQ(inset.extent.x / inset.extent.y, 1280.0f / 720.0f);
    EXPECT_GE(inset.pos.x, 0.0f);
    EXPECT_GE(inset.pos.y, 0.0f);
    EXPECT_LE(inset.pos.x + inset.extent.x, 1280.0f);
    EXPECT_LE(inset.pos.y + inset.extent.y, 720.0f);
    EXPECT_GT(inset.pos.x, 1280.0f * 0.5f);
    EXPECT_GT(inset.pos.y, 720.0f * 0.5f);
}

TEST(CameraFrustumOverlayTest, FrustumLinesUnprojectNearPlaneOfRhZoPerspective)
{
    const glm::mat4 view       = glm::mat4(1.0f);
    const glm::mat4 projection = FMath::perspective(glm::radians(90.0f), 1.0f, 1.0f, 2.0f);
    const glm::vec4 color{0.2f, 0.8f, 1.0f, 1.0f};

    std::vector<RenderOverlayLine3D> lines;
    appendCameraFrustumOverlayLines(lines, view, projection, color);

    ASSERT_EQ(lines.size(), 16u);
    for (const auto& line : lines) {
        EXPECT_EQ(line.color, color);
    }

    // Identity view + 90deg 1:1 RH_ZO: near half-extent is tan(45)*near = 1.
    const glm::vec3 n00 = lines[0].from;
    const glm::vec3 n10 = lines[0].to;
    EXPECT_NEAR(n00.x, -1.0f, 1e-4f);
    EXPECT_NEAR(n00.y, -1.0f, 1e-4f);
    EXPECT_NEAR(n00.z, -1.0f, 1e-4f);
    EXPECT_NEAR(n10.x, 1.0f, 1e-4f);
    EXPECT_NEAR(n10.y, -1.0f, 1e-4f);
    EXPECT_NEAR(n10.z, -1.0f, 1e-4f);

    const glm::vec3 eye = lines[12].from;
    EXPECT_NEAR(eye.x, 0.0f, 1e-4f);
    EXPECT_NEAR(eye.y, 0.0f, 1e-4f);
    EXPECT_NEAR(eye.z, 0.0f, 1e-4f);
    EXPECT_EQ(lines[12].to, n00);
}

} // namespace
} // namespace ya

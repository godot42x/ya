#include "Core/Math/Math.h"
#include "Render3D/Shadow/ShadowSettings.h"
#include "Render3D/Common/Shadow/Common/DirectionalShadowMath.h"

#include <gtest/gtest.h>

#include <cmath>

namespace ya
{
namespace
{

bool isFinite(const glm::mat4& matrix)
{
    for (uint32_t column = 0; column < 4; ++column) {
        for (uint32_t row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) return false;
        }
    }
    return true;
}

TEST(DirectionalShadowMathTest, BuildsMonotonicFiniteCascades)
{
    const glm::mat4 view = FMath::lookAt(
        glm::vec3(0.0f, 2.0f, 8.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = FMath::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 200.0f);

    const auto cascades = DirectionalShadowMath::buildCascades(
        glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f)),
        view,
        projection,
        80.0f,
        2048,
        4,
        true,
        {0.02f, 0.1f, 0.5f},
        10.0f);

    EXPECT_EQ(cascades.count, 4u);
    EXPECT_LT(cascades.splits[0], cascades.splits[1]);
    EXPECT_LT(cascades.splits[1], cascades.splits[2]);
    EXPECT_LT(cascades.splits[2], cascades.splits[3]);
    EXPECT_NEAR(cascades.splits[0], 0.1f + (80.0f - 0.1f) * 0.02f, 0.01f);
    EXPECT_NEAR(cascades.splits[1], 0.1f + (80.0f - 0.1f) * 0.1f, 0.01f);
    EXPECT_NEAR(cascades.splits[2], 0.1f + (80.0f - 0.1f) * 0.5f, 0.01f);
    EXPECT_NEAR(cascades.splits[3], 80.0f, 0.001f);
    for (uint32_t cascadeIndex = 0; cascadeIndex < cascades.count; ++cascadeIndex) {
        EXPECT_TRUE(isFinite(cascades.viewProjections[cascadeIndex]));
    }
}

TEST(DirectionalShadowMathTest, OneCascadeKeepsLegacyModeSelectable)
{
    ShadowSettings settings = ShadowSettings::fromQuality(EShadowQuality::Medium);
    settings.directionalCascades = 1;
    EXPECT_EQ(settings.getEffectiveDirectionalCascadeCount(), 1u);

    settings.directionalCascades = MAX_DIRECTIONAL_CASCADES + 1;
    EXPECT_EQ(settings.getEffectiveDirectionalCascadeCount(), MAX_DIRECTIONAL_CASCADES);
}

TEST(DirectionalShadowMathTest, SanitizesConfiguredSplitRatios)
{
    ShadowSettings settings;
    settings.directionalCascadeSplitRatios = {0.8f, 0.2f, 2.0f};
    settings.sanitizeDirectionalCascadeSplitRatios();

    EXPECT_LT(settings.directionalCascadeSplitRatios[0], settings.directionalCascadeSplitRatios[1]);
    EXPECT_LT(settings.directionalCascadeSplitRatios[1], settings.directionalCascadeSplitRatios[2]);
    EXPECT_LT(settings.directionalCascadeSplitRatios[2], 1.0f);
}

} // namespace
} // namespace ya

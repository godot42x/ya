#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <cmath>

#include <gtest/gtest.h>

namespace ya
{
namespace
{

bool matricesNear(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > eps) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST(CameraProjection, PerspectiveMatchesThePreviousMatrix)
{
    CameraComponent camera;
    const float     aspect = 16.0f / 9.0f;
    const glm::mat4 expected = FMath::perspective(glm::radians(camera._fov), aspect, camera._nearClip, camera._farClip);
    EXPECT_TRUE(matricesNear(camera.getProjection(aspect), expected));
}

TEST(CameraProjection, FixedAspectIgnoresTheOutputAspect)
{
    CameraComponent camera;
    camera._fixedAspectRatio = true;
    camera._aspectRatio      = 2.0f;
    const glm::mat4 expected = FMath::perspective(glm::radians(camera._fov), 2.0f, camera._nearClip, camera._farClip);
    EXPECT_TRUE(matricesNear(camera.getProjection(1.0f), expected));
}

TEST(CameraProjection, OrthographicHeightIsIndependentOfAspect)
{
    CameraComponent camera;
    camera._projection       = ECameraProjection::Orthographic;
    camera._orthoHalfHeight  = 5.0f;
    const glm::mat4 wide     = camera.getProjection(2.0f);
    const glm::mat4 narrow   = camera.getProjection(0.5f);
    const glm::mat4 expectedWide = FMath::orthographic(-10.0f, 10.0f, -5.0f, 5.0f, camera._nearClip, camera._farClip);
    const glm::mat4 expectedNarrow = FMath::orthographic(-2.5f, 2.5f, -5.0f, 5.0f, camera._nearClip, camera._farClip);
    EXPECT_TRUE(matricesNear(wide, expectedWide));
    EXPECT_TRUE(matricesNear(narrow, expectedNarrow));
}

TEST(CameraProjection, ProjectionDoesNotReadOrWriteTheOwner)
{
    Scene scene("CameraProjectionScene");
    auto* node = scene.createNode3D("Camera", scene.getRootNode());
    ASSERT_NE(node, nullptr);
    Entity* entity = node->getEntity();
    auto*   camera = entity->addComponent<CameraComponent>();
    auto*   transform = entity->getComponent<TransformComponent>();
    ASSERT_NE(camera, nullptr);
    ASSERT_NE(transform, nullptr);
    transform->setPosition({4.0f, 2.0f, 8.0f});
    transform->setRotation({0.0f, 15.0f, 0.0f});
    TransformSystem::computeWorldMatrix(transform);
    const glm::vec3 positionBefore = transform->getPosition();

    const glm::mat4 projection = camera->getProjection(1.5f);
    EXPECT_NE(projection, glm::mat4(1.0f));
    EXPECT_EQ(transform->getPosition(), positionBefore);

    const glm::mat4 view = cameraView(*entity);
    EXPECT_EQ(transform->getPosition(), positionBefore);
    const glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
    EXPECT_NEAR(glm::length(eye - glm::vec3(4.0f, 2.0f, 8.0f)), 0.0f, 1e-3f);
}

} // namespace ya

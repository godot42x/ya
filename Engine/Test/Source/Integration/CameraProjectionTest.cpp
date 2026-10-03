#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Scene/Core/Scene.h"
#include "Scene/Serialization/SceneSerializer.h"
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

CameraComponent pixelPerfectCamera()
{
    CameraComponent camera;
    camera._projection        = ECameraProjection::Orthographic;
    camera._orthoHalfHeight   = 8.0f;
    camera._pixelPerfect      = true;
    camera._pixelsPerUnit     = 16.0f;
    camera._referenceHeightPx = 192.0f;
    camera._nearClip          = 0.1f;
    camera._farClip           = 100.0f;
    return camera;
}

void expectIntegerTexelScale(const CameraViewFraming& framing, glm::vec2 extent, float pixelsPerUnit)
{
    ASSERT_GE(framing.zoom, 1);
    ASSERT_GT(framing.halfHeight, 0.0f);
    ASSERT_GT(framing.halfWidth, 0.0f);
    const float vertical = extent.y / (2.0f * framing.halfHeight * pixelsPerUnit);
    const float horizontal = extent.x / (2.0f * framing.halfWidth * pixelsPerUnit);
    EXPECT_NEAR(vertical, static_cast<float>(framing.zoom), 1e-4f);
    EXPECT_NEAR(horizontal, static_cast<float>(framing.zoom), 1e-4f);
    EXPECT_NEAR(vertical, std::round(vertical), 1e-4f);
    EXPECT_GE(vertical, 1.0f);
}

TEST(CameraProjection, PixelPerfectOffIgnoresThePixelHeight)
{
    CameraComponent camera;
    camera._projection      = ECameraProjection::Orthographic;
    camera._orthoHalfHeight = 5.0f;
    const glm::vec2 extent(1440.0f, 720.0f);
    const glm::mat4 fromAspect = camera.getProjection(2.0f);
    const glm::mat4 fromExtent = camera.getProjection(extent);
    EXPECT_TRUE(matricesNear(fromAspect, fromExtent));

    const CameraViewFraming framing = resolveCameraViewFraming(camera, 2.0f, extent);
    EXPECT_EQ(framing.zoom, 0);
    EXPECT_FLOAT_EQ(framing.snapStep, 0.0f);
    EXPECT_NEAR(framing.halfHeight, 5.0f, 1e-5f);
    EXPECT_NEAR(framing.halfWidth, 10.0f, 1e-5f);
    const glm::vec2 reported = scriptViewSize(&camera, extent);
    EXPECT_NEAR(reported.x, extent.x, 1e-5f);
    EXPECT_NEAR(reported.y, extent.y, 1e-5f);
}

TEST(CameraProjection, PixelPerfectZoomTableMatchesTheFrustum)
{
    const CameraComponent camera = pixelPerfectCamera();
    const struct
    {
        float width;
        float height;
        int   zoom;
        float halfHeight;
    } rows[] = {
        {1280.0f, 720.0f, 3, 7.5f},
        {700.0f, 500.0f, 2, 500.0f / 64.0f},
        {596.0f, 419.0f, 2, 419.0f / 64.0f},
        {500.0f, 350.0f, 1, 350.0f / 32.0f},
        {100.0f, 80.0f, 1, 80.0f / 32.0f},
        {400.0f, 191.0f, 1, 191.0f / 32.0f},
    };

    for (const auto& row : rows) {
        const glm::vec2 extent(row.width, row.height);
        const float aspect = row.width / row.height;
        const CameraViewFraming framing = resolveCameraViewFraming(camera, aspect, extent);
        EXPECT_EQ(framing.zoom, row.zoom) << row.width << "x" << row.height;
        EXPECT_NEAR(framing.halfHeight, row.halfHeight, 1e-4f) << row.width << "x" << row.height;
        EXPECT_NEAR(framing.halfWidth, row.halfHeight * aspect, 1e-4f);
        expectIntegerTexelScale(framing, extent, camera._pixelsPerUnit);

        const glm::mat4 projection = camera.getProjection(extent);
        const glm::mat4 expected = FMath::orthographic(-framing.halfWidth,
                                                       framing.halfWidth,
                                                       -framing.halfHeight,
                                                       framing.halfHeight,
                                                       camera._nearClip,
                                                       camera._farClip);
        EXPECT_TRUE(matricesNear(projection, expected)) << row.width << "x" << row.height;

        const glm::vec2 reported = scriptViewSize(&camera, extent);
        EXPECT_NEAR(reported.x, framing.halfWidth, 1e-4f);
        EXPECT_NEAR(reported.y, framing.halfHeight, 1e-4f);
        EXPECT_NEAR(scriptViewAspect(&camera, extent), framing.halfWidth / framing.halfHeight, 1e-5f);
        EXPECT_NEAR(scriptViewAspect(&camera, extent), aspect, 1e-5f);
    }
}

TEST(CameraProjection, PixelPerfectZoomFollowsDevicePixelHeight)
{
    const CameraComponent camera = pixelPerfectCamera();
    const CameraViewFraming logical = resolveCameraViewFraming(camera, 349.0f / 197.0f, {349.0f, 197.0f});
    const CameraViewFraming device  = resolveCameraViewFraming(camera, 349.0f / 197.0f, {698.0f, 394.0f});
    EXPECT_EQ(logical.zoom, 1);
    EXPECT_EQ(device.zoom, 2);
    // Zoom doubled with the pixel height, so the world frustum is unchanged
    // and one texel covers two device pixels.
    EXPECT_NEAR(device.halfHeight, logical.halfHeight, 1e-4f);
    EXPECT_NEAR(device.halfHeight, 394.0f / (2.0f * 16.0f * 2.0f), 1e-4f);
}

TEST(CameraProjection, PixelPerfectSnapsTheEyeAndLeavesTheTransform)
{
    Scene scene("PixelSnapScene");
    auto* node = scene.createNode3D("Camera", scene.getRootNode());
    ASSERT_NE(node, nullptr);
    Entity* entity = node->getEntity();
    auto*   camera = entity->addComponent<CameraComponent>();
    auto*   transform = entity->getComponent<TransformComponent>();
    ASSERT_NE(camera, nullptr);
    ASSERT_NE(transform, nullptr);
    camera->_projection        = ECameraProjection::Orthographic;
    camera->_orthoHalfHeight   = 8.0f;
    camera->_pixelPerfect      = true;
    camera->_pixelsPerUnit     = 16.0f;
    camera->_referenceHeightPx = 192.0f;
    camera->_nearClip          = 0.1f;
    camera->_farClip           = 100.0f;
    transform->setPosition({1.03f, -2.2f, 8.0f});

    const glm::vec2 extent(596.0f, 419.0f);
    const CameraRenderMatrices frame = buildCameraRenderMatrices(*camera, entity, extent);
    EXPECT_EQ(transform->getPosition(), glm::vec3(1.03f, -2.2f, 8.0f));
    EXPECT_EQ(frame.framing.zoom, 2);
    EXPECT_FLOAT_EQ(frame.framing.snapPhaseX, 0.0f);
    EXPECT_NEAR(frame.framing.snapPhaseY, 0.5f * frame.framing.snapStep, 1e-6f);

    const glm::vec3 snapped = snapWorldXY(glm::vec3(1.03f, -2.2f, 8.0f),
                                          frame.framing.snapStep,
                                          frame.framing.snapPhaseX,
                                          frame.framing.snapPhaseY);
    const glm::vec3 eye = glm::vec3(glm::inverse(frame.view)[3]);
    EXPECT_NEAR(eye.x, snapped.x, 1e-4f);
    EXPECT_NEAR(eye.y, snapped.y, 1e-4f);
    EXPECT_NEAR(eye.z, 8.0f, 1e-3f);
    EXPECT_NEAR(glm::length(frame.cameraPos - eye), 0.0f, 1e-4f);

    // Texel grid vs device-pixel eye. An even extent puts the edge on a whole
    // pixel (integer). An odd extent's half-pixel phase puts it on a pixel edge
    // too, which is a half-integer in device pixels — 0.5, not a whole pixel.
    const glm::vec3 sprite = snapWorldXY(glm::vec3(1.03f, -2.2f, 0.0f), 1.0f / 16.0f, 0.0f, 0.0f);
    // 1/(pixelsPerUnit*zoom) is not always exact in float, so a whole pixel can
    // land at 0.999998. Fold that back onto the expected residue.
    const auto distanceToResidue = [](float value, float residue) {
        float wrapped = value - std::floor(value);
        float delta   = wrapped - residue;
        if (delta > 0.5f) {
            delta -= 1.0f;
        }
        if (delta < -0.5f) {
            delta += 1.0f;
        }
        return delta;
    };
    const float dx = (sprite.x - eye.x) / frame.framing.snapStep;
    const float dy = (sprite.y - eye.y) / frame.framing.snapStep;
    EXPECT_NEAR(distanceToResidue(dx, 0.0f), 0.0f, 1e-3f);
    EXPECT_NEAR(distanceToResidue(dy, 0.5f), 0.0f, 1e-3f);

    const CameraRenderMatrices even = buildCameraRenderMatrices(*camera, entity, glm::vec2(1280.0f, 720.0f));
    const glm::vec3 evenEye = glm::vec3(glm::inverse(even.view)[3]);
    const float evenDx = (sprite.x - evenEye.x) / even.framing.snapStep;
    const float evenDy = (sprite.y - evenEye.y) / even.framing.snapStep;
    EXPECT_NEAR(distanceToResidue(evenDx, 0.0f), 0.0f, 1e-3f);
    EXPECT_NEAR(distanceToResidue(evenDy, 0.0f), 0.0f, 1e-3f);
    EXPECT_EQ(even.framing.zoom, 3);
    EXPECT_NEAR(even.framing.halfHeight, 7.5f, 1e-4f);
}

TEST(CameraProjection, PixelPerfectFieldsRoundTrip)
{
    static bool bReady = false;
    if (!bReady) {
        reflection::DeferredInitializerQueue::instance().executeAll();
        bReady = true;
    }

    Scene scene("PixelPerfectRoundTrip");
    auto* node = scene.createNode3D("Camera", scene.getRootNode());
    ASSERT_NE(node, nullptr);
    auto* camera = node->getEntity()->addComponent<CameraComponent>();
    ASSERT_NE(camera, nullptr);
    camera->_projection        = ECameraProjection::Orthographic;
    camera->_orthoHalfHeight   = 8.0f;
    camera->_pixelPerfect      = true;
    camera->_pixelsPerUnit     = 16.0f;
    camera->_referenceHeightPx = 192.0f;

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();
    const auto& componentJson = saved["entities"][0]["components"]["CameraComponent"];
    EXPECT_EQ(componentJson["_pixelPerfect"], true);
    EXPECT_FLOAT_EQ(componentJson["_pixelsPerUnit"].get<float>(), 16.0f);
    EXPECT_FLOAT_EQ(componentJson["_referenceHeightPx"].get<float>(), 192.0f);

    Scene loaded("PixelPerfectLoaded");
    SceneSerializer loadedSerializer(&loaded);
    loadedSerializer.deserialize(saved);
    auto* loadedCamera = loaded.getEntityByName("Camera")->getComponent<CameraComponent>();
    ASSERT_NE(loadedCamera, nullptr);
    EXPECT_TRUE(loadedCamera->_pixelPerfect);
    EXPECT_FLOAT_EQ(loadedCamera->_pixelsPerUnit, 16.0f);
    EXPECT_FLOAT_EQ(loadedCamera->_referenceHeightPx, 192.0f);
    EXPECT_EQ(loadedCamera->_projection, ECameraProjection::Orthographic);
}

} // namespace ya

#include "ECS/Component/TransformComponent.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Physics/PhysicsSystem.h"
#include "Render3D/Scene.h"
#include "Render3D/SceneManager.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

TransformComponent* findFirstPhysicsTransform(Scene& scene)
{
    for (auto&& [entity, transform, body] :
         scene.getRegistry().view<TransformComponent, PhysicsBodyComponent>().each()) {
        (void)entity;
        (void)body;
        return &transform;
    }
    return nullptr;
}

} // namespace

// Regression: leaving a play session must drop every Jolt body immediately,
// driven by SceneManager lifecycle events (not by polling in onUpdate). A
// restarted session clones a fresh scene that can reuse the previous scene's
// address and entity ids; without event-driven cleanup the old bodies (and
// their positions) would be picked up and objects would start from the last
// session's state.
TEST(PhysicsSystemTest, BodiesResetWhenPlaySessionStops)
{
    SceneManager sceneManager;

    auto     authoringScene = std::make_shared<Scene>("Authoring");
    Node3D* const node      = authoringScene->createNode3D("PhysicsBody");
    Entity* const entity    = node->getEntity();
    entity->addComponent<PhysicsBodyComponent>();
    auto* const authoringTc = entity->getComponent<TransformComponent>();
    ASSERT_NE(authoringTc, nullptr);
    authoringTc->setPosition({0.0f, 5.0f, 0.0f});
    sceneManager.activateScene(authoringScene);

    MulticastDelegate<void(AppState)> appStateEvents;

    PhysicsSystem system;
    system.setSceneManager(&sceneManager);
    system.setAppStateChangedSource(&appStateEvents);
    system.init();

    // --- Session 1 ---
    stdptr<Scene> playScene = sceneManager.cloneScene(authoringScene.get());
    ASSERT_NE(playScene, nullptr);
    sceneManager.activateScene(playScene);

    appStateEvents.broadcast(AppState::Runtime);
    system.onUpdate(1.0f / 60.0f);
    auto* playTc = findFirstPhysicsTransform(*playScene);
    ASSERT_NE(playTc, nullptr);
    EXPECT_NEAR(playTc->getPosition().y, 5.0f, 0.01f);

    for (int i = 0; i < 30; ++i) {
        system.onUpdate(1.0f / 60.0f);
    }
    EXPECT_LT(playTc->getPosition().y, 4.0f);
    EXPECT_NEAR(authoringTc->getPosition().y, 5.0f, 0.001f); // authoring untouched

    // --- Stop session 1 (mirrors EditorPlaySession::end) ---
    appStateEvents.broadcast(AppState::Stopped);
    sceneManager.activateScene(authoringScene); // onSceneActivated(authoring) -> bodies dropped
    sceneManager.destroyScene(playScene);       // onSceneDestroy(play)

    // --- Session 2: a fresh clone must start from the authoring transform ---
    stdptr<Scene> playScene2 = sceneManager.cloneScene(authoringScene.get());
    ASSERT_NE(playScene2, nullptr);
    sceneManager.activateScene(playScene2);

    appStateEvents.broadcast(AppState::Runtime);
    system.onUpdate(1.0f / 60.0f);
    auto* playTc2 = findFirstPhysicsTransform(*playScene2);
    ASSERT_NE(playTc2, nullptr);
    EXPECT_NEAR(playTc2->getPosition().x, 0.0f, 0.01f);
    EXPECT_NEAR(playTc2->getPosition().y, 5.0f, 0.01f); // NOT the previous session's y

    appStateEvents.broadcast(AppState::Stopped);
    sceneManager.activateScene(authoringScene);
    sceneManager.destroyScene(playScene2);

    system.shutdown();
}

} // namespace ya

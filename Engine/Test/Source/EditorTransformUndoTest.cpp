#include "GameEditor/UI/EditorTransformUndo.h"

#include "ECS/Component.h"
#include "GUI/Binding/UndoStack.h"
#include "ECS/Systems/TransformSystem.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorTransformUndoTest, RestoresTransformsByStableEntityUuid)
{
    Scene scene("Undo");
    Node* node = scene.createNode3D("Cube");
    ASSERT_NE(node, nullptr);
    Entity* entity = node->getEntity();
    ASSERT_NE(entity, nullptr);
    auto* id = entity->getComponent<IDComponent>();
    auto* transform = entity->getComponent<TransformComponent>();
    ASSERT_NE(id, nullptr);
    ASSERT_NE(transform, nullptr);

    UndoStack undo;
    const uint64_t uuid = id->_id.value;
    TransformSystem::computeWorldMatrix(transform);
    const glm::mat4 before = transform->getTransform();
    TransformSystem::setWorldTransform(transform, glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 5.0f, 6.0f)));
    const glm::mat4 after = transform->getTransform();
    ASSERT_TRUE(pushEditorTransformUndo(undo, &scene, {{uuid, before}}, {{uuid, after}}));

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(transform->getPosition(), glm::vec3(0.0f));
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(transform->getPosition(), glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST(EditorTransformUndoTest, CaptureSelectionSkipsEntitiesWithoutStableTransformIdentity)
{
    Scene scene("Capture");
    Node* valid = scene.createNode3D("Valid");
    Node* plain = scene.createNode("Plain");
    ASSERT_NE(valid, nullptr);
    ASSERT_NE(plain, nullptr);

    auto snapshots = captureEditorTransformSelection({valid->getEntity(), plain->getEntity(), nullptr});
    ASSERT_EQ(snapshots.size(), 1u);
    EXPECT_EQ(snapshots.front().entityUUID, valid->getEntity()->getComponent<IDComponent>()->_id.value);
}

} // namespace ya

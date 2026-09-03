#include "GameEditor/UI/EditorHierarchyOps.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

uint64_t entityUuidOrFail(const Entity* entity)
{
    EXPECT_NE(entity, nullptr);
    if (!entity) {
        return 0;
    }
    const auto* id = entity->getComponent<IDComponent>();
    EXPECT_NE(id, nullptr);
    return id ? id->_id.value : 0;
}

} // namespace

TEST(EditorHierarchyOpsTest, MoveEntityBeforeSiblingReordersChildren)
{
    Scene scene("Test");
    Node* parent = scene.createNode("Parent");
    Node* first  = scene.createNode("First", parent);
    Node* second = scene.createNode("Second", parent);

    const std::string firstId  = editorHierarchyEntityIdKey(entityUuidOrFail(first->getEntity()));
    const std::string secondId = editorHierarchyEntityIdKey(entityUuidOrFail(second->getEntity()));

    Entity* moved = moveEditorHierarchyEntity(scene, secondId, firstId, 0);
    ASSERT_EQ(moved, second->getEntity());
    ASSERT_EQ(parent->getChildCount(), 2u);
    EXPECT_EQ(parent->getChild(0), second);
    EXPECT_EQ(parent->getChild(1), first);
}

TEST(EditorHierarchyOpsTest, RejectsUiEntryIds)
{
    Scene scene("Test");
    Node* node = scene.createNode("Node");
    const std::string entityId = editorHierarchyEntityIdKey(entityUuidOrFail(node->getEntity()));

    EXPECT_EQ(moveEditorHierarchyEntity(scene, "ui:entry", entityId, 1), nullptr);
    EXPECT_EQ(moveEditorHierarchyEntity(scene, entityId, "ui-root", 1), nullptr);
}

} // namespace ya

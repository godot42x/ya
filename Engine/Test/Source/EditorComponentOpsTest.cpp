#include "GameEditor/UI/Ops/EditorComponentOps.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorComponentOpsTest, IdentityComponentsAreNotAddableOrRemovable)
{
    EXPECT_TRUE(isIdentityAuthoringComponent("IDComponent"));
    EXPECT_TRUE(isIdentityAuthoringComponent("TransformComponent"));
    EXPECT_TRUE(isIdentityAuthoringComponent("ManagedChildComponent"));
    EXPECT_TRUE(isIdentityAuthoringComponent("ScriptComponent"));
    EXPECT_FALSE(isIdentityAuthoringComponent("CameraComponent"));
    EXPECT_FALSE(isIdentityAuthoringComponent("LuaScriptComponent"));

    Scene scene("Authoring");
    Node* node = scene.createNode3D("Empty");
    ASSERT_NE(node, nullptr);
    Entity* entity = node->getEntity();
    ASSERT_NE(entity, nullptr);

    EXPECT_TRUE(canMutateAuthoringComponents(*entity));
    EXPECT_FALSE(canAddAuthoringComponent(*entity, type_index_v<TransformComponent>));
    EXPECT_FALSE(canRemoveAuthoringComponent(*entity, type_index_v<TransformComponent>));
    EXPECT_FALSE(canRemoveAuthoringComponent(*entity, type_index_v<IDComponent>));
}

TEST(EditorComponentOpsTest, AddRemoveCameraAndLuaOnAuthorEntity)
{
    Scene scene("Authoring");
    Node* node = scene.createNode3D("Empty");
    ASSERT_NE(node, nullptr);
    Entity* entity = node->getEntity();
    ASSERT_NE(entity, nullptr);

    EXPECT_TRUE(canAddAuthoringComponent(*entity, type_index_v<CameraComponent>));
    ASSERT_TRUE(addAuthoringComponent({entity}, type_index_v<CameraComponent>));
    EXPECT_TRUE(entity->hasComponent<CameraComponent>());
    EXPECT_FALSE(canAddAuthoringComponent(*entity, type_index_v<CameraComponent>));
    EXPECT_TRUE(canRemoveAuthoringComponent(*entity, type_index_v<CameraComponent>));
    ASSERT_TRUE(removeAuthoringComponent({entity}, type_index_v<CameraComponent>));
    EXPECT_FALSE(entity->hasComponent<CameraComponent>());

    EXPECT_TRUE(canAddAuthoringComponent(*entity, type_index_v<LuaScriptComponent>));
    ASSERT_TRUE(addAuthoringComponent({entity}, type_index_v<LuaScriptComponent>));
    EXPECT_TRUE(entity->hasComponent<LuaScriptComponent>());
    ASSERT_TRUE(removeAuthoringComponent({entity}, type_index_v<LuaScriptComponent>));
    EXPECT_FALSE(entity->hasComponent<LuaScriptComponent>());
}

TEST(EditorComponentOpsTest, InstanceChildCannotMutateComponentSet)
{
    Scene scene("Instance");
    Node* root = scene.createNode3D("Model");
    Node* mesh = scene.createNode3D("Mesh_0", root);
    ASSERT_NE(mesh, nullptr);
    Entity* child = mesh->getEntity();
    ASSERT_NE(child, nullptr);
    child->addComponent<ManagedChildComponent>();

    EXPECT_FALSE(canMutateAuthoringComponents(*child));
    EXPECT_FALSE(canAddAuthoringComponent(*child, type_index_v<CameraComponent>));
    EXPECT_FALSE(canRemoveAuthoringComponent(*child, type_index_v<TransformComponent>));
}

TEST(EditorComponentOpsTest, AuthoringMenuOmitsIdentityTypes)
{
    const auto types = authoringComponentTypes();
    for (const auto& [name, type] : types) {
        EXPECT_FALSE(isIdentityAuthoringComponent(name)) << name;
        EXPECT_NE(type, 0u);
    }
}

} // namespace ya

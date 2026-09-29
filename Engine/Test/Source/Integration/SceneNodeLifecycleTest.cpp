#include "ECS/Component.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Hierarchy/Node.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

/// Scene stores entities by value and recycles nodes through the node map, so a
/// raw Entity*/Node* must never be touched after the entity is destroyed. These
/// tests therefore keep handles and re-query the scene.
entt::entity handleOf(Node* node)
{
    EXPECT_NE(node, nullptr);
    Entity* entity = node ? node->getEntity() : nullptr;
    EXPECT_NE(entity, nullptr);
    return entity ? entity->getHandle() : entt::null;
}

void markAsInstanceChild(Node* node)
{
    Entity* entity = node ? node->getEntity() : nullptr;
    ASSERT_NE(entity, nullptr);
    entity->addComponent<ManagedChildComponent>();
}

} // namespace

TEST(SceneNodeLifecycleTest, DestroyingAnEntityTakesItsWholeSubtree)
{
    Scene scene("Test");
    Node* parent = scene.createNode("Parent");
    Node* child  = scene.createNode("Child", parent);
    Node* grand  = scene.createNode("Grandchild", child);

    const entt::entity parentHandle = handleOf(parent);
    const entt::entity childHandle  = handleOf(child);
    const entt::entity grandHandle  = handleOf(grand);
    const uint32_t     before       = scene.entityCount();
    ASSERT_GE(before, 4u); // synthetic scene root + the three nodes above

    scene.destroyEntity(scene.getEntityByEnttID(parentHandle));

    // Nothing may survive as a detached orphan: an orphan entity stays in the
    // registry (so render extraction keeps drawing it) while being unreachable
    // from the node tree, which makes it invisible to the Hierarchy and
    // unpickable.
    EXPECT_EQ(scene.entityCount(), before - 3);
    EXPECT_EQ(scene.getEntityByEnttID(parentHandle), nullptr);
    EXPECT_EQ(scene.getEntityByEnttID(childHandle), nullptr);
    EXPECT_EQ(scene.getEntityByEnttID(grandHandle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(parentHandle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(childHandle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(grandHandle), nullptr);
}

TEST(SceneNodeLifecycleTest, DestroyingAModelRootRemovesEveryGeneratedMesh)
{
    Scene scene("Test");
    Node* model  = scene.createNode("Suzanne");
    Node* mesh0  = scene.createNode("Suzanne_Mesh_0", model);
    Node* mesh1  = scene.createNode("Suzanne_Mesh_1", model);
    Node* nested = scene.createNode("Suzanne_Mesh_1_Sub", mesh1);
    markAsInstanceChild(mesh0);
    markAsInstanceChild(mesh1);
    markAsInstanceChild(nested);

    Node* keep = scene.createNode("KeepMe");

    const entt::entity modelHandle  = handleOf(model);
    const entt::entity mesh0Handle  = handleOf(mesh0);
    const entt::entity mesh1Handle  = handleOf(mesh1);
    const entt::entity nestedHandle = handleOf(nested);
    const entt::entity keepHandle   = handleOf(keep);
    const uint32_t     before       = scene.entityCount();

    scene.destroyEntity(scene.getEntityByEnttID(modelHandle));

    EXPECT_EQ(scene.entityCount(), before - 4);
    EXPECT_EQ(scene.getNodeByEntity(modelHandle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(mesh0Handle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(mesh1Handle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(nestedHandle), nullptr);

    // Unrelated scene objects are untouched.
    EXPECT_NE(scene.getNodeByEntity(keepHandle), nullptr);
}

TEST(SceneNodeLifecycleTest, DestroyingOneGeneratedMeshLeavesTheRestOfTheInstanceIntact)
{
    // Regression for the deleted ModelComponent::_childNodes ledger: destroying a
    // single generated mesh (what Delete used to do in the Hierarchy) left the
    // component holding a freed Node*, and the next rebuild walked it.
    Scene scene("Test");
    Node* model = scene.createNode("Model");
    Node* mesh0 = scene.createNode("Mesh_0", model);
    Node* mesh1 = scene.createNode("Mesh_1", model);
    markAsInstanceChild(mesh0);
    markAsInstanceChild(mesh1);

    const entt::entity modelHandle = handleOf(model);
    const entt::entity mesh0Handle = handleOf(mesh0);
    const entt::entity mesh1Handle = handleOf(mesh1);

    scene.destroyEntity(scene.getEntityByEnttID(mesh0Handle));

    EXPECT_EQ(scene.getNodeByEntity(mesh0Handle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(mesh1Handle), mesh1);
    EXPECT_EQ(scene.getNodeByEntity(modelHandle), model);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->getChildCount(), 1u);
    EXPECT_EQ(model->getChild(0), mesh1);

    // Rebuilding the instance means destroying whatever is left under the root;
    // this is the step that used to dereference the stale pointer.
    scene.destroyEntity(scene.getEntityByEnttID(modelHandle));

    EXPECT_EQ(scene.getNodeByEntity(modelHandle), nullptr);
    EXPECT_EQ(scene.getNodeByEntity(mesh1Handle), nullptr);
}

TEST(SceneNodeLifecycleTest, CreatingAComponentOnAnEntityAssignsThatEntityAsItsOwner)
{
    Scene   scene("OwnerScene");
    Entity* entity = scene.createNode3D("Owner")->getEntity();
    ASSERT_NE(entity, nullptr);

    // A component that reads its owner (a camera builds its view from the owner
    // transform) needs the back-pointer whichever funnel created it: the typed
    // API, the name-based API scripts and automation use, or the Scene handle the
    // loader uses.
    ASSERT_NE(entity->addComponent<CameraComponent>(), nullptr);
    EXPECT_EQ(entity->getComponent<CameraComponent>()->getOwner(), entity);

    auto* mesh = static_cast<StaticMeshComponent*>(entity->addComponentByName("StaticMeshComponent"));
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->getOwner(), entity);

    auto* skybox = scene.addComponent<SkyboxComponent>(entity->getHandle());
    ASSERT_NE(skybox, nullptr);
    EXPECT_EQ(skybox->getOwner(), entity);

    // "Get or create" answers for this entity even when the component was
    // already there.
    EXPECT_EQ(entity->addComponentByName("StaticMeshComponent"), mesh);
    EXPECT_EQ(mesh->getOwner(), entity);
}

} // namespace ya

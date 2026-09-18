#include "GameEditor/UI/Ops/EditorHierarchyOps.h"

#include "ECS/Component.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Entity.h"
#include "Hierarchy/Node.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <string>

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

namespace
{

/// Mark `node`'s entity as a generated model-instance child, the way
/// ModelInstantiationSystem does for one mesh.
void markAsInstanceChild(Node* node)
{
    ASSERT_NE(node, nullptr);
    Entity* entity = node ? node->getEntity() : nullptr;
    ASSERT_NE(entity, nullptr);
    if (entity) {
        entity->addComponent<ManagedChildComponent>();
    }
}

} // namespace

TEST(EditorHierarchyOpsTest, InstanceChildPredicateTracksManagedChildComponent)
{
    Scene scene("Test");
    Node* root  = scene.createNode("Model");
    Node* plain = scene.createNode("Empty", root);
    Node* mesh  = scene.createNode("Mesh_0", root);
    markAsInstanceChild(mesh);

    EXPECT_FALSE(editorIsInstanceChild(root->getEntity()));
    EXPECT_FALSE(editorIsInstanceChild(plain->getEntity()));
    EXPECT_TRUE(editorIsInstanceChild(mesh->getEntity()));
    EXPECT_FALSE(editorIsInstanceChild(nullptr));
}

// Regression: the Inspector's instance notice used to call
// `getComponent<ModelComponent>()` unconditionally, so selecting any entity
// without a model -- a camera, a light, an empty -- aborted the editor on the
// `Entity::getComponent` presence assert. Every non-instance entity must now
// produce "nothing to say" instead of a crash.
TEST(EditorHierarchyOpsTest, InstanceNoticeIsEmptyForEntitiesWithoutAModel)
{
    Scene scene("Test");
    Node* camera = scene.createNode3D("Camera");
    Node* light  = scene.createNode3D("Light");
    Node* empty  = scene.createNode("Empty");

    EXPECT_TRUE(camera);

    EXPECT_TRUE(editorInstanceNotice(scene, camera->getEntity()).empty());
    EXPECT_TRUE(editorInstanceNotice(scene, light->getEntity()).empty());
    EXPECT_TRUE(editorInstanceNotice(scene, empty->getEntity()).empty());
    EXPECT_TRUE(editorInstanceNotice(scene, nullptr).empty());
}

// The notice still explains both instance-shaped selections: the model root and
// one of its generated mesh children (which reports its root's name).
TEST(EditorHierarchyOpsTest, InstanceNoticeDescribesModelRootAndItsMeshChild)
{
    Scene scene("Test");
    Node* root = scene.createNode3D("Model");
    Node* mesh = scene.createNode3D("Model_Mesh_0", root);
    markAsInstanceChild(mesh);

    ASSERT_TRUE(root->getEntity());
    ASSERT_TRUE(root->getEntity()->addComponent<ModelComponent>());

    const std::string rootNotice = editorInstanceNotice(scene, root->getEntity());
    EXPECT_FALSE(rootNotice.empty());
    // Names the model source and how many meshes it generated, not the entity.
    EXPECT_NE(rootNotice.find("generated mesh(es)"), std::string::npos);

    const std::string meshNotice = editorInstanceNotice(scene, mesh->getEntity());
    EXPECT_FALSE(meshNotice.empty());
    // A mesh child reports the root it belongs to, so the user knows where to go.
    EXPECT_NE(meshNotice.find("Model"), std::string::npos);
}

TEST(EditorHierarchyOpsTest, ResolveInstanceRootWalksPastManagedAncestors)
{
    Scene scene("Test");
    Node* model = scene.createNode("Model");
    Node* meshA = scene.createNode("Mesh_0", model);
    Node* meshB = scene.createNode("Mesh_1", model);
    markAsInstanceChild(meshA);
    markAsInstanceChild(meshB);

    // The instance root is the first unmanaged ancestor, not the direct parent
    // and not the mesh the ray happened to hit.
    EXPECT_EQ(editorResolveInstanceRoot(scene, meshA->getEntity()), model->getEntity());
    EXPECT_EQ(editorResolveInstanceRoot(scene, meshB->getEntity()), model->getEntity());

    // Unmanaged entities resolve to themselves, including the root.
    EXPECT_EQ(editorResolveInstanceRoot(scene, model->getEntity()), model->getEntity());

    Node* standalone = scene.createNode("Standalone");
    EXPECT_EQ(editorResolveInstanceRoot(scene, standalone->getEntity()), standalone->getEntity());
    EXPECT_EQ(editorResolveInstanceRoot(scene, nullptr), nullptr);
}

TEST(EditorHierarchyOpsTest, ResolveInstanceRootSkipsNestedManagedLevels)
{
    Scene scene("Test");
    Node* model = scene.createNode("Model");
    Node* group = scene.createNode("Group", model);
    Node* leaf  = scene.createNode("Leaf", group);
    markAsInstanceChild(group);
    markAsInstanceChild(leaf);

    EXPECT_EQ(editorResolveInstanceRoot(scene, leaf->getEntity()), model->getEntity());
}

TEST(EditorHierarchyOpsTest, UnmanagedChildOfInstanceChildResolvesToItself)
{
    // An object the author parented under a generated mesh is authored state:
    // resolving it to the model root would make it impossible to select.
    Scene scene("Test");
    Node* model = scene.createNode("Model");
    Node* mesh  = scene.createNode("Mesh_0", model);
    markAsInstanceChild(mesh);
    Node* attached = scene.createNode("Attached", mesh);

    EXPECT_EQ(editorResolveInstanceRoot(scene, attached->getEntity()), attached->getEntity());
}

TEST(EditorHierarchyOpsTest, RejectsReorderingInstanceChildren)
{
    Scene scene("Test");
    Node* model  = scene.createNode("Model");
    Node* mesh   = scene.createNode("Mesh_0", model);
    Node* sibling = scene.createNode("Sibling", model);
    markAsInstanceChild(mesh);

    const std::string meshId    = editorHierarchyEntityIdKey(entityUuidOrFail(mesh->getEntity()));
    const std::string siblingId = editorHierarchyEntityIdKey(entityUuidOrFail(sibling->getEntity()));

    EXPECT_EQ(moveEditorHierarchyEntity(scene, meshId, siblingId, 0), nullptr);
    EXPECT_EQ(model->getChild(0), mesh);
}

TEST(EditorHierarchyOpsTest, RejectsParentingIntoInstanceChildren)
{
    // Dropping into a generated mesh would hand the object to a subtree that the
    // next instantiation destroys.
    Scene scene("Test");
    Node* model      = scene.createNode("Model");
    Node* mesh       = scene.createNode("Mesh_0", model);
    Node* standalone = scene.createNode("Standalone");
    markAsInstanceChild(mesh);

    const std::string meshId       = editorHierarchyEntityIdKey(entityUuidOrFail(mesh->getEntity()));
    const std::string standaloneId = editorHierarchyEntityIdKey(entityUuidOrFail(standalone->getEntity()));

    EXPECT_EQ(moveEditorHierarchyEntity(scene, standaloneId, meshId, 1), nullptr);
    EXPECT_EQ(mesh->getChildCount(), 0u);
    EXPECT_EQ(standalone->getParent(), scene.getRootNode());
}

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

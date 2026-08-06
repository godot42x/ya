#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Entity.h"
#include <gtest/gtest.h>

namespace ya
{

namespace
{

void ensureReflectionReady()
{
    static bool bInitialized = false;
    if (!bInitialized) {
        reflection::DeferredInitializerQueue::instance().executeAll();
        bInitialized = true;
    }
}

std::array<std::string, CubeFace_Count> makeCubemapFacePaths()
{
    return {
        "Content/Skybox/px.hdr",
        "Content/Skybox/nx.hdr",
        "Content/Skybox/py.hdr",
        "Content/Skybox/ny.hdr",
        "Content/Skybox/pz.hdr",
        "Content/Skybox/nz.hdr",
    };
}

} // namespace

TEST(SceneSerializerTest, SkyboxCubemapPathsRoundtrip)
{
    ensureReflectionReady();

    Scene scene("SkyboxScene");
    auto* node = scene.createNode3D("SkyboxEntity", scene.getRootNode());
    ASSERT_NE(node, nullptr);

    auto* entity = node->getEntity();
    ASSERT_NE(entity, nullptr);

    auto* skybox = entity->addComponent<SkyboxComponent>();
    ASSERT_NE(skybox, nullptr);

    skybox->sourceType                 = ESkyboxSourceType::CubeFaces;
    skybox->cubemapSource.files        = makeCubemapFacePaths();
    skybox->cubemapSource.flipVertical = true;
    skybox->cylindricalSource.filepath = "Content/Skybox/fallback.hdr";
    skybox->cylindricalSource.flipVertical = false;

    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    ASSERT_TRUE(json.contains("entities"));
    ASSERT_EQ(json["entities"].size(), 1);
    const auto& entityJson = json["entities"][0];
    ASSERT_TRUE(entityJson.contains("components"));
    ASSERT_TRUE(entityJson["components"].contains("SkyboxComponent"));

    const auto& skyboxJson = entityJson["components"]["SkyboxComponent"];
    ASSERT_TRUE(skyboxJson.contains("cubemapSource"));
    ASSERT_TRUE(skyboxJson["cubemapSource"].contains("files"));
    ASSERT_TRUE(skyboxJson["cubemapSource"]["files"].is_array());
    ASSERT_EQ(skyboxJson["cubemapSource"]["files"].size(), CubeFace_Count);

    const auto expectedPaths = makeCubemapFacePaths();
    for (size_t i = 0; i < expectedPaths.size(); ++i) {
        EXPECT_EQ(skyboxJson["cubemapSource"]["files"][i], expectedPaths[i]);
    }

    Scene loadedScene("LoadedSkyboxScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(json);

    Entity* loadedEntity = loadedScene.getEntityByName("SkyboxEntity");
    ASSERT_NE(loadedEntity, nullptr);
    ASSERT_TRUE(loadedEntity->hasComponent<SkyboxComponent>());

    auto* loadedSkybox = loadedEntity->getComponent<SkyboxComponent>();
    ASSERT_NE(loadedSkybox, nullptr);
    EXPECT_EQ(loadedSkybox->sourceType, ESkyboxSourceType::CubeFaces);
    EXPECT_TRUE(loadedSkybox->cubemapSource.flipVertical);
    EXPECT_EQ(loadedSkybox->cylindricalSource.filepath, "Content/Skybox/fallback.hdr");

    for (size_t i = 0; i < expectedPaths.size(); ++i) {
        EXPECT_EQ(loadedSkybox->cubemapSource.files[i], expectedPaths[i]);
    }
}

// ============================================================================
// Tests verifying that template-component parent classes produce correct
// serialization output with __base__ blocks containing canonical type names.
// This prevents cross-platform oscillation (MSVC prefixing struct/class,
// Clang not) and ensures parent-class fields are preserved on all platforms.
// ============================================================================

TEST(SceneSerializerTest, MaterialComponentBaseClassSerializedWithBaseBlock)
{
    ensureReflectionReady();

    Scene scene("MaterialTestScene");
    auto* node = scene.createNode3D("MaterialEntity", scene.getRootNode());
    ASSERT_NE(node, nullptr);

    auto* entity = node->getEntity();
    ASSERT_NE(entity, nullptr);

    // Add a PBRMaterialComponent – this should queue a deferred init for
    // MaterialComponent<PBRMaterial> via __ensure_reflection_registered().
    auto* pbr = entity->addComponent<PBRMaterialComponent>();
    ASSERT_NE(pbr, nullptr);

    // Set a distinct material path so we can verify round-trip
    pbr->_materialPath = "Content/Materials/test_pbr.mat";

    // Serialize – SceneSerializer::serialize() calls executeAll() which
    // flushes the deferred registration for MaterialComponent<PBRMaterial>.
    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    ASSERT_TRUE(json.contains("entities"));
    ASSERT_EQ(json["entities"].size(), 1);
    const auto& entityJson = json["entities"][0];
    ASSERT_TRUE(entityJson.contains("components"));
    ASSERT_TRUE(entityJson["components"].contains("PBRMaterialComponent"));

    const auto& compJson = entityJson["components"]["PBRMaterialComponent"];

    // The JSON must contain a __base__ block for MaterialComponent<PBRMaterial>
    ASSERT_TRUE(compJson.contains("__base__"));
    ASSERT_TRUE(compJson["__base__"].is_object());

    const auto& baseBlock = compJson["__base__"];
    ASSERT_EQ(baseBlock.size(), 1);

    const std::string baseKey = baseBlock.begin().key();

    // Key must use the canonical type name (no struct/class prefix)
    EXPECT_EQ(baseKey.find("struct "), std::string::npos);
    EXPECT_EQ(baseKey.find("class "), std::string::npos);

    // Key must identify as MaterialComponent<...PBRMaterial>
    EXPECT_NE(baseKey.find("MaterialComponent"), std::string::npos);
    EXPECT_NE(baseKey.find("PBRMaterial"), std::string::npos);

    // The parent-class field _materialPath must be inside the __base__ block
    ASSERT_TRUE(baseBlock[baseKey].is_object());
    EXPECT_EQ(baseBlock[baseKey]["_materialPath"], "Content/Materials/test_pbr.mat");

    // -----------------------------------------------------------------------
    // Round-trip: deserialize and verify _materialPath survives
    // -----------------------------------------------------------------------
    Scene loadedScene("LoadedMaterialScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(json);

    Entity* loadedEntity = loadedScene.getEntityByName("MaterialEntity");
    ASSERT_NE(loadedEntity, nullptr);
    ASSERT_TRUE(loadedEntity->hasComponent<PBRMaterialComponent>());

    auto* loadedPbr = loadedEntity->getComponent<PBRMaterialComponent>();
    ASSERT_NE(loadedPbr, nullptr);
    EXPECT_EQ(loadedPbr->_materialPath, "Content/Materials/test_pbr.mat");

    // Other fields should survive too
    EXPECT_EQ(loadedPbr->_params.albedo, pbr->_params.albedo);
    EXPECT_EQ(loadedPbr->_params.metallic, pbr->_params.metallic);
    EXPECT_EQ(loadedPbr->_params.roughness, pbr->_params.roughness);
}

// ============================================================================
// Entity-less folder nodes must survive a serialize -> deserialize round trip
// and the scene clone path (used by PIE). Folders carry only a name and their
// children; entity nodes inside them keep their entityRef.
// ============================================================================

TEST(SceneSerializerTest, FolderNodesRoundtrip)
{
    ensureReflectionReady();

    Scene scene("FolderScene");
    Node* folder = scene.createFolder("Props", scene.getRootNode());
    ASSERT_NE(folder, nullptr);
    EXPECT_TRUE(folder->isFolder());
    EXPECT_EQ(folder->getEntity(), nullptr);

    Node* cubeNode = scene.createNode3D("Cube", folder);
    ASSERT_NE(cubeNode, nullptr);
    ASSERT_NE(cubeNode->getEntity(), nullptr);

    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    ASSERT_TRUE(json.contains("nodeTree"));
    ASSERT_TRUE(json["nodeTree"].contains("children"));
    ASSERT_EQ(json["nodeTree"]["children"].size(), 1);

    // Folder nodes serialize without an entityRef.
    const auto& folderJson = json["nodeTree"]["children"][0];
    ASSERT_FALSE(folderJson.contains("entityRef"));
    EXPECT_EQ(folderJson["name"], "Props");
    ASSERT_TRUE(folderJson.contains("children"));
    ASSERT_EQ(folderJson["children"].size(), 1);
    EXPECT_EQ(folderJson["children"][0]["name"], "Cube");
    EXPECT_TRUE(folderJson["children"][0].contains("entityRef"));

    Scene loadedScene("LoadedFolderScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(json);

    Node* loadedRoot = loadedScene.getRootNode();
    ASSERT_NE(loadedRoot, nullptr);

    bool bFoundFolder = false;
    for (Node* child : loadedRoot->getChildren()) {
        if (child->isFolder() && child->getName() == "Props") {
            bFoundFolder = true;
            EXPECT_EQ(child->getEntity(), nullptr);
            ASSERT_EQ(child->getChildCount(), 1u);
            EXPECT_FALSE(child->getChildren()[0]->isFolder());
            EXPECT_EQ(child->getChildren()[0]->getName(), "Cube");
        }
    }
    EXPECT_TRUE(bFoundFolder);

    Entity* loadedEntity = loadedScene.getEntityByName("Cube");
    ASSERT_NE(loadedEntity, nullptr);
    ASSERT_TRUE(loadedEntity->isValid());
    EXPECT_EQ(loadedScene.getNodeByEntity(loadedEntity)->getParent(), loadedRoot->getChildren()[0]);
}

TEST(SceneSerializerTest, FolderNodesSurviveClone)
{
    ensureReflectionReady();

    Scene scene("CloneFolderScene");
    Node* folder = scene.createFolder("Actors", scene.getRootNode());
    ASSERT_NE(folder, nullptr);

    Node* childFolder = scene.createFolder("Environment", folder);
    ASSERT_NE(childFolder, nullptr);

    Node* cubeNode = scene.createNode3D("Cube", childFolder);
    ASSERT_NE(cubeNode, nullptr);

    stdptr<Scene> cloned = scene.clone();
    ASSERT_NE(cloned, nullptr);

    Node* clonedRoot = cloned->getRootNode();
    ASSERT_NE(clonedRoot, nullptr);
    ASSERT_EQ(clonedRoot->getChildCount(), 1u);

    Node* clonedFolder = clonedRoot->getChildren()[0];
    EXPECT_TRUE(clonedFolder->isFolder());
    EXPECT_EQ(clonedFolder->getName(), "Actors");
    ASSERT_EQ(clonedFolder->getChildCount(), 1u);

    Node* clonedChildFolder = clonedFolder->getChildren()[0];
    EXPECT_TRUE(clonedChildFolder->isFolder());
    EXPECT_EQ(clonedChildFolder->getName(), "Environment");
    ASSERT_EQ(clonedChildFolder->getChildCount(), 1u);

    Node* clonedCube = clonedChildFolder->getChildren()[0];
    EXPECT_FALSE(clonedCube->isFolder());
    EXPECT_EQ(clonedCube->getName(), "Cube");
    ASSERT_NE(clonedCube->getEntity(), nullptr);
    EXPECT_TRUE(clonedCube->getEntity()->isValid());
}

TEST(SceneSerializerTest, DestroyFolderRemovesEntireSubtree)
{
    ensureReflectionReady();

    Scene scene("DestroyFolderScene");
    Node* folder = scene.createFolder("Root", scene.getRootNode());
    ASSERT_NE(folder, nullptr);

    Node* childFolder = scene.createFolder("Nested", folder);
    ASSERT_NE(childFolder, nullptr);

    Node* parentEntity = scene.createNode3D("Parent", folder);
    ASSERT_NE(parentEntity, nullptr);
    Node* childEntity = scene.createNode3D("Child", parentEntity);
    ASSERT_NE(childEntity, nullptr);
    Entity* childEntityPtr = childEntity->getEntity();
    ASSERT_NE(childEntityPtr, nullptr);

    scene.destroyNode(folder);

    // The whole subtree must be gone: no entities, no nodes.
    EXPECT_FALSE(childEntityPtr->isValid());
    EXPECT_EQ(scene.getEntityByName("Parent"), nullptr);
    EXPECT_EQ(scene.getEntityByName("Child"), nullptr);
    EXPECT_EQ(scene.getRootNode()->getChildCount(), 0u);
}

} // namespace ya

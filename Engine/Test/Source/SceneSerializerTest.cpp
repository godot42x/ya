#include "Foundation/Core/Reflection/DeferredInitializer.h"
#include "Framework/Game/Render/Render3D/SceneSerializer.h"
#include "Framework/Game/Gameplay/ECS/Component/3D/SkyboxComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Material/PBRMaterialComponent.h"
#include "Framework/Game/Gameplay/ECS/Entity.h"
#include "Framework/GUI/Runtime/Scene/Node2D.h"
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
// Node2D (UI) nodes are entity-less and live in the same scene tree as Node3D
// entities. They serialize as nodeType + reflected fields + children, and must
// survive both a scene round trip and the PIE clone path.
// ============================================================================

TEST(SceneSerializerTest, UINodeTreeRoundtrip)
{
    ensureReflectionReady();

    Scene scene("UIScene");
    auto* canvas = scene.createUINode<UICanvasNode>("Canvas");
    ASSERT_NE(canvas, nullptr);

    auto* text = scene.createUINode<UITextNode>("Title", canvas);
    ASSERT_NE(text, nullptr);
    text->_position = {10.0f, 20.0f};
    text->_zOrder   = 3;
    text->_text     = "Hello UI";
    text->_fontSize = 24;
    text->_color    = {1.0f, 0.0f, 0.0f, 1.0f};
    text->_hAlign   = EUIAlignH::Center;

    auto* button = scene.createUINode<UIButtonNode>("OK", canvas);
    ASSERT_NE(button, nullptr);
    button->_position = {100.0f, 200.0f};
    button->_size     = {80.0f, 32.0f};

    // A 3D entity sibling to verify mixed-tree serialization.
    auto* cube = scene.createNode3D("Cube", scene.getRootNode());
    ASSERT_NE(cube, nullptr);

    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    ASSERT_TRUE(json.contains("nodeTree"));
    const auto& children = json["nodeTree"]["children"];
    ASSERT_EQ(children.size(), 2);

    // Find the canvas entry: entity-less with nodeType + fields.
    const nlohmann::json* canvasJson = nullptr;
    const nlohmann::json* cubeJson   = nullptr;
    for (const auto& child : children) {
        if (child.contains("nodeType")) {
            canvasJson = &child;
        }
        else {
            cubeJson = &child;
        }
    }
    ASSERT_NE(canvasJson, nullptr);
    ASSERT_NE(cubeJson, nullptr);

    EXPECT_EQ((*canvasJson)["name"], "Canvas");
    EXPECT_EQ((*canvasJson)["nodeType"], "UICanvasNode");
    EXPECT_FALSE(canvasJson->contains("entityRef"));
    EXPECT_TRUE(canvasJson->contains("fields"));
    EXPECT_TRUE(cubeJson->contains("entityRef"));

    ASSERT_TRUE((*canvasJson).contains("children"));
    ASSERT_EQ((*canvasJson)["children"].size(), 2);
    const auto& titleJson = (*canvasJson)["children"][0];
    EXPECT_EQ(titleJson["nodeType"], "UITextNode");
    EXPECT_EQ(titleJson["fields"]["_text"], "Hello UI");

    Scene loadedScene("LoadedUIScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(json);

    Node* loadedRoot = loadedScene.getRootNode();
    ASSERT_NE(loadedRoot, nullptr);
    ASSERT_EQ(loadedRoot->getChildCount(), 2u);

    Node2D* loadedCanvas = nullptr;
    Node*   loadedCube   = nullptr;
    for (Node* child : loadedRoot->getChildren()) {
        if (child->getEntity()) {
            loadedCube = child;
        }
        else {
            loadedCanvas = dynamic_cast<Node2D*>(child);
        }
    }
    ASSERT_NE(loadedCanvas, nullptr);
    ASSERT_NE(loadedCube, nullptr);
    EXPECT_EQ(loadedCanvas->getName(), "Canvas");
    EXPECT_EQ(loadedCube->getEntity()->getName(), "Cube");

    ASSERT_EQ(loadedCanvas->getChildCount(), 2u);
    auto* loadedText = dynamic_cast<UITextNode*>(loadedCanvas->getChild(0));
    ASSERT_NE(loadedText, nullptr);
    EXPECT_EQ(loadedText->_text, "Hello UI");
    EXPECT_EQ(loadedText->_fontSize, 24u);
    EXPECT_EQ(loadedText->_hAlign, EUIAlignH::Center);
    EXPECT_EQ(loadedText->_position.x, 10.0f);
    EXPECT_EQ(loadedText->_position.y, 20.0f);
    EXPECT_EQ(loadedText->_zOrder, 3);

    auto* loadedButton = dynamic_cast<UIButtonNode*>(loadedCanvas->getChild(1));
    ASSERT_NE(loadedButton, nullptr);
    EXPECT_EQ(loadedButton->_position.x, 100.0f);
    EXPECT_EQ(loadedButton->_size.y, 32.0f);
}

TEST(SceneSerializerTest, UINodeTreeSurvivesClone)
{
    ensureReflectionReady();

    Scene scene("CloneUIScene");
    auto* canvas = scene.createUINode<UICanvasNode>("HUD");
    ASSERT_NE(canvas, nullptr);

    auto* button = scene.createUINode<UIButtonNode>("Start", canvas);
    ASSERT_NE(button, nullptr);
    button->_position = {50.0f, 60.0f};
    button->_size     = {120.0f, 40.0f};
    button->_zOrder   = 5;

    auto* world = scene.createNode3D("World", scene.getRootNode());
    ASSERT_NE(world, nullptr);

    stdptr<Scene> cloned = scene.clone();
    ASSERT_NE(cloned, nullptr);

    Node* clonedRoot = cloned->getRootNode();
    ASSERT_NE(clonedRoot, nullptr);
    ASSERT_EQ(clonedRoot->getChildCount(), 2u);

    Node2D* clonedCanvas = nullptr;
    for (Node* child : clonedRoot->getChildren()) {
        if (!child->getEntity()) {
            clonedCanvas = dynamic_cast<Node2D*>(child);
            break;
        }
    }
    ASSERT_NE(clonedCanvas, nullptr);
    EXPECT_EQ(clonedCanvas->getName(), "HUD");
    ASSERT_EQ(clonedCanvas->getChildCount(), 1u);

    auto* clonedButton = dynamic_cast<UIButtonNode*>(clonedCanvas->getChild(0));
    ASSERT_NE(clonedButton, nullptr);
    EXPECT_EQ(clonedButton->_position.x, 50.0f);
    EXPECT_EQ(clonedButton->_position.y, 60.0f);
    EXPECT_EQ(clonedButton->_size.x, 120.0f);
    EXPECT_EQ(clonedButton->_zOrder, 5);
    EXPECT_EQ(clonedButton->getName(), "Start");
}

} // namespace ya

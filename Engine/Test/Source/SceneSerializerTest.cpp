#include "Core/Reflection/DeferredInitializer.h"
#include "Core/System/VirtualFileSystem.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Entity.h"
#include "GUI/Scene/Node2D.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/SceneWidgetEntry.h"
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

// ============================================================================
// Game UI migration (ui-widget-tree-refactor Phase 2b): the serializer now
// stores Game UI as widgetEntries (inline UIDocuments) instead of Node2D
// scene-tree subtrees. Live Node2D (code-created UI) migrates to entries on
// save; legacy nodeType data migrates to entries on load.
// ============================================================================

TEST(SceneSerializerTest, CodeCreatedUIMigratesToWidgetEntriesOnSave)
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

    // The world tree serializes without UI nodes: the canvas subtree was
    // captured into widgetEntries.
    ASSERT_TRUE(json.contains("nodeTree"));
    const auto& children = json["nodeTree"]["children"];
    ASSERT_EQ(children.size(), 1);
    EXPECT_TRUE(children[0].contains("entityRef"));
    EXPECT_EQ(children[0]["name"], "Cube");

    // Game UI became two entries (the canvas is the content layer).
    ASSERT_TRUE(json.contains("widgetEntries"));
    const auto& entries = json["widgetEntries"];
    ASSERT_EQ(entries.size(), 2);

    const nlohmann::json* titleJson = nullptr;
    const nlohmann::json* okJson    = nullptr;
    for (const auto& entry : entries) {
        const std::string typeId = entry["inline"]["typeId"].get<std::string>();
        if (typeId == "engine.text") {
            titleJson = &entry;
        }
        else if (typeId == "engine.button") {
            okJson = &entry;
        }
    }
    ASSERT_NE(titleJson, nullptr);
    ASSERT_NE(okJson, nullptr);

    EXPECT_EQ((*titleJson)["entryId"], "Title");
    EXPECT_EQ((*titleJson)["zOrder"].get<int32_t>(), 3);
    EXPECT_TRUE((*titleJson)["autoMount"].get<bool>());
    EXPECT_EQ((*titleJson)["inline"]["fields"]["__base__"]["UIElement"]["_position"][0], 10.0);
    EXPECT_EQ((*titleJson)["inline"]["fields"]["_text"], "Hello UI");
    EXPECT_EQ((*titleJson)["inline"]["fields"]["_fontSize"], 24);
    EXPECT_EQ((*okJson)["inline"]["fields"]["__base__"]["UIElement"]["_hitFilter"], "Stop");
    EXPECT_EQ((*okJson)["inline"]["fields"]["__base__"]["UIElement"]["_size"][1], 32.0);

    Scene loadedScene("LoadedUIScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(json);

    // No live Node2D is recreated from the file; entries carry the authoring
    // data, the world tree keeps only the 3D node.
    Node* loadedRoot = loadedScene.getRootNode();
    ASSERT_NE(loadedRoot, nullptr);
    ASSERT_EQ(loadedRoot->getChildCount(), 1u);
    EXPECT_EQ(loadedRoot->getChildren()[0]->getEntity()->getName(), "Cube");

    const auto& loadedEntries = loadedScene.getWidgetEntries();
    ASSERT_EQ(loadedEntries.size(), 2);

    const SceneWidgetEntry* loadedTextEntry = nullptr;
    const SceneWidgetEntry* loadedOkEntry   = nullptr;
    for (const auto& entry : loadedEntries) {
        if (entry.entryId == "Title") {
            loadedTextEntry = &entry;
        }
        else if (entry.entryId == "OK") {
            loadedOkEntry = &entry;
        }
    }
    ASSERT_NE(loadedTextEntry, nullptr);
    ASSERT_NE(loadedOkEntry, nullptr);
    ASSERT_NE(loadedTextEntry->inlineDocument, nullptr);
    EXPECT_EQ(loadedTextEntry->inlineDocument->typeId, "engine.text");
    EXPECT_EQ(loadedTextEntry->zOrder, 3);
    EXPECT_EQ(loadedOkEntry->inlineDocument->typeId, "engine.button");
}

TEST(SceneSerializerTest, ExistingEntriesDoNotDuplicateLiveUINodesOnSave)
{
    ensureReflectionReady();

    Scene scene("NoDupScene");
    // Scene-authored entry (the authoring fact source).
    scene.addWidgetEntry(SceneWidgetEntry{
        .entryId        = "HUD",
        .inlineDocument = std::make_shared<UIDocument>(UIDocument{.typeId = "engine.text"}),
        .autoMount      = true,
    });
    // Plus legacy runtime UI created in code.
    auto* canvas = scene.createUINode<UICanvasNode>("Canvas");
    ASSERT_NE(canvas, nullptr);
    auto* button = scene.createUINode<UIButtonNode>("OK", canvas);
    ASSERT_NE(button, nullptr);

    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    // Only the authored entry is written; the live nodes are runtime-only and
    // must not be migrated again (a save/load cycle would otherwise duplicate
    // UI on every editor save).
    ASSERT_TRUE(json.contains("widgetEntries"));
    ASSERT_EQ(json["widgetEntries"].size(), 1u);
    EXPECT_EQ(json["widgetEntries"][0]["entryId"], "HUD");
    EXPECT_EQ(json["widgetEntries"][0]["inline"]["typeId"], "engine.text");
}

TEST(SceneSerializerTest, UINodeTreeSurvivesClone)
{
    ensureReflectionReady();

    Scene scene("CloneUIScene");
    // Scene-authored entries (new format) + legacy code-created Node2D: the
    // clone copies the authoring recipe; the legacy tree path is unchanged
    // until the runtime switches to WidgetTree (Phase 3).
    scene.addWidgetEntry(SceneWidgetEntry{
        .entryId        = "HUD",
        .inlineDocument = std::make_shared<UIDocument>(UIDocument{
            .typeId = "engine.button",
            .fields = [] {
                nlohmann::json f;
                f["__base__"]           = nlohmann::json::object();
                f["__base__"]["UIElement"] = nlohmann::json{
                    {"_position", {50.0f, 60.0f}},
                    {"_size", {120.0f, 40.0f}},
                    {"_zOrder", 5},
                };
                return f;
            }(),
        }),
        .zOrder   = 5,
        .autoMount = true,
    });

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

    // Authoring entries survive the clone (documents are immutable recipes).
    ASSERT_EQ(cloned->getWidgetEntries().size(), 1u);
    const auto& clonedEntry = cloned->getWidgetEntries().front();
    EXPECT_EQ(clonedEntry.entryId, "HUD");
    ASSERT_NE(clonedEntry.inlineDocument, nullptr);
    EXPECT_EQ(clonedEntry.inlineDocument->typeId, "engine.button");
    EXPECT_EQ(clonedEntry.zOrder, 5);
    EXPECT_EQ(clonedEntry.inlineDocument->fields["__base__"]["UIElement"]["_position"][0], 50.0);

    // Legacy runtime UI tree still clones (removed when Phase 3 switches the
    // runtime fact source).
    Node* clonedRoot = cloned->getRootNode();
    ASSERT_NE(clonedRoot, nullptr);
    ASSERT_EQ(clonedRoot->getChildCount(), 2u); // HUD + World

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

// ============================================================================
// Old-format migration input fixture (Phase 0 of ui-widget-tree-refactor):
// a committed .scene.json that stores Game UI as Node2D tree entries. It is
// the durable input for the Phase 2 importer that migrates these entries to
// SceneWidgetEntry + UIDocument.
// ============================================================================

TEST(SceneSerializerTest, OldFormatUISceneFixtureLoads)
{
    ensureReflectionReady();
    VirtualFileSystem::init();
    ASSERT_NE(VirtualFileSystem::get(), nullptr);

    Scene scene("FixtureScene");
    SceneSerializer serializer(&scene);
    ASSERT_TRUE(serializer.loadFromFile("Engine/Test/Fixture/Data/OldFormatUIScene.scene.json"));

    // The legacy UI subtree (Canvas -> Panel/Title/OK) becomes three authoring
    // entries with inline UIDocuments; no live Node2D is created.
    Node* root = scene.getRootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->getChildCount(), 0u); // no UI nodes in the world tree

    const auto& entries = scene.getWidgetEntries();
    ASSERT_EQ(entries.size(), 3u);

    const SceneWidgetEntry* panelEntry = nullptr;
    const SceneWidgetEntry* titleEntry = nullptr;
    const SceneWidgetEntry* okEntry    = nullptr;
    for (const auto& entry : entries) {
        ASSERT_NE(entry.inlineDocument, nullptr);
        if (entry.inlineDocument->typeId == "engine.panel") {
            panelEntry = &entry;
        }
        else if (entry.inlineDocument->typeId == "engine.text") {
            titleEntry = &entry;
        }
        else if (entry.inlineDocument->typeId == "engine.button") {
            okEntry = &entry;
        }
    }
    ASSERT_NE(panelEntry, nullptr);
    ASSERT_NE(titleEntry, nullptr);
    ASSERT_NE(okEntry, nullptr);

    // Base-class fields translated to the new __base__.UIElement block.
    EXPECT_EQ(panelEntry->inlineDocument->fields["__base__"]["UIElement"]["_position"][0], 20.0);
    EXPECT_EQ(panelEntry->inlineDocument->fields["__base__"]["UIElement"]["_position"][1], 20.0);
    EXPECT_EQ(panelEntry->inlineDocument->fields["__base__"]["UIElement"]["_size"][1], 120.0);
    EXPECT_EQ(panelEntry->inlineDocument->fields["_color"][0], 0.12);

    EXPECT_EQ(titleEntry->entryId, "Title");
    EXPECT_EQ(titleEntry->zOrder, 3);
    EXPECT_EQ(titleEntry->inlineDocument->fields["_text"], "Hello UI");
    EXPECT_EQ(titleEntry->inlineDocument->fields["_fontSize"], 24);
    EXPECT_EQ(titleEntry->inlineDocument->fields["_hAlign"], "Center");

    EXPECT_EQ(okEntry->entryId, "OK");
    EXPECT_EQ(okEntry->inlineDocument->fields["__base__"]["UIElement"]["_hitFilter"], "Stop");

    // The migrated entry can instantiate into a detached widget subtree with
    // the correct fields (importer produces runnable documents).
    UIElementRef okInstance = okEntry->inlineDocument->instantiate();
    ASSERT_NE(okInstance, nullptr);
    auto* button = dynamic_cast<UIButton*>(okInstance.get());
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->_hitFilter, EWidgetHitFilter::Stop);
    EXPECT_EQ(button->_position, glm::vec2(100.0f, 200.0f));
    EXPECT_EQ(button->_size, glm::vec2(80.0f, 32.0f));
}

} // namespace ya

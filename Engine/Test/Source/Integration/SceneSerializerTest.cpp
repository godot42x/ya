#include "Core/Reflection/DeferredInitializer.h"
#include "Core/System/VirtualFileSystem.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "Core/Common/AssetRef.h"
#include "Scene2D/Sprite2DComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "ECS/Systems/TransformSystem.h"
#include "ECS/Entity.h"
#include "GUI/Widgets/Controls/Button.h"
#include "Render3D/Common/CameraFrustumOverlay.h"
#include "Scene/Core/SceneWidgetEntry.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene3D/TransformComponent.h"
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
// Game UI migration (ui-widget-tree-refactor Phase 2b): the serializer now
// stores Game UI as widgetEntries. An entry is a *reference* to a .yaui
// document (UIDocumentStore), never an inline document, so the scene file
// stays small and a document is editable/usable on its own.
// ============================================================================

TEST(SceneSerializerTest, EntriesAndWorldTreeRoundtrip)
{
    ensureReflectionReady();

    Scene scene("UIScene");
    scene.addWidgetEntry(SceneWidgetEntry{
        .entryId      = "Title",
        .documentPath = "Example/Game/Content/UI/Title.yaui",
        .rootSlot     = FCanvasSlotArgs{.offset = {10.0f, 20.0f}},
        .zOrder       = 3,
        .autoMount = true,
    });
    scene.addWidgetEntry(SceneWidgetEntry{
        .entryId      = "OK",
        .documentPath = "Example/Game/Content/UI/OK.yaui",
        .rootSlot     = FCanvasSlotArgs{.fixedSize = {80.0f, 32.0f}},
    });

    // A 3D entity sibling to verify mixed-tree serialization.
    auto* cube = scene.createNode3D("Cube", scene.getRootNode());
    ASSERT_NE(cube, nullptr);

    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    // The world tree serializes without UI nodes.
    ASSERT_TRUE(json.contains("nodeTree"));
    const auto& children = json["nodeTree"]["children"];
    ASSERT_EQ(children.size(), 1);
    EXPECT_TRUE(children[0].contains("entityRef"));
    EXPECT_EQ(children[0]["name"], "Cube");

    // Entries serialize with their document references.
    ASSERT_TRUE(json.contains("widgetEntries"));
    const auto& entries = json["widgetEntries"];
    ASSERT_EQ(entries.size(), 2);

    const nlohmann::json* titleJson = nullptr;
    const nlohmann::json* okJson    = nullptr;
    for (const auto& entry : entries) {
        const std::string document = entry["document"].get<std::string>();
        if (document.ends_with("Title.yaui")) {
            titleJson = &entry;
        }
        else if (document.ends_with("OK.yaui")) {
            okJson = &entry;
        }
    }
    ASSERT_NE(titleJson, nullptr);
    ASSERT_NE(okJson, nullptr);

    EXPECT_EQ((*titleJson)["entryId"], "Title");
    EXPECT_EQ((*titleJson)["zOrder"].get<int32_t>(), 3);
    EXPECT_TRUE((*titleJson)["autoMount"].get<bool>());
    EXPECT_TRUE((*titleJson).contains("rootSlot"));
    // The scene file must not carry the widget tree: a document is an asset.
    EXPECT_FALSE((*titleJson).contains("inline"));
    EXPECT_EQ((*okJson)["document"], "Example/Game/Content/UI/OK.yaui");

    Scene loadedScene("LoadedUIScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(json);

    // Entries carry the authoring data; the world tree keeps only the 3D node.
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
    EXPECT_EQ(loadedTextEntry->documentPath, "Example/Game/Content/UI/Title.yaui");
    EXPECT_EQ(loadedTextEntry->zOrder, 3);
    EXPECT_EQ(loadedOkEntry->documentPath, "Example/Game/Content/UI/OK.yaui");
}

TEST(SceneSerializerTest, SceneSaveWritesEntriesOnly)
{
    ensureReflectionReady();

    Scene scene("NoDupScene");
    // Scene-authored entry (the authoring fact source).
    scene.addWidgetEntry(SceneWidgetEntry{
        .entryId      = "HUD",
        .documentPath = "Example/Game/Content/UI/HUD.yaui",
        .autoMount    = true,
    });
    auto* world = scene.createNode3D("World", scene.getRootNode());
    ASSERT_NE(world, nullptr);

    SceneSerializer serializer(&scene);
    const nlohmann::json json = serializer.serialize();

    // Only the authored entry is written; the world tree has no UI.
    ASSERT_TRUE(json.contains("widgetEntries"));
    ASSERT_EQ(json["widgetEntries"].size(), 1u);
    EXPECT_EQ(json["widgetEntries"][0]["entryId"], "HUD");
    EXPECT_EQ(json["widgetEntries"][0]["document"], "Example/Game/Content/UI/HUD.yaui");
    ASSERT_TRUE(json.contains("nodeTree"));
    ASSERT_EQ(json["nodeTree"]["children"].size(), 1u);
    EXPECT_EQ(json["nodeTree"]["children"][0]["name"], "World");
}

TEST(SceneSerializerTest, WidgetEntriesSurviveClone)
{
    ensureReflectionReady();

    Scene scene("CloneUIScene");
    // Scene-authored entries: the clone copies the authoring recipe.
    scene.addWidgetEntry(SceneWidgetEntry{
        .entryId      = "HUD",
        .documentPath = "Example/Game/Content/UI/HUD.yaui",
        .rootSlot     = FCanvasSlotArgs{.offset = {50.0f, 60.0f}, .fixedSize = {120.0f, 40.0f}},
        .zOrder       = 5,
        .autoMount = true,
    });

    auto* world = scene.createNode3D("World", scene.getRootNode());
    ASSERT_NE(world, nullptr);

    stdptr<Scene> cloned = scene.clone();
    ASSERT_NE(cloned, nullptr);

    // Authoring entries survive the clone (the reference is the recipe).
    ASSERT_EQ(cloned->getWidgetEntries().size(), 1u);
    const auto& clonedEntry = cloned->getWidgetEntries().front();
    EXPECT_EQ(clonedEntry.entryId, "HUD");
    EXPECT_EQ(clonedEntry.documentPath, "Example/Game/Content/UI/HUD.yaui");
    EXPECT_EQ(clonedEntry.zOrder, 5);
    EXPECT_EQ(clonedEntry.rootSlot.offset, glm::vec2(50.0f, 60.0f));

    Node* clonedRoot = cloned->getRootNode();
    ASSERT_NE(clonedRoot, nullptr);
    ASSERT_EQ(clonedRoot->getChildCount(), 1u); // World only
}

// ============================================================================

// A generated companion (camera body, light icon, model mesh child) is derived
// state: it must never reach the scene file, neither as an entity nor as a node
// row, because the host rebuilds it on load. This is the serialization half of
// the companion boundary.
// A reflection clone copies only the reflected state of an asset ref -- the
// path. The handle is runtime-derived: the clone must re-derive it from the
// path exactly like deserialization does, or a PIE scene clone (and every
// duplicated node) carries slots that read as never-loaded and the render
// contract skips them (the PIE black-viewport root cause).
TEST(SceneSerializerTest, CloneRebindsSpriteTextureSlots)
{
    ensureReflectionReady();
    if (!VirtualFileSystem::get()) {
        VirtualFileSystem::init();
    }

    Scene scene("SpriteCloneScene");
    auto* node   = scene.createNode3D("Sprite", scene.getRootNode());
    ASSERT_NE(node, nullptr);
    Entity* spriteEntity = node->getEntity();
    ASSERT_NE(spriteEntity, nullptr);
    auto* sprite = spriteEntity->addComponent<Sprite2DComponent>();
    ASSERT_NE(sprite, nullptr);
    sprite->image.fromPath("Content/Textures/__clone_rebind_missing.png");
    // The source ref is bound (the resolver created its slot at setPath).
    ASSERT_NE(sprite->image.textureRef._handle, nullptr);

    stdptr<Scene> cloned = scene.clone();
    ASSERT_NE(cloned, nullptr);
    Entity* clonedEntity = cloned->getEntityByUUID(
        spriteEntity->getComponent<IDComponent>()->_id.value);
    ASSERT_NE(clonedEntity, nullptr);
    auto* clonedSprite = clonedEntity->getComponent<Sprite2DComponent>();
    ASSERT_NE(clonedSprite, nullptr);
    EXPECT_EQ(clonedSprite->image.textureRef.getPath(), sprite->image.textureRef.getPath());

    // Same bound slot as the source -- not a path-only shell with a null handle.
    ASSERT_NE(clonedSprite->image.textureRef._handle, nullptr);
    EXPECT_EQ(clonedSprite->image.textureRef._handle, sprite->image.textureRef._handle);
}

TEST(SceneSerializerTest, GeneratedCompanionIsNotSerialized)
{
    ensureReflectionReady();

    Scene scene("CompanionScene");
    auto* cameraNode = scene.createNode3D("Camera", scene.getRootNode());
    ASSERT_NE(cameraNode, nullptr);

    Entity* camera = cameraNode->getEntity();
    ASSERT_NE(camera, nullptr);
    ASSERT_NE(camera->addComponent<StaticMeshComponent>(), nullptr);

    // Exactly what CompanionManager builds: a child node plus the generated
    // marker carrying its host. No component field is consulted to decide this.
    auto* bodyNode = scene.createNode3D("Camera_CameraBody", cameraNode);
    ASSERT_NE(bodyNode, nullptr);
    Entity* body = bodyNode->getEntity();
    ASSERT_NE(body, nullptr);
    auto* marker = body->addComponent<ManagedChildComponent>();
    ASSERT_NE(marker, nullptr);
    marker->host = camera->getHandle();

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();

    ASSERT_TRUE(saved.contains("entities"));
    ASSERT_EQ(saved["entities"].size(), 1u);
    EXPECT_EQ(saved["entities"][0]["name"].get<std::string>(), "Camera");

    ASSERT_TRUE(saved.contains("nodeTree"));
    const auto& roots = saved["nodeTree"]["children"];
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0]["name"].get<std::string>(), "Camera");
    // The companion node is gone from the tree; the camera keeps its own mesh.
    ASSERT_TRUE(roots[0].contains("children"));
    EXPECT_TRUE(roots[0]["children"].empty());
    EXPECT_TRUE(saved["entities"][0]["components"].contains("StaticMeshComponent"));
}

TEST(SceneSerializerTest, LoadedCameraViewAndWireframeUseItsAuthoredPose)
{
    ensureReflectionReady();

    const glm::vec3 kAuthoredPosition{3.5f, 6.0f, 12.25f};
    const glm::vec3 kAuthoredRotation{0.0f, 90.0f, 0.0f};

    Scene scene("CameraRoundTripScene");
    auto* cameraNode = scene.createNode3D("Camera", scene.getRootNode());
    ASSERT_NE(cameraNode, nullptr);
    Entity* camera = cameraNode->getEntity();
    ASSERT_NE(camera, nullptr);
    ASSERT_NE(camera->addComponent<CameraComponent>(), nullptr);
    auto* authoredTc = camera->getComponent<TransformComponent>();
    ASSERT_NE(authoredTc, nullptr);
    authoredTc->setPosition(kAuthoredPosition);
    authoredTc->setRotation(kAuthoredRotation);

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();

    Scene         loadedScene("LoadedCameraScene");
    SceneSerializer loadedSerializer(&loadedScene);
    loadedSerializer.deserialize(saved);

    Entity* loaded = loadedScene.getEntityByName("Camera");
    ASSERT_NE(loaded, nullptr);
    ASSERT_TRUE(loaded->hasComponent<CameraComponent>());
    auto* loadedTc = loaded->getComponent<TransformComponent>();
    ASSERT_NE(loadedTc, nullptr);
    TransformSystem::computeWorldMatrix(loadedTc);

    const glm::vec3 worldEye = glm::vec3(loadedTc->getWorldMatrix()[3]);
    EXPECT_NEAR(glm::length(worldEye - kAuthoredPosition), 0.0f, 1e-4f);

    // A camera reads the pose of the entity that owns it. A component created by
    // the deserializer must therefore know its owner: without it the view (and
    // the FOV wireframe drawn from the same matrix) silently falls back to the
    // orbit default near the world origin while the mesh sits at the authored
    // transform -- three things that disagree, and only one of them is right.
    auto* loadedCamera = loaded->getComponent<CameraComponent>();
    EXPECT_EQ(loadedCamera->getOwner(), loaded);

    const glm::mat4 inverseView = glm::inverse(cameraView(*loaded));
    const glm::vec3 viewEye     = glm::vec3(inverseView[3]);
    EXPECT_NEAR(glm::length(viewEye - worldEye), 0.0f, 1e-3f);

    std::vector<RenderOverlayLine3D> lines;
    appendCameraFrustumOverlayLines(lines,
                                    cameraView(*loaded),
                                    loadedCamera->getProjection(loadedCamera->_aspectRatio),
                                    glm::vec4(1.0f));
    ASSERT_FALSE(lines.empty());
    // The wireframe eye is the view eye: the drawn frustum has to sit on the
    // same pose as the mesh.
    EXPECT_NEAR(glm::length(lines.back().from - worldEye), 0.0f, 1e-3f);
}

// ============================================================================

} // namespace ya

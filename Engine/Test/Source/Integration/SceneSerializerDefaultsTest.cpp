#include "Core/Common/SamplerEnums.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "ECS/ECSRegistry.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneWidgetEntry.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "Scene2D/Sprite2DComponent.h"
#include "Scene2D/TilemapComponent.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

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

const nlohmann::json* findComponent(const nlohmann::json& scene, const std::string& entityName, const char* componentName)
{
    if (!scene.contains("entities")) {
        return nullptr;
    }
    for (const auto& entity : scene["entities"]) {
        if (entity.value("name", std::string{}) != entityName) {
            continue;
        }
        if (!entity.contains("components") || !entity["components"].contains(componentName)) {
            return nullptr;
        }
        return &entity["components"][componentName];
    }
    return nullptr;
}

std::set<std::string> objectKeys(const nlohmann::json& json)
{
    std::set<std::string> keys;
    for (auto it = json.begin(); it != json.end(); ++it) {
        keys.insert(it.key());
    }
    return keys;
}

void expectSpriteEqual(const Sprite2DComponent& lhs, const Sprite2DComponent& rhs)
{
    EXPECT_EQ(lhs.bVisible, rhs.bVisible);
    EXPECT_EQ(lhs.size, rhs.size);
    EXPECT_EQ(lhs.pivot, rhs.pivot);
    EXPECT_EQ(lhs.uvRect, rhs.uvRect);
    EXPECT_EQ(lhs.bFlipU, rhs.bFlipU);
    EXPECT_EQ(lhs.bFlipV, rhs.bFlipV);
    EXPECT_EQ(lhs.tint, rhs.tint);
    EXPECT_EQ(lhs.layer, rhs.layer);
    EXPECT_EQ(lhs.sortOrder, rhs.sortOrder);
    EXPECT_EQ(lhs.bYSort, rhs.bYSort);
    EXPECT_EQ(lhs.pickId, rhs.pickId);
    EXPECT_EQ(lhs.image.bEnable, rhs.image.bEnable);
    EXPECT_EQ(lhs.image.uvScale, rhs.image.uvScale);
    EXPECT_EQ(lhs.image.uvOffset, rhs.image.uvOffset);
    EXPECT_FLOAT_EQ(lhs.image.uvRotation, rhs.image.uvRotation);
    EXPECT_EQ(lhs.image.samplerConfig.filterMode, rhs.image.samplerConfig.filterMode);
    EXPECT_EQ(lhs.image.samplerConfig.addressMode, rhs.image.samplerConfig.addressMode);
    EXPECT_EQ(lhs.image.textureRef.getPath(), rhs.image.textureRef.getPath());
}

void collectJsonDiff(const nlohmann::json& created, const nlohmann::json& defaults, const std::string& path, std::string& out)
{
    if (created == defaults) {
        return;
    }
    if (created.is_object() && defaults.is_object()) {
        std::set<std::string> keys = objectKeys(created);
        for (const std::string& key : objectKeys(defaults)) {
            keys.insert(key);
        }
        for (const std::string& key : keys) {
            const std::string child = path.empty() ? key : path + "." + key;
            const bool bInCreated = created.contains(key);
            const bool bInDefault = defaults.contains(key);
            if (!bInCreated || !bInDefault) {
                out += child;
                out += " created=";
                out += bInCreated ? created.at(key).dump() : "<missing>";
                out += " default=";
                out += bInDefault ? defaults.at(key).dump() : "<missing>";
                out += "\n";
                continue;
            }
            collectJsonDiff(created.at(key), defaults.at(key), child, out);
        }
        return;
    }

    out += path.empty() ? "<root>" : path;
    out += " created=";
    out += created.dump();
    out += " default=";
    out += defaults.dump();
    out += "\n";
}

nlohmann::json legacySpriteEntity(const nlohmann::json& sprite, const nlohmann::json& transform)
{
    return {
        {"version", "1.0"},
        {"name", "Legacy"},
        {"entities", nlohmann::json::array({{
            {"id", 7},
            {"name", "Hero"},
            {"components", {
                {"Sprite2DComponent", sprite},
                {"TransformComponent", transform},
            }},
        }})},
    };
}

} // namespace

TEST(SceneSerializerDefaultsTest, DefaultSpriteWritesAnEmptyObject)
{
    ensureReflectionReady();

    Scene scene("DefaultSprite");
    Node* node = scene.createNode3D("Hero", scene.getRootNode());
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->getEntity()->addComponent<Sprite2DComponent>(), nullptr);

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();

    const nlohmann::json* sprite = findComponent(saved, "Hero", "Sprite2DComponent");
    ASSERT_NE(sprite, nullptr);
    EXPECT_TRUE(sprite->is_object());
    EXPECT_TRUE(sprite->empty());

    const nlohmann::json* transform = findComponent(saved, "Hero", "TransformComponent");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(transform->is_object());
    EXPECT_TRUE(transform->empty());
}

TEST(SceneSerializerDefaultsTest, ChangedFieldsAndNestedSamplerOnly)
{
    ensureReflectionReady();

    Scene scene("EditedSprite");
    Node* node = scene.createNode3D("Hero", scene.getRootNode());
    ASSERT_NE(node, nullptr);
    auto* sprite = node->getEntity()->addComponent<Sprite2DComponent>();
    ASSERT_NE(sprite, nullptr);
    sprite->size = {2.0f, 3.0f};
    sprite->tint = {0.25f, 0.5f, 0.75f, 1.0f};

    Node* nested = scene.createNode3D("Prop", scene.getRootNode());
    ASSERT_NE(nested, nullptr);
    auto* prop = nested->getEntity()->addComponent<Sprite2DComponent>();
    ASSERT_NE(prop, nullptr);
    prop->image.samplerConfig.filterMode = EFilter::Nearest;

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();

    const nlohmann::json* hero = findComponent(saved, "Hero", "Sprite2DComponent");
    ASSERT_NE(hero, nullptr);
    EXPECT_EQ(objectKeys(*hero), (std::set<std::string>{"size", "tint"}));
    EXPECT_TRUE((*hero)["size"].is_array());
    EXPECT_TRUE((*hero)["tint"].is_array());

    const nlohmann::json* propJson = findComponent(saved, "Prop", "Sprite2DComponent");
    ASSERT_NE(propJson, nullptr);
    EXPECT_EQ(objectKeys(*propJson), (std::set<std::string>{"image"}));
    ASSERT_TRUE((*propJson)["image"].is_object());
    EXPECT_EQ(objectKeys((*propJson)["image"]), (std::set<std::string>{"samplerConfig"}));
    ASSERT_TRUE((*propJson)["image"]["samplerConfig"].is_object());
    EXPECT_EQ(objectKeys((*propJson)["image"]["samplerConfig"]), (std::set<std::string>{"filterMode"}));
    EXPECT_EQ((*propJson)["image"]["samplerConfig"]["filterMode"], "Nearest");
}

TEST(SceneSerializerDefaultsTest, RoundTripKeepsEditedAndDefaultFields)
{
    ensureReflectionReady();

    Scene scene("RoundTrip");
    Node* heroNode = scene.createNode3D("Hero", scene.getRootNode());
    ASSERT_NE(heroNode, nullptr);
    Entity* hero = heroNode->getEntity();
    auto* sprite = hero->addComponent<Sprite2DComponent>();
    ASSERT_NE(sprite, nullptr);
    sprite->size = {2.0f, 3.0f};
    sprite->tint = {0.25f, 0.5f, 0.75f, 1.0f};
    sprite->image.samplerConfig.filterMode = EFilter::Nearest;
    sprite->image.fromPath("Content/Sprites/hero.png");
    auto* transform = hero->getComponent<TransformComponent>();
    ASSERT_NE(transform, nullptr);
    transform->setPosition({1.0f, 2.0f, 4.0f});

    Node* mapNode = scene.createNode3D("Ground", scene.getRootNode());
    ASSERT_NE(mapNode, nullptr);
    auto* map = mapNode->getEntity()->addComponent<TilemapComponent>();
    ASSERT_NE(map, nullptr);
    map->width = 2;
    map->height = 2;
    map->cellSize = {1.5f, 1.5f};
    TilemapLayer ground;
    ground.name = "Ground";
    ground.cells = {1, 0, 0, 2};
    TilemapLayer overlay;
    overlay.name = "Overlay";
    overlay.zOffset = 0.25f;
    overlay.cells = {0, 3, 0, 0};
    map->layers = {ground, overlay};

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();

    const nlohmann::json* transformJson = findComponent(saved, "Hero", "TransformComponent");
    ASSERT_NE(transformJson, nullptr);
    EXPECT_EQ(objectKeys(*transformJson), (std::set<std::string>{"_position"}));

    const nlohmann::json* mapJson = findComponent(saved, "Ground", "TilemapComponent");
    ASSERT_NE(mapJson, nullptr);
    EXPECT_FALSE(mapJson->contains("layer"));
    EXPECT_TRUE(mapJson->contains("layers"));
    ASSERT_TRUE((*mapJson)["layers"].is_array());
    ASSERT_EQ((*mapJson)["layers"].size(), 2u);
    // Arrays compare as a whole, so a default zOffset inside a layer stays.
    EXPECT_TRUE((*mapJson)["layers"][0].contains("zOffset"));
    EXPECT_EQ((*mapJson)["layers"][0]["name"], "Ground");
    EXPECT_EQ((*mapJson)["layers"][1]["zOffset"], 0.25f);

    Scene loaded("RoundTripLoaded");
    SceneSerializer loadedSerializer(&loaded);
    loadedSerializer.deserialize(saved);

    Entity* loadedHero = loaded.getEntityByName("Hero");
    ASSERT_NE(loadedHero, nullptr);
    auto* loadedSprite = loadedHero->getComponent<Sprite2DComponent>();
    ASSERT_NE(loadedSprite, nullptr);
    expectSpriteEqual(*loadedSprite, *sprite);
    auto* loadedTransform = loadedHero->getComponent<TransformComponent>();
    ASSERT_NE(loadedTransform, nullptr);
    EXPECT_EQ(loadedTransform->getPosition(), transform->getPosition());
    EXPECT_EQ(loadedTransform->getRotation(), glm::vec3(0.0f));
    EXPECT_EQ(loadedTransform->getScale(), glm::vec3(1.0f));

    Entity* loadedGround = loaded.getEntityByName("Ground");
    ASSERT_NE(loadedGround, nullptr);
    auto* loadedMap = loadedGround->getComponent<TilemapComponent>();
    ASSERT_NE(loadedMap, nullptr);
    EXPECT_EQ(loadedMap->width, map->width);
    EXPECT_EQ(loadedMap->height, map->height);
    EXPECT_EQ(loadedMap->cellSize, map->cellSize);
    EXPECT_EQ(loadedMap->layer, 0);
    EXPECT_TRUE(loadedMap->tileset.getPath().empty());
    ASSERT_EQ(loadedMap->layers.size(), 2u);
    EXPECT_EQ(loadedMap->layers[0].name, "Ground");
    EXPECT_FLOAT_EQ(loadedMap->layers[0].zOffset, 0.0f);
    EXPECT_EQ(loadedMap->layers[0].cells, ground.cells);
    EXPECT_EQ(loadedMap->layers[1].name, "Overlay");
    EXPECT_FLOAT_EQ(loadedMap->layers[1].zOffset, 0.25f);
    EXPECT_EQ(loadedMap->layers[1].cells, overlay.cells);
    EXPECT_FALSE(loadedMap->layers[0].bYSort);
    EXPECT_EQ(loadedMap->layers[0].layerOffset, 0);
    EXPECT_FALSE((*mapJson)["layers"][0].contains("bYSort"));
    EXPECT_FALSE((*mapJson)["layers"][0].contains("layerOffset"));
    EXPECT_FALSE((*mapJson)["layers"][1].contains("bYSort"));
    EXPECT_FALSE((*mapJson)["layers"][1].contains("layerOffset"));
}

TEST(SceneSerializerDefaultsTest, YSortAndLayerOffsetOmitTheirDefaults)
{
    ensureReflectionReady();

    Scene scene("YSortDefaults");
    Node* heroNode = scene.createNode3D("Hero", scene.getRootNode());
    ASSERT_NE(heroNode, nullptr);
    auto* sprite = heroNode->getEntity()->addComponent<Sprite2DComponent>();
    ASSERT_NE(sprite, nullptr);
    sprite->bYSort = true;

    Node* propNode = scene.createNode3D("Prop", scene.getRootNode());
    ASSERT_NE(propNode, nullptr);
    ASSERT_NE(propNode->getEntity()->addComponent<Sprite2DComponent>(), nullptr);

    Node* mapNode = scene.createNode3D("Ground", scene.getRootNode());
    ASSERT_NE(mapNode, nullptr);
    auto* map = mapNode->getEntity()->addComponent<TilemapComponent>();
    ASSERT_NE(map, nullptr);
    map->width = 1;
    map->height = 1;
    TilemapLayer ground;
    ground.name = "Ground";
    ground.cells = {1};
    TilemapLayer overlay;
    overlay.name = "Overlay";
    overlay.layerOffset = 1;
    overlay.bYSort = true;
    overlay.cells = {2};
    map->layers = {ground, overlay};

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();

    const nlohmann::json* hero = findComponent(saved, "Hero", "Sprite2DComponent");
    ASSERT_NE(hero, nullptr);
    EXPECT_EQ(hero->value("bYSort", false), true);
    EXPECT_TRUE(hero->contains("bYSort"));

    const nlohmann::json* prop = findComponent(saved, "Prop", "Sprite2DComponent");
    ASSERT_NE(prop, nullptr);
    EXPECT_FALSE(prop->contains("bYSort"));

    const nlohmann::json* mapJson = findComponent(saved, "Ground", "TilemapComponent");
    ASSERT_NE(mapJson, nullptr);
    ASSERT_EQ((*mapJson)["layers"].size(), 2u);
    EXPECT_FALSE((*mapJson)["layers"][0].contains("bYSort"));
    EXPECT_FALSE((*mapJson)["layers"][0].contains("layerOffset"));
    EXPECT_EQ((*mapJson)["layers"][1]["bYSort"], true);
    EXPECT_EQ((*mapJson)["layers"][1]["layerOffset"], 1);

    Scene loaded("YSortDefaultsLoaded");
    SceneSerializer loadedSerializer(&loaded);
    loadedSerializer.deserialize(saved);
    auto* loadedMap = loaded.getEntityByName("Ground")->getComponent<TilemapComponent>();
    ASSERT_NE(loadedMap, nullptr);
    EXPECT_FALSE(loadedMap->layers[0].bYSort);
    EXPECT_EQ(loadedMap->layers[0].layerOffset, 0);
    EXPECT_TRUE(loadedMap->layers[1].bYSort);
    EXPECT_EQ(loadedMap->layers[1].layerOffset, 1);
    EXPECT_TRUE(loaded.getEntityByName("Hero")->getComponent<Sprite2DComponent>()->bYSort);
    EXPECT_FALSE(loaded.getEntityByName("Prop")->getComponent<Sprite2DComponent>()->bYSort);
}

TEST(SceneSerializerDefaultsTest, LegacyFullAndSparseJsonLoadTheSameValues)
{
    ensureReflectionReady();

    const nlohmann::json fullSprite = {
        {"bVisible", true},
        {"size", {2.0, 3.0}},
        {"pivot", {0.5, 0.5}},
        {"uvRect", {0.0, 0.0, 1.0, 1.0}},
        {"bFlipU", false},
        {"bFlipV", false},
        {"tint", {0.25, 0.5, 0.75, 1.0}},
        {"layer", 0},
        {"sortOrder", 0},
        {"pickId", 0},
        {"image", {
            {"bEnable", true},
            {"uvScale", {1.0, 1.0}},
            {"uvOffset", {0.0, 0.0}},
            {"uvRotation", 0.0},
            {"samplerConfig", {{"filterMode", "Nearest"}, {"addressMode", "Repeat"}}},
            {"textureRef", {{"__base__", {{"AssetRefBase", {{"_path", "Content/Sprites/hero.png"}}}}}}},
        }},
    };
    const nlohmann::json fullTransform = {
        {"_position", {1.0, 2.0, 4.0}},
        {"_rotation", {0.0, 0.0, 0.0}},
        {"_scale", {1.0, 1.0, 1.0}},
    };
    const nlohmann::json sparseSprite = {
        {"size", {2.0, 3.0}},
        {"tint", {0.25, 0.5, 0.75, 1.0}},
        {"image", {
            {"samplerConfig", {{"filterMode", "Nearest"}}},
            {"textureRef", {{"__base__", {{"AssetRefBase", {{"_path", "Content/Sprites/hero.png"}}}}}}},
        }},
    };
    const nlohmann::json sparseTransform = {
        {"_position", {1.0, 2.0, 4.0}},
    };

    Scene fullScene("LegacyFull");
    SceneSerializer fullSerializer(&fullScene);
    fullSerializer.deserialize(legacySpriteEntity(fullSprite, fullTransform));

    Scene sparseScene("LegacySparse");
    SceneSerializer sparseSerializer(&sparseScene);
    sparseSerializer.deserialize(legacySpriteEntity(sparseSprite, sparseTransform));

    Entity* fullHero = fullScene.getEntityByName("Hero");
    Entity* sparseHero = sparseScene.getEntityByName("Hero");
    ASSERT_NE(fullHero, nullptr);
    ASSERT_NE(sparseHero, nullptr);

    auto* fullSpriteComponent = fullHero->getComponent<Sprite2DComponent>();
    auto* sparseSpriteComponent = sparseHero->getComponent<Sprite2DComponent>();
    ASSERT_NE(fullSpriteComponent, nullptr);
    ASSERT_NE(sparseSpriteComponent, nullptr);
    expectSpriteEqual(*sparseSpriteComponent, *fullSpriteComponent);
    EXPECT_EQ(fullSpriteComponent->size, glm::vec2(2.0f, 3.0f));
    EXPECT_EQ(fullSpriteComponent->pivot, glm::vec2(0.5f, 0.5f));
    EXPECT_EQ(fullSpriteComponent->image.samplerConfig.filterMode, EFilter::Nearest);
    EXPECT_EQ(fullSpriteComponent->image.samplerConfig.addressMode, ESamplerAddressMode::Repeat);
    EXPECT_FALSE(fullSpriteComponent->image.textureRef.getPath().empty());

    auto* fullTransformComponent = fullHero->getComponent<TransformComponent>();
    auto* sparseTransformComponent = sparseHero->getComponent<TransformComponent>();
    ASSERT_NE(fullTransformComponent, nullptr);
    ASSERT_NE(sparseTransformComponent, nullptr);
    EXPECT_EQ(sparseTransformComponent->getPosition(), fullTransformComponent->getPosition());
    EXPECT_EQ(sparseTransformComponent->getRotation(), fullTransformComponent->getRotation());
    EXPECT_EQ(sparseTransformComponent->getScale(), fullTransformComponent->getScale());
    EXPECT_EQ(fullTransformComponent->getPosition(), glm::vec3(1.0f, 2.0f, 4.0f));
    EXPECT_EQ(fullTransformComponent->getScale(), glm::vec3(1.0f));
}

TEST(SceneSerializerDefaultsTest, RegisteredComponentsCanBuildADefaultInstance)
{
    ensureReflectionReady();

    std::vector<std::string> skipped;
    auto& reg = ECSRegistry::get();
    for (const auto& [name, typeIndex] : reg.getTypeIndexCache()) {
        const auto* ops = reg.getComponentOps(typeIndex);
        if (!ops || !ops->createDefaultInstance()) {
            skipped.push_back(name.toString());
        }
    }
    EXPECT_TRUE(skipped.empty());
    for (const std::string& name : skipped) {
        ADD_FAILURE() << name;
    }
}

TEST(SceneSerializerDefaultsTest, RegisteredComponentsCreatedMatchTheirDefaultInstance)
{
    ensureReflectionReady();

    auto& reg = ECSRegistry::get();
    const std::vector<std::pair<FName, type_index_t>> types(
        reg.getTypeIndexCache().begin(), reg.getTypeIndexCache().end());

    Scene scene("CreatedVersusDefault");
    for (const auto& [name, typeIndex] : types) {
        const std::string typeName = name.toString();
        if (typeName == "IDComponent") {
            continue;
        }

        const auto* ops = reg.getComponentOps(typeIndex);
        if (!ops) {
            ADD_FAILURE() << typeName << " has no component ops";
            continue;
        }

        // createNode builds the same kind of entity deserializeEntity does
        // (IDComponent only). createEntity itself is private to Scene.
        Node* node = scene.createNode(typeName, scene.getRootNode());
        ASSERT_NE(node, nullptr);
        Entity* entity = node->getEntity();
        ASSERT_NE(entity, nullptr);
        void* created = reg.addComponent(name, scene.getRegistry(), entity->getHandle(), entity);
        reflection::DeferredInitializerQueue::instance().executeAll();
        if (!created) {
            ADD_FAILURE() << typeName << " addComponent returned null";
            continue;
        }
        if (!ops->useReflectionSerialization(created)) {
            continue;
        }

        const std::shared_ptr<void> defaults = ops->createDefaultInstance();
        if (!defaults) {
            ADD_FAILURE() << typeName << " has no default instance to compare";
            continue;
        }

        const nlohmann::json createdJson =
            ReflectionSerializer::serializeByRuntimeReflection(created, typeIndex, typeName);
        const nlohmann::json defaultJson =
            ReflectionSerializer::serializeByRuntimeReflection(defaults.get(), typeIndex, typeName);
        if (createdJson == defaultJson) {
            continue;
        }

        std::string diff;
        collectJsonDiff(createdJson, defaultJson, "", diff);
        if (diff.empty()) {
            diff = "created=" + createdJson.dump() + "\ndefault=" + defaultJson.dump() + "\n";
        }
        ADD_FAILURE() << typeName << "\n" << diff;
    }
}

TEST(SceneSerializerDefaultsTest, LuaScriptCustomOutputIsNotStripped)
{
    ensureReflectionReady();

    Scene scene("Lua");
    Node* emptyNode = scene.createNode3D("Empty", scene.getRootNode());
    ASSERT_NE(emptyNode, nullptr);
    ASSERT_NE(emptyNode->getEntity()->addComponent<LuaScriptComponent>(), nullptr);

    Node* playerNode = scene.createNode3D("Player", scene.getRootNode());
    ASSERT_NE(playerNode, nullptr);
    auto* scripts = playerNode->getEntity()->addComponent<LuaScriptComponent>();
    ASSERT_NE(scripts, nullptr);
    scripts->addScript("Content/Scripts/Player.lua");

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();

    const nlohmann::json* emptyJson = findComponent(saved, "Empty", "LuaScriptComponent");
    ASSERT_NE(emptyJson, nullptr);
    ASSERT_TRUE(emptyJson->contains("scripts"));
    EXPECT_TRUE((*emptyJson)["scripts"].is_array());
    EXPECT_TRUE((*emptyJson)["scripts"].empty());

    const nlohmann::json* playerJson = findComponent(saved, "Player", "LuaScriptComponent");
    ASSERT_NE(playerJson, nullptr);
    ASSERT_TRUE(playerJson->contains("scripts"));
    ASSERT_EQ((*playerJson)["scripts"].size(), 1u);
    EXPECT_EQ((*playerJson)["scripts"][0]["scriptPath"], scripts->scripts[0].scriptPath);
    EXPECT_TRUE((*playerJson)["scripts"][0].contains("enabled"));
    EXPECT_EQ((*playerJson)["scripts"][0]["enabled"], true);
    EXPECT_FALSE((*playerJson)["scripts"][0].contains("executionOrder"));
}

namespace
{

const nlohmann::json* findNode(const nlohmann::json& nodes, const std::string& name)
{
    if (!nodes.is_array()) {
        return nullptr;
    }
    for (const auto& node : nodes) {
        if (node.value("name", std::string{}) == name) {
            return &node;
        }
        if (node.contains("children")) {
            if (const nlohmann::json* found = findNode(node["children"], name)) {
                return found;
            }
        }
    }
    return nullptr;
}

nlohmann::json* findNodeMutable(nlohmann::json& nodes, const std::string& name)
{
    if (!nodes.is_array()) {
        return nullptr;
    }
    for (auto& node : nodes) {
        if (node.value("name", std::string{}) == name) {
            return &node;
        }
        if (node.contains("children")) {
            if (nlohmann::json* found = findNodeMutable(node["children"], name)) {
                return found;
            }
        }
    }
    return nullptr;
}

} // namespace

// A host whose only child is a companion used to write `"children": []`, and
// every widget entry wrote `"overrides": {}`. Both load the same when the key
// is missing. A real child and a real override stay.
TEST(SceneSerializerDefaultsTest, EmptyContainersMatchMissingKeys)
{
    ensureReflectionReady();

    Scene scene("SparseContainers");
    Node* camera = scene.createNode3D("Camera", scene.getRootNode());
    ASSERT_NE(camera, nullptr);
    Node* body = scene.createNode3D("CameraBody", camera);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(body->getEntity()->addComponent<ManagedChildComponent>(), nullptr);

    Node* chest = scene.createNode3D("Chest", scene.getRootNode());
    ASSERT_NE(chest, nullptr);
    ASSERT_NE(scene.createNode3D("Lid", chest), nullptr);

    SceneWidgetEntry dialogue;
    dialogue.entryId      = "Dialogue";
    dialogue.documentPath = "Content/UI/Dialogue.yaui.json";
    scene.addWidgetEntry(dialogue);

    SceneWidgetEntry hud;
    hud.entryId      = "HUD";
    hud.documentPath = "Content/UI/HUD.yaui.json";
    hud.overrides.fieldOverrides["title"] = "Hello";
    scene.addWidgetEntry(hud);

    SceneSerializer serializer(&scene);
    const nlohmann::json saved = serializer.serialize();
    EXPECT_EQ(saved.value("version", std::string{}), "1.0");

    ASSERT_TRUE(saved.contains("nodeTree"));
    const nlohmann::json* cameraJson = findNode(saved["nodeTree"]["children"], "Camera");
    const nlohmann::json* chestJson  = findNode(saved["nodeTree"]["children"], "Chest");
    ASSERT_NE(cameraJson, nullptr);
    ASSERT_NE(chestJson, nullptr);
    EXPECT_FALSE(cameraJson->contains("children"));
    ASSERT_TRUE(chestJson->contains("children"));
    ASSERT_EQ((*chestJson)["children"].size(), 1u);
    EXPECT_EQ((*chestJson)["children"][0]["name"], "Lid");

    ASSERT_TRUE(saved.contains("widgetEntries"));
    const nlohmann::json* dialogueJson = nullptr;
    const nlohmann::json* hudJson      = nullptr;
    for (const auto& entry : saved["widgetEntries"]) {
        if (entry.value("entryId", std::string{}) == "Dialogue") {
            dialogueJson = &entry;
        }
        else if (entry.value("entryId", std::string{}) == "HUD") {
            hudJson = &entry;
        }
    }
    ASSERT_NE(dialogueJson, nullptr);
    ASSERT_NE(hudJson, nullptr);
    EXPECT_FALSE(dialogueJson->contains("overrides"));
    ASSERT_TRUE(hudJson->contains("overrides"));
    EXPECT_EQ((*hudJson)["overrides"]["title"], "Hello");

    // The file format marker and a non-empty container are not defaults to drop.
    EXPECT_TRUE(saved.contains("version"));
    EXPECT_FALSE(saved["entities"].empty());

    nlohmann::json legacy = saved;
    nlohmann::json* legacyCamera = findNodeMutable(legacy["nodeTree"]["children"], "Camera");
    ASSERT_NE(legacyCamera, nullptr);
    (*legacyCamera)["children"] = nlohmann::json::array();
    for (auto& entry : legacy["widgetEntries"]) {
        if (entry.value("entryId", std::string{}) == "Dialogue") {
            entry["overrides"] = nlohmann::json::object();
        }
    }

    Scene fromSaved("FromSaved");
    Scene fromLegacy("FromLegacy");
    SceneSerializer(&fromSaved).deserialize(saved);
    SceneSerializer(&fromLegacy).deserialize(legacy);

    const nlohmann::json resaved = SceneSerializer(&fromSaved).serialize();
    const nlohmann::json fromLegacySaved = SceneSerializer(&fromLegacy).serialize();
    EXPECT_EQ(resaved, saved);
    EXPECT_EQ(fromLegacySaved, saved);
}

} // namespace ya

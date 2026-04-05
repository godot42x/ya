#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "ECS/Component/3D/SkyboxComponent.h"
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

} // namespace ya

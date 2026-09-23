#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/Terrain/TerrainProcessor.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <vector>

namespace ya
{
namespace
{

/// The derived-resource processors take the Scene as an argument and keep one
/// Scene's resolve work per Scene. These cases pin the two halves of that
/// contract, because the previous "the Scene prepared last" slot had neither:
/// preparing a second Scene dropped the first Scene's work, and an entity id
/// was only ever meaningful against whichever Scene happened to be pending.
TEST(SceneDerivedStateTest, PreparingASecondSceneKeepsTheFirstScenesSkyboxWork)
{
    Scene sceneA("A");
    Scene sceneB("B");

    // The same entt id in both Scenes: work keyed by entity alone would make
    // these two one entry.
    const auto entityA = sceneA.getRegistry().create();
    const auto entityB = sceneB.getRegistry().create();
    ASSERT_EQ(entityA, entityB);
    sceneA.getRegistry().emplace<SkyboxComponent>(entityA);
    sceneB.getRegistry().emplace<SkyboxComponent>(entityB);

    EnvironmentLightingProcessor lighting;
    lighting.prepareScenes(std::vector<Scene*>{&sceneA, &sceneB}, 0.0f);

    const auto* workA = lighting.findSceneWork(sceneA);
    const auto* workB = lighting.findSceneWork(sceneB);
    ASSERT_NE(workA, nullptr);
    ASSERT_NE(workB, nullptr);

    const auto* skyboxA = workA->findSkyboxState(entityA);
    const auto* skyboxB = workB->findSkyboxState(entityB);
    ASSERT_NE(skyboxA, nullptr);
    ASSERT_NE(skyboxB, nullptr);
    EXPECT_NE(skyboxA, skyboxB);
}

TEST(SceneDerivedStateTest, ASceneTheTickStopsPreparingLosesItsSkyboxWork)
{
    Scene sceneA("A");
    Scene sceneB("B");
    const auto entityA = sceneA.getRegistry().create();
    const auto entityB = sceneB.getRegistry().create();
    sceneA.getRegistry().emplace<SkyboxComponent>(entityA);
    sceneB.getRegistry().emplace<SkyboxComponent>(entityB);

    EnvironmentLightingProcessor lighting;
    lighting.prepareScenes(std::vector<Scene*>{&sceneA, &sceneB}, 0.0f);
    ASSERT_NE(lighting.findSceneWork(sceneA), nullptr);

    lighting.prepareScenes(std::vector<Scene*>{&sceneB}, 0.0f);

    EXPECT_EQ(lighting.findSceneWork(sceneA), nullptr);
    EXPECT_NE(lighting.findSceneWork(sceneB), nullptr);
}

TEST(SceneDerivedStateTest, PreparingASecondSceneKeepsTheFirstScenesTerrainWork)
{
    Scene sceneA("A");
    Scene sceneB("B");
    const auto entityA = sceneA.getRegistry().create();
    const auto entityB = sceneB.getRegistry().create();
    sceneA.getRegistry().emplace<TerrainComponent>(entityA);
    sceneB.getRegistry().emplace<TerrainComponent>(entityB);

    TerrainProcessor terrain;
    terrain.prepareScenes(std::vector<Scene*>{&sceneA}, 0.0f);

    const auto* seededA = terrain.findTerrainState(sceneA, entityA);
    ASSERT_NE(seededA, nullptr);

    terrain.prepareScenes(std::vector<Scene*>{&sceneA, &sceneB}, 0.0f);

    // The same state object, not a re-seed: the second Scene did not take it.
    EXPECT_EQ(terrain.findTerrainState(sceneA, entityA), seededA);
    EXPECT_NE(terrain.findTerrainState(sceneB, entityB), nullptr);
}

} // namespace
} // namespace ya

// Scene transfer during play (rpg-prototype R3): the spawn places the player,
// the persistent table survives a transfer, and leaving play wipes it.

#include "AppModuleTestAccess.h"

#include "GameRuntime/App.h"
#include "GameRuntime/AppSceneServices.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"
#include "ECS/Entity.h"
#include "ECS/Systems/LuaScriptingSystem.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"
#include "Scene3D/Node3D.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace ya
{

namespace
{

/// A scene file with a Player (at 1,1) and, when asked, a spawn marker at
/// (5.5, 1.5). Written to a temp dir so the transfer exercises the real
/// loadScene path.
void writeSceneFile(const std::filesystem::path& path, const std::string& sceneName, bool bWithSpawn)
{
    Scene    scene{sceneName};
    Node3D*  player = scene.createNode3D("Player");
    player->getEntity()->getComponent<TransformComponent>()->setPosition({1.0f, 1.0f, 0.1f});
    if (bWithSpawn) {
        Node3D* marker = scene.createNode3D("SpawnFromTown");
        marker->getEntity()->getComponent<TransformComponent>()->setPosition({5.5f, 1.5f, 0.0f});
    }
    SceneManager manager;
    ASSERT_TRUE(manager.serializeToFile(path.string(), &scene));
}

class SceneTransferTest : public ::testing::Test
{
  protected:
    App                                 app;
    std::unique_ptr<SceneManager>       sceneManager;
    std::unique_ptr<LuaScriptingSystem> lua;
    std::filesystem::path               dir;

    void SetUp() override
    {
        VirtualFileSystem::init();
        sceneManager = std::make_unique<SceneManager>();
        AppModuleTestAccess::setSceneManager(app, sceneManager.get());
        AppModuleTestAccess::setAppState(app, AppState::Runtime);

        // A real Lua state (no init: only what the persist API needs).
        lua = std::make_unique<LuaScriptingSystem>();
        lua->lua().open_libraries(sol::lib::base, sol::lib::table);
        lua->setRuntimeServices({
            .activeScene = [this]() { return sceneManager->getActiveScene(); },
        });
        AppModuleTestAccess::setLuaScriptingSystem(app, lua.get());

        dir         = std::filesystem::temp_directory_path() / "ya-scene-transfer-test";
        std::filesystem::create_directories(dir);
        writeSceneFile(dir / "House.scene.json", "House", true);
        writeSceneFile(dir / "Town.scene.json", "Town", false);
        ASSERT_TRUE(sceneManager->loadScene((dir / "Town.scene.json").string()));
    }

    void TearDown() override
    {
        AppModuleTestAccess::setLuaScriptingSystem(app, nullptr);
        AppModuleTestAccess::setSceneManager(app, nullptr);
        sceneManager.reset();
        lua.reset();
        std::filesystem::remove_all(dir);
    }
};

TEST_F(SceneTransferTest, PersistentStateSurvivesTransfer)
{
    lua->persistentState()["chest"] = true;

    app.getSceneServices().requestSceneTransfer((dir / "House.scene.json").string(), "SpawnFromTown");
    app.getSceneServices().runPendingSceneTransfer();

    Scene* active = sceneManager->getActiveScene();
    ASSERT_NE(active, nullptr);
    // The new scene is live (only House carries the spawn marker) and the
    // player stands on it.
    Entity* marker = active->getEntityByName("SpawnFromTown");
    ASSERT_NE(marker, nullptr);
    Entity* player = active->getEntityByName("Player");
    ASSERT_NE(player, nullptr);
    const glm::vec3 position = player->getComponent<TransformComponent>()->getPosition();
    EXPECT_FLOAT_EQ(position.x, 5.5f);
    EXPECT_FLOAT_EQ(position.y, 1.5f);
    EXPECT_FLOAT_EQ(position.z, 0.1f); // the actor keeps its own depth

    // The persistent table is still the same one, contents intact: a scene
    // transfer never touches it (rpg R3).
    EXPECT_TRUE(lua->lua()["Persist"]["chest"].get<bool>());
}

TEST_F(SceneTransferTest, TransferWithoutSpawnKeepsAuthoredPositions)
{
    app.getSceneServices().requestSceneTransfer((dir / "House.scene.json").string(), "");
    app.getSceneServices().runPendingSceneTransfer();

    Scene* active = sceneManager->getActiveScene();
    ASSERT_NE(active, nullptr);
    Entity* player = active->getEntityByName("Player");
    ASSERT_NE(player, nullptr);
    const glm::vec3 position = player->getComponent<TransformComponent>()->getPosition();
    EXPECT_FLOAT_EQ(position.x, 1.0f);
    EXPECT_FLOAT_EQ(position.y, 1.0f);
}

TEST_F(SceneTransferTest, PlayStopClearsPersistentState)
{
    lua->persistentState()["chest"] = true;

    AppModuleTestAccess::setAppState(app, AppState::Runtime);
    app.stopRuntime();

    // The global reads a fresh table; the next play starts empty.
    const bool bChestViaScript = lua->lua().script("return Persist ~= nil and Persist.chest == true").get<bool>(0);
    EXPECT_FALSE(bChestViaScript);
    const sol::table persist = lua->lua()["Persist"];
    EXPECT_FALSE(persist.get_or("chest", false));
    const sol::table fresh = lua->persistentState();
    EXPECT_FALSE(fresh.get_or("chest", false));
}

TEST_F(SceneTransferTest, SpawnPointPlacesPlayer)
{
    Scene   scene{"Town"};
    Node3D* player = scene.createNode3D("Player");
    player->getEntity()->getComponent<TransformComponent>()->setPosition({1.0f, 1.0f, 0.1f});
    Node3D* marker = scene.createNode3D("SpawnFromTown");
    marker->getEntity()->getComponent<TransformComponent>()->setPosition({7.5f, 3.5f, 0.0f});

    placePlayerAtSpawn(scene, "SpawnFromTown");

    const glm::vec3 position = player->getEntity()->getComponent<TransformComponent>()->getPosition();
    EXPECT_FLOAT_EQ(position.x, 7.5f);
    EXPECT_FLOAT_EQ(position.y, 3.5f);
    EXPECT_FLOAT_EQ(position.z, 0.1f);

    // A missing marker logs and leaves the scene as authored.
    placePlayerAtSpawn(scene, "NoSuchSpawn");
    const glm::vec3 untouched = player->getEntity()->getComponent<TransformComponent>()->getPosition();
    EXPECT_FLOAT_EQ(untouched.x, 7.5f);
}

} // namespace
} // namespace ya

// Terrain resolve is event-driven (resource-handle-events H4). A future
// rebuild tick waits on a min-heap instead of an active re-pump; a height-map
// batch completes through its callback, and a second prepare before that
// callback stays in LoadingHeightMap. Component add / edit / remove reach the
// processor through the scene edit funnel.

#include "Core/Async/TaskQueue.h"
#include "Core/System/VirtualFileSystem.h"
#include "Core/TypeIndex.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "Render3D/Terrain/TerrainProcessor.h"
#include "Resource/AssetManager.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

namespace ya
{
namespace
{

constexpr const char* kMissingHeight = "Content/Textures/__terrain_height_missing.png";

class TerrainResolveEventTest : public ::testing::Test
{
  protected:
    uint64_t _tick = 0;

    void SetUp() override
    {
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        AssetManager::setFrameTaskSink({});
        AssetManager::get()->clearCache();
        TaskQueue::get().start(1);
        _processor.setHostTickProvider([this]() { return _tick; });
        _processor.init();
    }

    void TearDown() override
    {
        _processor.shutdown();
        for (int i = 0; i < 20; ++i) {
            TaskQueue::get().processMainThreadCallbacks();
        }
        AssetManager::get()->clearCache();
    }

    bool pumpUntilFailed(Scene& scene, entt::entity entity)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            TaskQueue::get().processMainThreadCallbacks();
            _processor.prepareScenes(std::vector<Scene*>{&scene}, 0.0f);
            const auto* state = _processor.findTerrainState(scene, entity);
            if (state && state->state == TerrainRuntimeState::EResolveState::Failed) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    TerrainProcessor _processor;
};

TEST_F(TerrainResolveEventTest, DebounceWaitsOnTheHeapAndCompletionIsTheBatchCallback)
{
    stdptr<Scene> scene{new Scene("TerrainDebounce")};
    Node*         node = scene->createNode("Ground");
    Entity*       entity = node->getEntity();
    auto*         terrain = entity->addComponent<TerrainComponent>();
    terrain->_heightMapRef.setPath(kMissingHeight);
    terrain->setRebuildNotBeforeTick(5);

    std::vector<Scene*> scenes{scene.get()};
    _processor.prepareScenes(scenes, 0.0f);

    const auto* waiting = _processor.findTerrainState(*scene, entity->getHandle());
    ASSERT_NE(waiting, nullptr);
    EXPECT_EQ(waiting->state, TerrainRuntimeState::EResolveState::Dirty);

    _tick = 4;
    _processor.prepareScenes(scenes, 0.0f);
    EXPECT_EQ(_processor.findTerrainState(*scene, entity->getHandle())->state,
              TerrainRuntimeState::EResolveState::Dirty);

    _tick = 5;
    _processor.prepareScenes(scenes, 0.0f);
    EXPECT_EQ(_processor.findTerrainState(*scene, entity->getHandle())->state,
              TerrainRuntimeState::EResolveState::LoadingHeightMap);

    // No active re-pump: another prepare before the batch callback does not
    // consume the decode, so the state stays Loading.
    _processor.prepareScenes(scenes, 0.0f);
    EXPECT_EQ(_processor.findTerrainState(*scene, entity->getHandle())->state,
              TerrainRuntimeState::EResolveState::LoadingHeightMap);

    ASSERT_TRUE(pumpUntilFailed(*scene, entity->getHandle()));
    EXPECT_EQ(_processor.getTerrainMesh(*scene, entity->getHandle()), nullptr);
}

TEST_F(TerrainResolveEventTest, AddEditAndRemoveRouteThroughTheSceneBus)
{
    stdptr<Scene> scene{new Scene("TerrainBus")};
    std::vector<Scene*> scenes{scene.get()};
    // The first prepare creates the scene's work. Components added afterwards
    // are discovered by the bus, not by another view sweep.
    _processor.prepareScenes(scenes, 0.0f);

    Node*   node = scene->createNode("Ground");
    Entity* entity = node->getEntity();
    auto*   terrain = entity->addComponent<TerrainComponent>();
    // addComponent broadcasts before the caller fills fields. The path write
    // then goes through the edit funnel, the same way the inspector does.
    terrain->_heightMapRef.setPath(kMissingHeight);
    terrain->invalidate();
    scene->notifyComponentEdited(entity->getHandle(), type_index_v<TerrainComponent>);

    _processor.prepareScenes(scenes, 0.0f);
    const auto* loading = _processor.findTerrainState(*scene, entity->getHandle());
    ASSERT_NE(loading, nullptr);
    EXPECT_EQ(loading->state, TerrainRuntimeState::EResolveState::LoadingHeightMap);

    terrain->_heightScale = 4.0f;
    terrain->invalidate();
    scene->notifyComponentEdited(entity->getHandle(), type_index_v<TerrainComponent>);
    _processor.prepareScenes(scenes, 0.0f);
    EXPECT_EQ(_processor.findTerrainState(*scene, entity->getHandle())->state,
              TerrainRuntimeState::EResolveState::LoadingHeightMap);
    EXPECT_EQ(_processor.findTerrainState(*scene, entity->getHandle())->lastQueuedAuthoringVersion,
              terrain->getAuthoringVersion());

    entity->removeComponent<TerrainComponent>();
    EXPECT_EQ(_processor.findTerrainState(*scene, entity->getHandle()), nullptr);

    // The in-flight batch callback must be inert after the component is gone.
    for (int i = 0; i < 50; ++i) {
        TaskQueue::get().processMainThreadCallbacks();
        _processor.prepareScenes(scenes, 0.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(_processor.findTerrainState(*scene, entity->getHandle()), nullptr);
}

} // namespace
} // namespace ya

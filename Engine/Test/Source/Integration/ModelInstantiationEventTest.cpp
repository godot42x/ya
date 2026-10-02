// ModelInstantiationSystem event-driven discovery guards
// (resource-handle-events H3). Component add/edit broadcasts enqueue
// instantiation work through the SceneBus (seed covers components that
// predate the system's first tick); a Loading model holds a slot
// subscription whose fill re-enqueues the entity. Missing models are a
// terminal childless state; a path edit rebinds the slot and re-instantiates.

#include "Core/Async/TaskQueue.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Entity.h"
#include "ECS/SceneBus.h"
#include "Hierarchy/Node.h"
#include "Render/Adapters/ModelInstantiationSystem.h"
#include "Render3D/ResourceResolveProbe.h"
#include "Resource/AssetManager.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

namespace ya
{
namespace
{

constexpr const char* kMissingModelA = "Content/Models/__instantiate_missing_a.obj";
constexpr const char* kMissingModelB = "Content/Models/__instantiate_missing_b.obj";

class ModelInstantiationEventTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!VirtualFileSystem::get()) {
            VirtualFileSystem::init();
        }
        AssetManager::setFrameTaskSink({});
        AssetManager::get()->clearCache();
        TaskQueue::get().start(1);
        _system.init();
    }

    void TearDown() override
    {
        _system.shutdown();
        AssetManager::get()->clearCache();
    }

    ModelInstantiationSystem _system;

    static bool pumpUntilSettled(ModelComponent& component, ModelInstantiationSystem& system)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (component._modelRef._handle &&
               component._modelRef._handle->state == EAssetSlotState::Loading) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            TaskQueue::get().processMainThreadCallbacks();
            system.onUpdate(0.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
};

TEST_F(ModelInstantiationEventTest, MissingModelsStayChildlessAndPathEditsRequeue)
{
    stdptr<Scene> scene{new Scene("ModelEventScene")};

    // The component exists before the system's first tick: the seed sweep
    // on that tick (not the bus, which fired too early) finds it.
    Node*   node  = scene->createNode("Model");
    Entity* entity = node->getEntity();
    auto*   component = entity->addComponent<ModelComponent>();
    component->setModelPath(kMissingModelA);

    _system.setSceneProvider([&]() { return scene.get(); });
    _system.onUpdate(0.0f);
    ASSERT_TRUE(pumpUntilSettled(*component, _system));

    EXPECT_FALSE(component->isResolved());
    EXPECT_TRUE(node->getChildren().empty());

    // A component added after the system is live is enqueued by the bus
    // broadcast alone.
    Node*   liveNode = scene->createNode("LiveModel");
    Entity* liveEntity = liveNode->getEntity();
    auto*   liveComponent = liveEntity->addComponent<ModelComponent>();
    liveComponent->setModelPath(kMissingModelA);
    _system.onUpdate(0.0f);
    ASSERT_TRUE(pumpUntilSettled(*liveComponent, _system));
    EXPECT_FALSE(liveComponent->isResolved());
    EXPECT_TRUE(liveNode->getChildren().empty());

    // A path edit rebinds the ref's slot and re-queues through the funnel.
    component->setModelPath(kMissingModelB);
    scene->notifyComponentEdited(entity->getHandle(), type_index_v<ModelComponent>);
    _system.onUpdate(0.0f);
    ASSERT_TRUE(pumpUntilSettled(*component, _system));
    EXPECT_FALSE(component->isResolved());
    EXPECT_EQ(component->_modelRef.getResolveState(), EAssetResolveState::Failed);
    EXPECT_TRUE(node->getChildren().empty());
}

TEST_F(ModelInstantiationEventTest, SecondUpdateDoesNotRescanTheModelView)
{
    stdptr<Scene> scene{new Scene("ModelSteady")};
    for (int i = 0; i < 4; ++i) {
        scene->createNode("Model")->getEntity()->addComponent<ModelComponent>();
    }

    resourceResolveComponentTouches() = 0;
    _system.setSceneProvider([&]() { return scene.get(); });
    _system.onUpdate(0.0f);
    const uint64_t seeded = resourceResolveComponentTouches();
    EXPECT_EQ(seeded, 1u);
    _system.onUpdate(0.0f);
    EXPECT_EQ(resourceResolveComponentTouches(), seeded);
}

TEST_F(ModelInstantiationEventTest, RemovedComponentDropsItsPendingWork)
{
    stdptr<Scene> scene{new Scene("ModelEventRemoveScene")};

    Node*   node  = scene->createNode("Model");
    Entity* entity = node->getEntity();
    auto*   component = entity->addComponent<ModelComponent>();
    component->setModelPath(kMissingModelA);

    _system.setSceneProvider([&]() { return scene.get(); });
    _system.onUpdate(0.0f);
    // The load is in flight; the work's subscription is held for the entity.
    TaskQueue::get().processMainThreadCallbacks();

    entity->removeComponent<ModelComponent>();
    _system.onUpdate(0.0f);

    // The slot fill for the removed component must be inert: no crash, no
    // children, and the system stays quiet.
    for (int i = 0; i < 50; ++i) {
        TaskQueue::get().processMainThreadCallbacks();
        _system.onUpdate(0.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(node->getChildren().empty());
    EXPECT_FALSE(scene->getRegistry().all_of<ModelComponent>(entity->getHandle()));
}

} // namespace
} // namespace ya

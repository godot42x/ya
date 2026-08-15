#include "ECS/Linkage/LinkageFramework.h"
#include "Render/Adapters/LightBillboard/LightBillboardLinkageRule.h"
#include "Render/Adapters/Material/MaterialRenderLinkageRule.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

#include <functional>
#include <utility>
#include <vector>

namespace ya
{
namespace
{

class FrameTaskCapture
{
  public:
    std::vector<std::function<void()>> tasks;

    void operator()(std::function<void()> task)
    {
        tasks.push_back(std::move(task));
    }

    void drain()
    {
        // Tasks may schedule more work while running; drain until stable.
        while (!tasks.empty()) {
            auto pending = std::move(tasks);
            tasks.clear();
            for (auto& task : pending) {
                task();
            }
        }
    }

    size_t size() const
    {
        return tasks.size();
    }
};

/// Scenes register themselves with the (global) lifecycle host at
/// construction; bind the test's SceneManager the same way the app does so
/// isSceneValid() works, and restore afterwards.
class SceneLifecycleHostScope
{
  public:
    explicit SceneLifecycleHostScope(ISceneLifecycleHost* host)
    {
        Scene::setLifecycleHost(host);
    }

    ~SceneLifecycleHostScope()
    {
        Scene::setLifecycleHost(nullptr);
    }
};

} // namespace

// Regression: rules must disconnect their entt signal connections when the
// scene is destroyed (onSceneUnload, fired before the registry dies). A
// connected rule would otherwise receive teardown on_destroy events and, if
// the framework/rule was already destroyed (Host shutdown before scene
// teardown), call into freed memory.
TEST(LinkageFrameworkTest, RulesDisconnectOnSceneUnload)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    framework.addRule(std::make_shared<MaterialRenderLinkageRule>(&framework));
    framework.addRule(std::make_shared<LightBillboardLinkageRule>(&framework));
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("LinkageScene");
    auto*         node  = scene->createNode3D("Body");
    node->getEntity()->addComponent<PBRMaterialComponent>();
    node->getEntity()->addComponent<PointLightComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));

    auto& registry = scene->getRegistry();
    // Connected after scene init (sweep also schedules deferred linkage).
    ASSERT_FALSE(registry.on_construct<PBRMaterialComponent>().empty());
    ASSERT_FALSE(registry.on_construct<PointLightComponent>().empty());
    ASSERT_FALSE(registry.on_update<TransformComponent>().empty());
    sink.drain();
    ASSERT_TRUE(node->getEntity()->hasComponent<RenderComponent>());

    // Observe the disconnect from inside onSceneDestroy: by the time the
    // framework's handler ran, every rule signal must be gone from the
    // registry that is about to die.
    bool disconnected = false;
    int  observer;
    sceneManager.onSceneDestroy.addLambda(&observer, [&disconnected, scene](Scene* dying) {
        if (dying != scene.get()) {
            return;
        }
        auto& reg = dying->getRegistry();
        disconnected =
            reg.on_construct<PBRMaterialComponent>().empty() &&
            reg.on_construct<PhongMaterialComponent>().empty() &&
            reg.on_construct<UnlitMaterialComponent>().empty() &&
            reg.on_construct<SimpleMaterialComponent>().empty() &&
            reg.on_construct<PointLightComponent>().empty() &&
            reg.on_construct<DirectionalLightComponent>().empty() &&
            reg.on_update<TransformComponent>().empty();
    });

    const size_t tasksBeforeDestroy = sink.size();
    // Destroy through the manager's active-scene path: this is the same
    // reference-alias flow the app uses at quit (unloadScene -> destroyScene
    // on _activeScene). Regression: onSceneDestroy must be broadcast BEFORE
    // the scene's last reference is released.
    ASSERT_TRUE(sceneManager.unloadScene());
    ASSERT_TRUE(disconnected) << "rule signals must be disconnected before registry teardown";
    // Teardown must not schedule any linkage work.
    ASSERT_EQ(sink.size(), tasksBeforeDestroy);

    framework.shutdown();
}

// Regression: a rule destroyed while its scene is still alive must
// disconnect first, so destroying the scene afterwards cannot call into the
// freed rule (this is the Host shutdown ordering: systems die before scenes).
TEST(LinkageFrameworkTest, RuleDestroyedBeforeSceneTeardownIsSafe)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    framework.addRule(std::make_shared<MaterialRenderLinkageRule>(&framework));
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("LinkageScene");
    auto*         node  = scene->createNode3D("Body");
    node->getEntity()->addComponent<PBRMaterialComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    // Framework shutdown destroys the rules; the scene stays alive.
    framework.shutdown();

    // Scene teardown afterwards must not dereference the destroyed rule.
    EXPECT_NO_FATAL_FAILURE(sceneManager.destroyScene(scene));
}

// Deferred tasks scheduled before shutdown must no-op once the framework is
// gone, even if they are still queued on the host frame-task sink.
TEST(LinkageFrameworkTest, DeferredTasksCancelledAfterShutdown)
{
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setFrameTaskSink(std::ref(sink));

    int ran = 0;
    framework.scheduleDeferred(nullptr, [&ran]() { ++ran; });
    ASSERT_EQ(sink.size(), 1u);

    framework.shutdown();
    sink.drain();
    ASSERT_EQ(ran, 0);
}

// Deferred tasks for a scene that has been destroyed must be skipped by the
// scene-validity guard.
TEST(LinkageFrameworkTest, DeferredTaskSkippedForDestroyedScene)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));

    stdptr<Scene> scene = std::make_shared<Scene>("LinkageScene");
    ASSERT_TRUE(sceneManager.activateScene(scene));

    int ran = 0;
    framework.scheduleDeferred(scene.get(), [&ran]() { ++ran; });
    sceneManager.destroyScene(scene);

    sink.drain();
    ASSERT_EQ(ran, 0);

    framework.shutdown();
}

} // namespace ya

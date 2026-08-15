#pragma once

#include "Core/Api.h"
#include "Core/Delegate.h"
#include "Core/System/System.h"
#include "ECS/Linkage/LinkageRule.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace ya
{

struct Scene;
class SceneManager;

/**
 * @brief Top-level component-linkage framework.
 *
 * Listens to scene lifecycle (SceneManager::onSceneInit) and component
 * removal (SceneBus) and dispatches to registered linkage rules. Deferred
 * work is scheduled on an injected frame-task sink with a scene-validity
 * guard, so rules never need Host access.
 *
 * The framework owns no business state; concrete rules (light billboards,
 * material topology, ...) are registered by the Host composition.
 */
class YA_COMPONENT_LINKAGE_API LinkageFramework : public ISystem
{
  public:
    using FrameTaskSink = std::function<void(std::function<void()>)>;

    LinkageFramework();
    ~LinkageFramework() override;

    /// Injected seams (bound by the Host at startup; no App access here).
    void setSceneManager(SceneManager* manager);
    void setFrameTaskSink(FrameTaskSink sink);

    /// Register a rule; the framework owns it for the framework lifetime.
    void addRule(std::shared_ptr<ILinkageRule> rule);
    void clearRules();

    /// Resolve the scene owning a registry (via the injected SceneManager).
    Scene* findScene(entt::registry& registry);

    /// Run `task` on the injected frame-task sink after the scene is
    /// re-validated; used by rules for deferred linkage work.
    void scheduleDeferred(Scene* scene, std::function<void()> task);

    void init() override;
    void shutdown() override;

  private:
    void onSceneInit(Scene* scene);
    void onSceneDestroy(Scene* scene);
    void onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type);

    SceneManager*                 _sceneManager = nullptr;
    FrameTaskSink                 _frameTaskSink;
    std::vector<std::shared_ptr<ILinkageRule>> _rules;
    /// Cancellation flag shared with in-flight deferred tasks; set when the
    /// framework shuts down so queued tasks no-op instead of touching scenes
    /// or the scene manager that may be torn down afterwards.
    std::shared_ptr<std::atomic<bool>> _cancelled;
    DelegateHandle                _sceneInitHandle;
    DelegateHandle                _sceneDestroyHandle;
    DelegateHandle                _componentRemovedHandle;
};

} // namespace ya

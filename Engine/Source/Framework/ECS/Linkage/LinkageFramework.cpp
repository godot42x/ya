#include "Gameplay/Linkage/LinkageFramework.h"

#include "ECS/SceneBus.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"

namespace ya
{

LinkageFramework::LinkageFramework()
    : _cancelled(std::make_shared<std::atomic<bool>>(false))
{
}

LinkageFramework::~LinkageFramework() = default;

void LinkageFramework::setSceneManager(SceneManager* manager)
{
    _sceneManager = manager;
}

void LinkageFramework::setFrameTaskSink(FrameTaskSink sink)
{
    _frameTaskSink = std::move(sink);
}

void LinkageFramework::addRule(std::shared_ptr<ILinkageRule> rule)
{
    if (rule) {
        _rules.push_back(std::move(rule));
    }
}

void LinkageFramework::clearRules()
{
    _rules.clear();
}

Scene* LinkageFramework::findScene(entt::registry& registry)
{
    return _sceneManager ? _sceneManager->getSceneByRegistry(&registry) : nullptr;
}

void LinkageFramework::scheduleDeferred(Scene* scene, std::function<void()> task)
{
    if (!_frameTaskSink) {
        return;
    }
    SceneManager* manager = _sceneManager;
    auto cancelled = _cancelled;
    _frameTaskSink([scene, manager, cancelled, task = std::move(task)]() {
        if (cancelled->load()) {
            return;
        }
        if (manager && !manager->isSceneValid(scene)) {
            return;
        }
        task();
    });
}

void LinkageFramework::init()
{
    if (!_sceneManager) {
        return;
    }
    // A fresh cancellation generation: tasks scheduled before a previous
    // shutdown keep their (cancelled) flag and can never run again.
    _cancelled = std::make_shared<std::atomic<bool>>(false);
    _sceneInitHandle = _sceneManager->onSceneInit.addObject(this, &LinkageFramework::onSceneInit);
    _sceneDestroyHandle = _sceneManager->onSceneDestroy.addObject(this, &LinkageFramework::onSceneDestroy);
    _componentRemovedHandle = SceneBus::get().onComponentRemoved.addLambda(
        this,
        [this](entt::registry& reg, const entt::entity entity, ya::type_index_t type) {
            onComponentRemoved(reg, entity, type);
        });
}

void LinkageFramework::shutdown()
{
    _cancelled->store(true);
    if (_sceneManager) {
        _sceneManager->onSceneInit.remove(_sceneInitHandle);
        _sceneManager->onSceneDestroy.remove(_sceneDestroyHandle);
    }
    SceneBus::get().onComponentRemoved.remove(_componentRemovedHandle);
    clearRules();
}

void LinkageFramework::onSceneInit(Scene* scene)
{
    for (auto& rule : _rules) {
        rule->onSceneInit(scene);
    }
}

void LinkageFramework::onSceneDestroy(Scene* scene)
{
    for (auto& rule : _rules) {
        rule->onSceneUnload(scene);
    }
}

void LinkageFramework::onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type)
{
    for (auto& rule : _rules) {
        rule->onComponentRemoved(reg, entity, type);
    }
}

} // namespace ya

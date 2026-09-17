#include "Render/Adapters/Companion/CompanionManager.h"

#include "Core/Log.h"
#include "ECS/Entity.h"
#include "ECS/Linkage/LinkageFramework.h"
#include "Hierarchy/Node.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene3D/TransformComponent.h"

namespace ya
{

namespace
{

/// Entity::getComponent asserts when the component is absent, and companion
/// queries run on ordinary entities all the time.
template <typename T>
[[nodiscard]] const T* tryComponent(const Entity& entity)
{
    return entity.hasComponent<T>() ? entity.getComponent<T>() : nullptr;
}

} // namespace

CompanionManager::CompanionManager(LinkageFramework* framework)
    : _framework(framework)
{
}

CompanionManager::~CompanionManager()
{
    // The rule may be destroyed while scenes are still alive (framework
    // shutdown before scene teardown); disconnect every registry we wired so
    // entt teardown signals never reach a dangling `this`.
    const auto connected = _connectedRegistries;
    for (entt::registry* registry : connected) {
        disconnectScene(*registry);
    }
}

void CompanionManager::disconnectScene(entt::registry& reg)
{
    for (const CompanionRegistry::FEntry& entry : CompanionRegistry::get()._entries) {
        if (entry.wire) {
            entry.wire(reg, *this, false);
        }
    }

    if (_transformRegistries.contains(&reg)) {
        reg.on_update<TransformComponent>().disconnect<&CompanionManager::onTransformSignal>(this);
        _transformRegistries.erase(&reg);
    }

    _connectedRegistries.erase(&reg);
}

std::vector<const CompanionSpec*> CompanionManager::activeSpecs(const entt::registry& reg, entt::entity entity)
{
    std::vector<const CompanionSpec*> specs;
    for (const CompanionRegistry::FEntry& entry : CompanionRegistry::get()._entries) {
        if (entry.present && entry.present(reg, entity)) {
            specs.push_back(&entry.spec);
        }
    }
    return specs;
}

const CompanionSpec* CompanionManager::activeSpec(const entt::registry& reg, entt::entity entity)
{
    for (const CompanionRegistry::FEntry& entry : CompanionRegistry::get()._entries) {
        if (entry.present && entry.present(reg, entity)) {
            return &entry.spec;
        }
    }
    return nullptr;
}

Entity* CompanionManager::findCompanion(Scene& scene, Entity& host)
{
    if (!host.isValid()) {
        return nullptr;
    }

    Node* hostNode = scene.getNodeByEntity(&host);
    if (!hostNode) {
        return nullptr;
    }

    for (Node* child : hostNode->getChildren()) {
        Entity* childEntity = child ? child->getEntity() : nullptr;
        if (!childEntity || !childEntity->isValid()) {
            continue;
        }
        const auto* marker = tryComponent<ManagedChildComponent>(*childEntity);
        if (marker && marker->host == host.getHandle()) {
            return childEntity;
        }
    }
    return nullptr;
}

void CompanionManager::reconcileScene(Scene& scene, entt::entity entity)
{
    entt::registry& reg = scene.getRegistry();
    if (!reg.valid(entity)) {
        return;
    }

    Entity* host = scene.getEntityByEnttID(entity);
    if (!host || !host->isValid()) {
        return;
    }

    const std::vector<const CompanionSpec*> specs = activeSpecs(reg, entity);

    if (specs.empty()) {
        // No declared host component left, so the companion has no reason to
        // exist. When the host entity itself is being destroyed the scene's
        // subtree cascade has already taken the companion and this is a no-op.
        if (Entity* companion = findCompanion(scene, *host)) {
            scene.destroyEntity(companion);
        }
        return;
    }

    Entity* companion = findCompanion(scene, *host);
    if (!companion) {
        const CompanionSpec& spec = *specs.front();
        if (!spec.onCreate) {
            return;
        }
        companion = spec.onCreate(scene, *host);
        if (!companion) {
            return;
        }
    }

    // Provenance is stamped here, not by the spec: one place, never forgotten,
    // and no spec can claim an entity that already belongs to someone else.
    auto* marker = companion->hasComponent<ManagedChildComponent>()
                       ? companion->getComponent<ManagedChildComponent>()
                       : nullptr;
    if (!marker) {
        marker = companion->addComponent<ManagedChildComponent>();
    }
    if (marker) {
        marker->host = entity;
    }

    // Host-driven values, in declaration order: an entity carrying several
    // declared host types keeps one companion refreshed by each of them.
    for (const CompanionSpec* spec : specs) {
        if (spec->onUpdateHost) {
            spec->onUpdateHost(scene, *host, *companion);
        }
    }
}

void CompanionManager::scheduleReconcile(entt::registry& reg, entt::entity entity)
{
    if (!_framework) {
        return;
    }

    Scene* scene = _framework->findScene(reg);
    if (!scene) {
        return;
    }

    // Captures no `this`: the deferred task must be safe even when this rule is
    // destroyed before the task drains. All reconcile state comes from the
    // registry.
    _framework->scheduleDeferred(scene, [scene, entity]() {
        reconcileScene(*scene, entity);
    });
}

void CompanionManager::reconcileNow(Scene& scene, entt::entity entity)
{
    reconcileScene(scene, entity);
}

void CompanionManager::onSceneInit(Scene* scene)
{
    if (!scene || !_framework) {
        return;
    }

    entt::registry& reg = scene->getRegistry();
    if (_connectedRegistries.contains(&reg)) {
        return;
    }

    bool bWantsTransformRefresh = false;
    for (const CompanionRegistry::FEntry& entry : CompanionRegistry::get()._entries) {
        if (entry.wire) {
            entry.wire(reg, *this, true);
        }
        bWantsTransformRefresh = bWantsTransformRefresh || entry.spec.bFollowsHostTransform;
    }

    if (bWantsTransformRefresh) {
        reg.on_update<TransformComponent>().connect<&CompanionManager::onTransformSignal>(this);
        _transformRegistries.insert(&reg);
    }

    _connectedRegistries.insert(&reg);

    // Companions are never serialized, so hosts that already exist -- every
    // host in a freshly loaded scene -- get theirs back here.
    for (const CompanionRegistry::FEntry& entry : CompanionRegistry::get()._entries) {
        if (entry.sweep) {
            entry.sweep(reg, *this);
        }
    }
}

void CompanionManager::onSceneUnload(Scene* scene)
{
    if (scene && _connectedRegistries.contains(&scene->getRegistry())) {
        disconnectScene(scene->getRegistry());
    }
}

void CompanionManager::onHostSignal(entt::registry& reg, entt::entity entity)
{
    scheduleReconcile(reg, entity);
}

void CompanionManager::onTransformSignal(entt::registry& reg, entt::entity entity)
{
    // Only declared hosts need a refresh. Ordinary transform churn (every
    // moving entity in the scene) must not schedule linkage work.
    if (activeSpec(reg, entity) == nullptr) {
        return;
    }
    scheduleReconcile(reg, entity);
}

void CompanionManager::onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type)
{
    if (!_framework) {
        return;
    }

    for (const CompanionRegistry::FEntry& entry : CompanionRegistry::get()._entries) {
        if (entry.hostType == type) {
            scheduleReconcile(reg, entity);
            return;
        }
    }
}

Entity* CompanionManager::hostOf(const Entity& companion)
{
    if (!companion.isValid()) {
        return nullptr;
    }

    const auto* marker = tryComponent<ManagedChildComponent>(companion);
    if (!marker || marker->host == entt::null) {
        return nullptr;
    }

    Scene* scene = companion.getScene();
    if (!scene) {
        return nullptr;
    }
    return scene->getEntityByEnttID(marker->host);
}

bool CompanionManager::isGeneratedCompanion(const Entity& entity)
{
    return entity.isValid() && entity.hasComponent<ManagedChildComponent>();
}

ECompanionKind CompanionManager::kindOf(const Entity& entity)
{
    Entity* host = hostOf(entity);
    if (host && host->getRegistry()) {
        if (const CompanionSpec* spec = activeSpec(*host->getRegistry(), host->getHandle())) {
            return spec->kind;
        }
    }
    return ECompanionKind::Content;
}

bool CompanionManager::isAuthorEditable(const Entity& entity)
{
    if (!isGeneratedCompanion(entity)) {
        return true;
    }

    Entity* host = hostOf(entity);
    if (host && host->getRegistry()) {
        if (const CompanionSpec* spec = activeSpec(*host->getRegistry(), host->getHandle())) {
            return spec->bAuthorEditable;
        }
    }

    // An undeclared companion is ordinary content: model mesh children keep
    // their transform editing.
    return true;
}

EAssetPackClass CompanionManager::packClassOf(const Entity& entity)
{
    Entity* host = hostOf(entity);
    if (host && host->getRegistry()) {
        if (const CompanionSpec* spec = activeSpec(*host->getRegistry(), host->getHandle())) {
            return spec->packClass;
        }
    }
    return EAssetPackClass::Runtime;
}

FRenderFeatureMask CompanionManager::featureMaskOf(const Entity& entity)
{
    return renderFeatureMaskOf(kindOf(entity));
}

uint32_t CompanionManager::hostEntityIdOf(const Entity& entity)
{
    if (Entity* host = hostOf(entity)) {
        return static_cast<uint32_t>(host->getHandle());
    }
    return 0;
}

void CompanionManager::eachCompanion(Scene&                       scene,
                                    const std::function<void(Entity& companion, EAssetPackClass packClass)>& visitor)
{
    auto& reg = scene.getRegistry();
    for (auto [handle, marker] : reg.view<ManagedChildComponent>().each()) {
        (void)marker;
        Entity* companion = scene.getEntityByEnttID(handle);
        if (companion && companion->isValid()) {
            visitor(*companion, packClassOf(*companion));
        }
    }
}

} // namespace ya

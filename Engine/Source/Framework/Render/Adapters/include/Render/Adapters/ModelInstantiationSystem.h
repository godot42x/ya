#pragma once

#include "Core/Api.h"
#include "Core/Common/AssetSlot.h"
#include "Core/Delegate.h"
#include "Core/System/System.h"
#include "Core/TypeIndex.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "entt/entt.hpp"

namespace ya
{

struct ModelComponent;
struct Scene;
struct Entity;
struct Model;
struct Node;
struct SkeletonAnimatorComponent;

/**
 * @brief Expand ModelComponent into mesh/material child entities.
 *
 * This system owns scene topology changes caused by ModelComponent.
 * Discovery is event-driven: the scene edit funnel (SceneBus
 * onComponentAdded / onComponentEdited) enqueues ModelComponent entities,
 * and the system holds the component's model-slot subscription whose fill
 * re-enqueues it. Steady state touches no component views.
 *
 * Runtime resource loading for already-existing components stays in GameplayResourceBinding.
 */
struct YA_RENDER_ECS_ADAPTERS_API ModelInstantiationSystem : public ISystem
{
    using SceneProvider = std::function<Scene*()>;

    /// A model-slot fill subscription held for one entity. The handle keeps
    /// the observed slot alive until after the token dies.
    struct SlotSubscription
    {
        AssetHandle<Model>   handle;
        AssetObservers::Token token;
    };

    /// One scene's pending instantiation work.
    struct SceneWork
    {
        entt::registry*                          registry = nullptr;
        std::deque<entt::entity>                 pendingQueue;
        std::unordered_set<entt::entity>         pendingSet;
        std::unordered_map<entt::entity, SlotSubscription> entityWork;
        bool                                     bSeeded = false;
    };

    /// Injected seam (bound by the Host at startup; no App access from here).
    void setSceneProvider(SceneProvider provider);

    void init() override;
    void shutdown() override;
    void onUpdate(float dt) override;

  private:
    SceneProvider _sceneProvider;

    std::unordered_map<const entt::registry*, SceneWork> _sceneWork;
    DelegateHandle _componentAddedHandle   = INVALID_HANDLE;
    DelegateHandle _componentEditedHandle  = INVALID_HANDLE;
    DelegateHandle _componentRemovedHandle = INVALID_HANDLE;
    bool           bBusSubscribed          = false;

    // SceneBus handlers. Work is found by the registry pointer (the key, never
    // dereferenced before the lookup succeeds), so a callback racing a scene
    // teardown is inert.
    void onComponentAdded(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);
    void onComponentEdited(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);
    void onComponentRemoved(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex);

    SceneWork* findWork(const entt::registry* registry);
    void       dropWork(SceneWork& work);
    void       dropEntityWork(SceneWork& work, entt::entity entity);
    void       dropWorkAbsentFrom(Scene& scene);

    void seedSceneWork(SceneWork& work);
    void enqueueModel(SceneWork& work, entt::entity entity);
    /// Slot-fill callbacks may run while their scene is already destroyed;
    /// this only touches the work map key, so it stays inert.
    void enqueueFromSlotFill(const entt::registry* registry, entt::entity entity);

    void instantiatePendingModels(Scene& scene, SceneWork& work);
    void instantiateModel(Scene* scene, Entity* entity, ModelComponent& modelComp);
    void buildSharedMaterials(Model* model, ModelComponent& modelComp);
    SkeletonAnimatorComponent* attachRootSkeletonAnimator(Entity* parentEntity, Model* model);
    Node* createMeshNode(Scene*                      scene,
                         Entity*                     parentEntity,
                         Model*                      model,
                         uint32_t                    meshIndex,
                         ModelComponent&             modelComp,
                         SkeletonAnimatorComponent*  rootAnimator);
    void cleanupChildEntities(Scene* scene, Entity* parentEntity, ModelComponent& modelComp);
};

} // namespace ya

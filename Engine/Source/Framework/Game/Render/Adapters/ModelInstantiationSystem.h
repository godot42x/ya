#pragma once

#include "Core/System/System.h"

#include <cstdint>
#include <functional>
#include <string>

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
 * Runtime resource loading for already-existing components stays in GameplayResourceBinding.
 */
struct ModelInstantiationSystem : public ISystem
{
    using SceneProvider = std::function<Scene*()>;

    /// Injected seam (bound by the Host at startup; no App access from here).
    void setSceneProvider(SceneProvider provider);

    void init() override {}
    void onUpdate(float dt) override;

  private:
    SceneProvider _sceneProvider;

    void instantiatePendingModels(Scene* scene);
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

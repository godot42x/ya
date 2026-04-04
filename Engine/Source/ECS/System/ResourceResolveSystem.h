
#pragma once

#include "Core/System/System.h"
#include "Render/Pipelines/EquidistantCylindrical2CubeMap.h"

namespace ya
{

// Forward declarations
struct MeshComponent;
struct PhongMaterialComponent;
struct PBRMaterialComponent;
struct SkyboxComponent;
struct Scene;

/**
 * @brief Resolve runtime resources for existing ECS components.
 *
 * This system does not create scene topology. ModelComponent expansion is handled
 * by ModelInstantiationSystem before ResourceResolveSystem runs.
 *
 * Call order during frame:
 * 1. ModelInstantiationSystem::onUpdate() - Expand ModelComponent into child entities
 * 2. ResourceResolveSystem::onUpdate() - Resolve runtime resources for existing components
 * 3. MaterialSystem::onUpdateByRenderTarget() - Prepare descriptors
 * 4. MaterialSystem::onRender() - Render
 */
struct ResourceResolveSystem : public ISystem
{
  void init() override;

    /**
     * @brief Resolve all pending resources
     * Iterates through components and calls resolve() on unresolved ones
     */
    void onUpdate(float dt) override;

    void shutdown() override;

  private:
    EquidistantCylindrical2CubeMap _equidistantCylindrical2CubeMap;

    void resolvePendingMeshes(Scene* scene);
    void resolvePendingMaterials(Scene* scene);
    void resolvePendingUI(Scene* scene);
    void resolvePendingBillboards(Scene* scene);
    void resolvePendingSkybox(Scene* scene);
};

} // namespace ya
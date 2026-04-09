
#pragma once

#include "Core/System/System.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "Render/Pipelines/CubeMap2IrradianceMap.h"
#include "Render/Pipelines/EquidistantCylindrical2CubeMap.h"

#include <unordered_map>

namespace ya
{

// Forward declarations
struct MeshComponent;
struct PhongMaterialComponent;
struct PBRMaterialComponent;
struct Scene;

struct SkyboxPendingState
{
    std::shared_ptr<SkyboxComponent::PendingBatchLoadState>       pendingBatchLoad;
    std::shared_ptr<SkyboxComponent::PendingOffscreenProcessState> pendingOffscreenProcess;
    std::optional<TextureFuture>                                  pendingCylindricalFuture;
};

struct EnvironmentLightingPendingState
{
    std::shared_ptr<EnvironmentLightingComponent::PendingBatchLoadState>       pendingBatchLoad;
    std::shared_ptr<EnvironmentLightingComponent::PendingOffscreenProcessState> pendingEnvironmentOffscreen;
    std::shared_ptr<EnvironmentLightingComponent::PendingOffscreenProcessState> pendingIrradianceOffscreen;
    std::optional<TextureFuture>                                                pendingCylindricalFuture;
    Texture*                                                                    lastSceneSkyboxSource = nullptr;
};

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
    EquidistantCylindrical2CubeMap                          _equidistantCylindrical2CubeMap;
    CubeMap2IrradianceMap                                   _cubeMap2IrradianceMap;
    Scene*                                                  _pendingStateScene = nullptr;
    std::unordered_map<entt::entity, SkyboxPendingState>   _skyboxPendingStates;
    std::unordered_map<entt::entity, EnvironmentLightingPendingState> _environmentPendingStates;

    void clearPendingResolveStates();
    void resolvePendingMeshes(Scene* scene);
    void resolvePendingMaterials(Scene* scene);
    void resolvePendingUI(Scene* scene);
    void resolvePendingBillboards(Scene* scene);
    void resolvePendingSkybox(Scene* scene);
    void resolvePendingEnvironmentLighting(Scene* scene);
};

} // namespace ya
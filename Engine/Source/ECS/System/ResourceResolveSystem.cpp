#include "ResourceResolveSystem.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"
#include "Scene/SceneManager.h"



#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"

#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Sampler.h"



namespace ya
{

void ResourceResolveSystem::onUpdate(float dt)
{
    // YA_PROFILE_FUNCTION_LOG();
    (void)dt;
    auto  sceneManager = App::get()->getSceneManager();
    auto* scene        = sceneManager->getActiveScene();
    if (!scene) return;

    resolvePendingMeshes(scene);
    resolvePendingMaterials(scene);
    resolvePendingUI(scene);
    resolvePendingBillboards(scene);
    resolvePendingSkybox(scene);

    // Add more component types here as needed:
    // - SkeletalMeshComponent
    // - etc.
}

void ResourceResolveSystem::resolvePendingMeshes(Scene* scene)
{
    auto& registry = scene->getRegistry();

    registry.view<MeshComponent>().each([&](auto entity, MeshComponent& meshComponent) {
        (void)entity;
        if (!meshComponent.isResolved() && meshComponent.hasMeshSource()) {
            meshComponent.resolve();
        }
    });
}

void ResourceResolveSystem::resolvePendingMaterials(Scene* scene)
{
    auto& registry = scene->getRegistry();

    registry.view<PhongMaterialComponent>().each([&](auto entity, PhongMaterialComponent& materialComponent) {
        (void)entity;
        if (materialComponent.needsResolve()) {
            materialComponent.resolve();
        }
    });

    registry.view<PBRMaterialComponent>().each([&](auto entity, PBRMaterialComponent& materialComponent) {
        (void)entity;
        if (materialComponent.needsResolve()) {
            materialComponent.resolve();
        }
    });
}

void ResourceResolveSystem::resolvePendingUI(Scene* scene)
{
    auto& registry = scene->getRegistry();

    registry.view<UIComponent>().each([&](auto entity, UIComponent& uiComponent) {
        (void)entity;
        if (!uiComponent.view.textureRef.isLoaded() &&
            uiComponent.view.textureRef.hasPath()) {
            uiComponent.view.textureRef.resolve();
        }
    });
}

void ResourceResolveSystem::resolvePendingBillboards(Scene* scene)
{
    auto& registry = scene->getRegistry();

    for (const auto& [entity, comp] : registry.view<BillboardComponent>().each()) {
        (void)entity;
        if (comp.bDirty) {
            comp.resolve();
        }
    }
}

void ResourceResolveSystem::resolvePendingSkybox(Scene* scene)
{
    auto* app      = App::get();
    auto* runtime  = app->getRenderRuntime();
    auto* render   = app->getRender();
    YA_CORE_ASSERT(runtime, "RenderRuntime is null");
    auto& registry = scene->getRegistry();

    for (auto&& [entity, sc] : registry.view<SkyboxComponent>().each()) {
        // Lazy-allocate DS from App-layer pool on first use
        if (!sc.cubeMapDS) {
            auto skyboxDSP = runtime->getSkyboxDescriptorPool();
            auto skyboxDSL = runtime->getSkyboxDescriptorSetLayout();
            YA_CORE_ASSERT(skyboxDSP && skyboxDSL, "Skybox shared descriptor resources are not initialized");
            sc.cubeMapDS = skyboxDSP->allocateDescriptorSets(skyboxDSL);
            render->as<VulkanRender>()->setDebugObjectName(
                VK_OBJECT_TYPE_DESCRIPTOR_SET, sc.cubeMapDS.ptr, "Skybox_CubeMap_DS_Shared");
            sc.bDirty = true; // force initial write
        }

        if (sc.bDirty && sc.cubemapTexture && sc.cubemapTexture->getImageView()) {
            auto* skyboxSampler = runtime->getSkyboxSampler();
            YA_CORE_ASSERT(skyboxSampler, "Skybox sampler is not initialized");
            render->getDescriptorHelper()->updateDescriptorSets(
                {
                    IDescriptorSetHelper::genImageWrite(
                        sc.cubeMapDS,
                        0,
                        0,
                        EPipelineDescriptorType::CombinedImageSampler,
                        {
                            DescriptorImageInfo(
                                sc.cubemapTexture->getImageView()->getHandle(),
                                skyboxSampler->getHandle(),
                                EImageLayout::ShaderReadOnlyOptimal),
                        }),
                },
                {});
            sc.bDirty = false;
        }
    }
}

} // namespace ya

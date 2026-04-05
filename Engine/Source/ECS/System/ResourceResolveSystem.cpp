#include "ResourceResolveSystem.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"
#include "Scene/SceneManager.h"



#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"


#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Sampler.h"
#include "Resource/DeferredDeletionQueue.h"



namespace ya
{

namespace
{
EFormat::T chooseSkyboxCubemapFormat(EFormat::T sourceFormat)
{
    switch (sourceFormat) {
    case EFormat::R8G8B8A8_SRGB:
        return EFormat::R8G8B8A8_SRGB;
    case EFormat::R16G16B16A16_SFLOAT:
        return EFormat::R16G16B16A16_SFLOAT;
    default:
        return EFormat::R8G8B8A8_UNORM;
    }
}

uint32_t computeSkyboxFaceSize(const Texture* sourceTexture)
{
    if (!sourceTexture) {
        return 0;
    }

    const auto width  = sourceTexture->getWidth();
    const auto height = sourceTexture->getHeight();
    if (width == 0 || height == 0) {
        return 0;
    }

    return std::max(1u, std::min(width / 4u, height / 2u));
}

stdptr<Texture> createRenderableSkyboxCubemap(IRender*           render,
                                              const std::string& label,
                                              uint32_t           faceSize,
                                              EFormat::T         format)
{
    auto* textureFactory = render ? render->getTextureFactory() : nullptr;
    if (!textureFactory || faceSize == 0 || format == EFormat::Undefined) {
        return nullptr;
    }

    auto image = textureFactory->createImage(
        ImageCreateInfo{
            .label  = std::format("{}_Image", label),
            .format = format,
            .extent = {
                .width  = faceSize,
                .height = faceSize,
                .depth  = 1,
            },
            .mipLevels     = 1,
            .arrayLayers   = CubeFace_Count,
            .samples       = ESampleCount::Sample_1,
            .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::Sampled),
            .initialLayout = EImageLayout::Undefined,
            .flags         = EImageCreateFlag::CubeCompatible,
        });
    if (!image) {
        return nullptr;
    }

    auto cubeView = textureFactory->createCubeMapImageView(image, EImageAspect::Color);
    if (!cubeView) {
        return nullptr;
    }

    return Texture::wrap(image, cubeView, label);
}
} // namespace

void ResourceResolveSystem::init()
{
    _equidistantCylindrical2CubeMap.init(App::get()->getRender());
}

void ResourceResolveSystem::shutdown()
{
    _equidistantCylindrical2CubeMap.shutdown();
}

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
        else if (materialComponent.isResolved()) {
            materialComponent.checkTexturesStaleness();
        }
    });

    registry.view<PBRMaterialComponent>().each([&](auto entity, PBRMaterialComponent& materialComponent) {
        (void)entity;
        if (materialComponent.needsResolve()) {
            materialComponent.resolve();
        }
        else if (materialComponent.isResolved()) {
            materialComponent.checkTexturesStaleness();
        }
    });
    registry.view<UnlitMaterialComponent>().each([&](auto entity, UnlitMaterialComponent& materialComponent) {
        (void)entity;
        if (materialComponent.needsResolve()) {
            materialComponent.resolve();
        }
        else if (materialComponent.isResolved()) {
            materialComponent.checkTexturesStaleness();
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
    auto* app     = App::get();
    auto* render  = app->getRender();
    auto& registry = scene->getRegistry();

    for (auto&& [entity, sc] : registry.view<SkyboxComponent>().each())
    {
        (void)entity;

        if (sc.resolveState == ESkyboxResolveState::Ready) {
            continue;
        }

        if (sc.resolveState == ESkyboxResolveState::Dirty ||
            sc.resolveState == ESkyboxResolveState::ResolvingSource ||
            sc.resolveState == ESkyboxResolveState::Preprocessing) {
            sc.resolve();
        }

        if (sc.resolveState == ESkyboxResolveState::Preprocessing &&
            sc._pendingOffscreenProcess &&
            sc._pendingOffscreenProcess->sourceTexture &&
            !sc._pendingOffscreenProcess->bTaskQueued)
        {
            auto pending         = sc._pendingOffscreenProcess;
            pending->bTaskQueued = true;

            const auto cubemapLabel = std::format("SkyboxCubemap_{}", static_cast<uint32_t>(entity));
            app->taskManager.enqueueOffscreenTask(
                [this, render, pending, cubemapLabel](ICommandBuffer* cmdBuf) {
                    auto failTask = [&pending]() {
                        pending->bTaskFinished  = true;
                        pending->bTaskSucceeded = false;
                    };

                    if (!pending || !cmdBuf || pending->bCancelled || !pending->sourceTexture || !pending->sourceTexture->getImageView()) {
                        if (pending) {
                            failTask();
                        }
                        return;
                    }

                    const auto faceSize      = computeSkyboxFaceSize(pending->sourceTexture.get());
                    auto       outputTexture = createRenderableSkyboxCubemap(render,
                                                                       cubemapLabel,
                                                                       faceSize,
                                                                       chooseSkyboxCubemapFormat(pending->sourceTexture->getFormat()));
                    if (!outputTexture) {
                        failTask();
                        return;
                    }

                    auto executeResult = _equidistantCylindrical2CubeMap.execute(
                        EquidistantCylindrical2CubeMap::ExecuteContext{
                            .cmdBuf        = cmdBuf,
                            .input         = pending->sourceTexture.get(),
                            .output        = outputTexture.get(),
                            .bFlipVertical = pending->bFlipVertical,
                        });
                    if (!executeResult.bSuccess) {
                        DeferredDeletionQueue::get().retireResource(executeResult.transientOutputArrayView);
                        failTask();
                        return;
                    }

                    DeferredDeletionQueue::get().retireResource(executeResult.transientOutputArrayView);
                    if (pending->bCancelled) {
                        DeferredDeletionQueue::get().retireResource(outputTexture);
                        failTask();
                        return;
                    }

                    pending->outputTexture  = std::move(outputTexture);
                    pending->bTaskFinished  = true;
                    pending->bTaskSucceeded = true;
                });
        }

    }
}

} // namespace ya

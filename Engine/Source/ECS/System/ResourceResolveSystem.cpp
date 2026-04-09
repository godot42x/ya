#include "ResourceResolveSystem.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"
#include "Scene/SceneManager.h"



#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
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

EFormat::T chooseEnvironmentIrradianceFormat(EFormat::T sourceFormat)
{
    switch (sourceFormat) {
    case EFormat::R16G16B16A16_SFLOAT:
        return EFormat::R16G16B16A16_SFLOAT;
    default:
        return EFormat::R16G16B16A16_SFLOAT;
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

uint32_t computeEnvironmentIrradianceFaceSize(const Texture* sourceTexture, uint32_t requestedFaceSize)
{
    if (!sourceTexture) {
        return 0;
    }

    const uint32_t maxFaceSize    = std::max(4u, requestedFaceSize);
    const uint32_t sourceFaceSize = std::max(1u, std::min(sourceTexture->getWidth(), sourceTexture->getHeight()));
    return std::max(4u, std::min(sourceFaceSize, maxFaceSize));
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

void retireSkyboxCubemapTexture(SkyboxComponent& component)
{
    if (component.cubemapTexture) {
        auto& ddq = DeferredDeletionQueue::get();
        ddq.enqueueResource(ddq.currentFrame(), std::move(component.cubemapTexture));
        component.cubemapTexture = nullptr;
    }
    component.clearCubemapPreviewViews();
}

void clearSkyboxPendingState(SkyboxPendingState& state)
{
    state.pendingBatchLoad.reset();
    state.pendingCylindricalFuture.reset();
    if (state.pendingOffscreenProcess) {
        state.pendingOffscreenProcess->bCancelled = true;
        DeferredDeletionQueue::get().retireResource(state.pendingOffscreenProcess->outputTexture);
        state.pendingOffscreenProcess->outputTexture.reset();
        state.pendingOffscreenProcess.reset();
    }
}

void checkSkyboxSourceLoad(SkyboxComponent& component, SkyboxPendingState& pendingState)
{
    auto transition = makeTransition(component.resolveState, "Skybox");
    if (component.resolveState == ESkyboxResolveState::Dirty) {
        if (component.hasCubemapSource()) {
            std::vector<std::string> facePaths(component.cubemapSource.files.begin(), component.cubemapSource.files.end());
            pendingState.pendingBatchLoad              = std::make_shared<SkyboxComponent::PendingBatchLoadState>();
            pendingState.pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
                AssetManager::TextureBatchMemoryLoadRequest{
                    .filepaths = facePaths,
                });
        }
        else if (component.hasCylindricalSource()) {
            pendingState.pendingCylindricalFuture = AssetManager::get()->loadTexture(
                AssetManager::TextureLoadRequest{
                    .filepath        = component.cylindricalSource.filepath,
                    .name            = "SkyboxCylindricalSource",
                    .onReady         = {},
                    .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
                    .textureSemantic = std::nullopt,
                });
        }
        transition.to(ESkyboxResolveState::ResolvingSource, "source load requested");
        return;
    }

    if (component.resolveState != ESkyboxResolveState::ResolvingSource) {
        return;
    }

    if (component.hasCubemapSource()) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!pendingState.pendingBatchLoad ||
            !AssetManager::get()->consumeTextureBatchMemory(pendingState.pendingBatchLoad->batchHandle, batchMemory)) {
            return;
        }

        pendingState.pendingBatchLoad.reset();
        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            retireSkyboxCubemapTexture(component);
            transition.fail("cubemap batch invalid");
            return;
        }

        CubeMapMemoryCreateInfo createInfo;
        createInfo.label        = "SkyboxCubemap";
        createInfo.flipVertical = component.cubemapSource.flipVertical;

        for (size_t index = 0; index < CubeFace_Count; ++index) {
            const auto& face        = batchMemory.textures[index];
            createInfo.faces[index] = TextureMemoryView{
                .width    = face.width,
                .height   = face.height,
                .channels = face.channels,
                .format   = face.format,
                .data     = face.bytes.data(),
                .dataSize = face.bytes.size(),
            };
        }

        auto cubemap = Texture::createCubeMapFromMemory(createInfo);
        if (!cubemap || !cubemap->isValid()) {
            retireSkyboxCubemapTexture(component);
            transition.fail("cubemap creation failed");
            return;
        }

        component.cubemapTexture = std::move(cubemap);
        component.rebuildCubemapPreviewViews();
        transition.to(ESkyboxResolveState::Ready, "cubemap source resolved");
        return;
    }

    if (component.hasCylindricalSource()) {
        if (!pendingState.pendingCylindricalFuture) {
            pendingState.pendingCylindricalFuture = AssetManager::get()->loadTexture(
                AssetManager::TextureLoadRequest{
                    .filepath        = component.cylindricalSource.filepath,
                    .name            = "SkyboxCylindricalSource",
                    .onReady         = {},
                    .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
                    .textureSemantic = std::nullopt,
                });
        }
        if (!pendingState.pendingCylindricalFuture->isReady()) {
            return;
        }

        auto sourceTexture = pendingState.pendingCylindricalFuture->getShared();
        pendingState.pendingCylindricalFuture.reset();
        if (!sourceTexture || !sourceTexture->getImageView()) {
            transition.fail("cylindrical source invalid");
            return;
        }

        component.sourcePreviewTexture                      = sourceTexture;
        pendingState.pendingOffscreenProcess                = std::make_shared<SkyboxComponent::PendingOffscreenProcessState>();
        pendingState.pendingOffscreenProcess->sourceTexture = std::move(sourceTexture);
        pendingState.pendingOffscreenProcess->bFlipVertical = component.cylindricalSource.flipVertical;
        transition.to(ESkyboxResolveState::Preprocessing, "queue cylindrical preprocess");
        return;
    }

    clearSkyboxPendingState(pendingState);
    transition.to(ESkyboxResolveState::Empty, "active source changed while resolving");
}

void consumeSkyboxPreprocess(SkyboxComponent& component, SkyboxPendingState& pendingState)
{
    auto transition = makeTransition(component.resolveState, "Skybox");
    if (component.resolveState != ESkyboxResolveState::Preprocessing ||
        !pendingState.pendingOffscreenProcess ||
        !pendingState.pendingOffscreenProcess->bTaskFinished) {
        return;
    }

    if (!pendingState.pendingOffscreenProcess->bTaskSucceeded ||
        !pendingState.pendingOffscreenProcess->outputTexture) {
        pendingState.pendingOffscreenProcess.reset();
        retireSkyboxCubemapTexture(component);
        transition.fail("preprocess failed");
        return;
    }

    component.cubemapTexture = std::move(pendingState.pendingOffscreenProcess->outputTexture);
    component.rebuildCubemapPreviewViews();
    pendingState.pendingOffscreenProcess.reset();
    transition.to(ESkyboxResolveState::Ready, "preprocess completed");
}

void retireEnvironmentLightingTextures(EnvironmentLightingComponent& component)
{
    auto& ddq = DeferredDeletionQueue::get();
    if (component.cubemapTexture) {
        ddq.enqueueResource(ddq.currentFrame(), std::move(component.cubemapTexture));
        component.cubemapTexture = nullptr;
    }
    if (component.irradianceTexture) {
        ddq.enqueueResource(ddq.currentFrame(), std::move(component.irradianceTexture));
        component.irradianceTexture = nullptr;
    }
}

void clearEnvironmentLightingPendingState(EnvironmentLightingPendingState& state)
{
    state.pendingBatchLoad.reset();
    state.pendingCylindricalFuture.reset();
    state.lastSceneSkyboxSource = nullptr;
    if (state.pendingEnvironmentOffscreen) {
        state.pendingEnvironmentOffscreen->bCancelled = true;
        DeferredDeletionQueue::get().retireResource(state.pendingEnvironmentOffscreen->outputTexture);
        state.pendingEnvironmentOffscreen->outputTexture.reset();
        state.pendingEnvironmentOffscreen.reset();
    }
    if (state.pendingIrradianceOffscreen) {
        state.pendingIrradianceOffscreen->bCancelled = true;
        DeferredDeletionQueue::get().retireResource(state.pendingIrradianceOffscreen->outputTexture);
        state.pendingIrradianceOffscreen->outputTexture.reset();
        state.pendingIrradianceOffscreen.reset();
    }
}

void environmentLighting_JumpToIrradianceFromResolvedCubemap(EnvironmentLightingComponent&    component,
                                        EnvironmentLightingPendingState& pendingState,
                                        stdptr<Texture>                  sourceCubemap)
{
    auto transition = makeTransition(component.resolveState, "EnvironmentLighting");
    if (!sourceCubemap || !sourceCubemap->getImageView()) {
        transition.fail("irradiance source invalid");
        return;
    }

    DeferredDeletionQueue::get().retireResource(component.irradianceTexture);
    component.irradianceTexture.reset();

    pendingState.pendingIrradianceOffscreen                = std::make_shared<EnvironmentLightingComponent::PendingOffscreenProcessState>();
    pendingState.pendingIrradianceOffscreen->sourceTexture = std::move(sourceCubemap);
    transition.to(EEnvironmentLightingResolveState::PreprocessingIrradiance, "queue irradiance preprocess");
}

void syncEnvironmentLightingSceneSkyboxDependency(EnvironmentLightingComponent&    component,
                                                  EnvironmentLightingPendingState& pendingState,
                                                  const stdptr<Texture>&           sceneSkyboxTexture)
{
    if (!component.usesSceneSkybox()) {
        return;
    }

    auto* currentSkyboxSource = sceneSkyboxTexture.get();
    if (pendingState.lastSceneSkyboxSource != currentSkyboxSource) {
        component.invalidate();
        clearEnvironmentLightingPendingState(pendingState);
        pendingState.lastSceneSkyboxSource = currentSkyboxSource;
    }

    if (currentSkyboxSource && component.resolveState == EEnvironmentLightingResolveState::Dirty) {
        // jump to irradiance generate process directly
        environmentLighting_JumpToIrradianceFromResolvedCubemap(component, pendingState, sceneSkyboxTexture);
    }
}

void checkEnvironmentLightingSourceLoad(EnvironmentLightingComponent&    component,
                                        EnvironmentLightingPendingState& pendingState)
{
    auto transition = makeTransition(component.resolveState, "EnvironmentLighting");

    if (component.resolveState == EEnvironmentLightingResolveState::Dirty) {
        if (component.hasCubemapSource()) {
            std::vector<std::string> facePaths(component.cubemapSource.files.begin(), component.cubemapSource.files.end());
            pendingState.pendingBatchLoad              = std::make_shared<EnvironmentLightingComponent::PendingBatchLoadState>();
            pendingState.pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
                AssetManager::TextureBatchMemoryLoadRequest{
                    .filepaths  = facePaths,
                    .colorSpace = AssetManager::ETextureColorSpace::Linear,
                });
            transition.to(EEnvironmentLightingResolveState::ResolvingSource, "source load requested");
        }
        else if (component.hasCylindricalSource()) {
            pendingState.pendingCylindricalFuture = AssetManager::get()->loadTexture(
                AssetManager::TextureLoadRequest{
                    .filepath        = component.cylindricalSource.filepath,
                    .name            = "EnvironmentLightingCylindricalSource",
                    .onReady         = {},
                    .colorSpace      = AssetManager::ETextureColorSpace::Linear,
                    .textureSemantic = std::nullopt,
                });
            transition.to(EEnvironmentLightingResolveState::ResolvingSource, "source load requested");
        }
        return;
    }

    if (component.resolveState != EEnvironmentLightingResolveState::ResolvingSource) {
        return;
    }

    if (component.hasCubemapSource()) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!pendingState.pendingBatchLoad ||
            !AssetManager::get()->consumeTextureBatchMemory(pendingState.pendingBatchLoad->batchHandle, batchMemory)) {
            return;
        }

        pendingState.pendingBatchLoad.reset();
        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            retireEnvironmentLightingTextures(component);
            transition.fail("cubemap batch invalid");
            return;
        }

        CubeMapMemoryCreateInfo createInfo;
        createInfo.label        = "EnvironmentLightingCubemap";
        createInfo.flipVertical = component.cubemapSource.flipVertical;

        for (size_t index = 0; index < CubeFace_Count; ++index) {
            const auto& face        = batchMemory.textures[index];
            createInfo.faces[index] = TextureMemoryView{
                .width    = face.width,
                .height   = face.height,
                .channels = face.channels,
                .format   = face.format,
                .data     = face.bytes.data(),
                .dataSize = face.bytes.size(),
            };
        }

        auto cubemap = Texture::createCubeMapFromMemory(createInfo);
        if (!cubemap || !cubemap->isValid()) {
            retireEnvironmentLightingTextures(component);
            transition.fail("cubemap creation failed");
            return;
        }

        component.cubemapTexture = std::move(cubemap);
        environmentLighting_JumpToIrradianceFromResolvedCubemap(component, pendingState, component.cubemapTexture);
        return;
    }

    if (component.hasCylindricalSource()) {
        if (!pendingState.pendingCylindricalFuture || !pendingState.pendingCylindricalFuture->isReady()) {
            return;
        }

        auto sourceTexture = pendingState.pendingCylindricalFuture->getShared();
        pendingState.pendingCylindricalFuture.reset();
        if (!sourceTexture || !sourceTexture->getImageView()) {
            retireEnvironmentLightingTextures(component);
            transition.fail("cylindrical source invalid");
            return;
        }

        pendingState.pendingEnvironmentOffscreen                = std::make_shared<EnvironmentLightingComponent::PendingOffscreenProcessState>();
        pendingState.pendingEnvironmentOffscreen->sourceTexture = std::move(sourceTexture);
        pendingState.pendingEnvironmentOffscreen->bFlipVertical = component.cylindricalSource.flipVertical;
        // to cylindrical -> cubemap process
        transition.to(EEnvironmentLightingResolveState::PreprocessingEnvironment,
                      "queue environment preprocess");
    }
}

void consumeEnvironmentCubemapPreprocess(EnvironmentLightingComponent&    component,
                                         EnvironmentLightingPendingState& pendingState)
{
    auto transition = makeTransition(component.resolveState, "EnvironmentLighting");
    if (component.resolveState != EEnvironmentLightingResolveState::PreprocessingEnvironment ||
        !pendingState.pendingEnvironmentOffscreen ||
        !pendingState.pendingEnvironmentOffscreen->bTaskFinished) {
        return;
    }

    if (!pendingState.pendingEnvironmentOffscreen->bTaskSucceeded ||
        !pendingState.pendingEnvironmentOffscreen->outputTexture) {
        pendingState.pendingEnvironmentOffscreen.reset();
        retireEnvironmentLightingTextures(component);
        transition.fail("environment preprocess failed");
        return;
    }

    component.cubemapTexture = std::move(pendingState.pendingEnvironmentOffscreen->outputTexture);
    pendingState.pendingEnvironmentOffscreen.reset();
    environmentLighting_JumpToIrradianceFromResolvedCubemap(component, pendingState, component.cubemapTexture);
}

void consumeEnvironmentIrradiancePreprocess(EnvironmentLightingComponent&    component,
                                            EnvironmentLightingPendingState& pendingState)
{
    auto transition = makeTransition(component.resolveState, "EnvironmentLighting");
    if (component.resolveState != EEnvironmentLightingResolveState::PreprocessingIrradiance ||
        !pendingState.pendingIrradianceOffscreen ||
        !pendingState.pendingIrradianceOffscreen->bTaskFinished) {
        return;
    }

    if (!pendingState.pendingIrradianceOffscreen->bTaskSucceeded ||
        !pendingState.pendingIrradianceOffscreen->outputTexture) {
        pendingState.pendingIrradianceOffscreen.reset();
        DeferredDeletionQueue::get().retireResource(component.irradianceTexture);
        component.irradianceTexture.reset();
        transition.fail("irradiance preprocess failed");
        return;
    }

    component.irradianceTexture = std::move(pendingState.pendingIrradianceOffscreen->outputTexture);
    pendingState.pendingIrradianceOffscreen.reset();
    transition.to(EEnvironmentLightingResolveState::Ready, "irradiance preprocess completed");
}
} // namespace

void ResourceResolveSystem::init()
{
    _equidistantCylindrical2CubeMap.init(App::get()->getRender());
    _cubeMap2IrradianceMap.init(App::get()->getRender());
}

void ResourceResolveSystem::clearPendingResolveStates()
{
    for (auto& [entity, pendingState] : _skyboxPendingStates) {
        (void)entity;
        clearSkyboxPendingState(pendingState);
    }
    for (auto& [entity, pendingState] : _environmentPendingStates) {
        (void)entity;
        clearEnvironmentLightingPendingState(pendingState);
    }
    _skyboxPendingStates.clear();
    _environmentPendingStates.clear();
    _pendingStateScene = nullptr;
}

void ResourceResolveSystem::shutdown()
{
    clearPendingResolveStates();
    _cubeMap2IrradianceMap.shutdown();
    _equidistantCylindrical2CubeMap.shutdown();
}

void ResourceResolveSystem::onUpdate(float dt)
{
    // YA_PROFILE_FUNCTION_LOG();
    (void)dt;
    auto  sceneManager = App::get()->getSceneManager();
    auto* scene        = sceneManager->getActiveScene();
    if (!scene) {
        clearPendingResolveStates();
        return;
    }
    if (_pendingStateScene != scene) {
        clearPendingResolveStates();
        _pendingStateScene = scene;
    }

    resolvePendingMeshes(scene);
    resolvePendingMaterials(scene);
    resolvePendingUI(scene);
    resolvePendingBillboards(scene);
    resolvePendingSkybox(scene);
    resolvePendingEnvironmentLighting(scene);

    // Add more component types here as needed:
    // - SkeletalMeshComponent
    // - etc.
}

void ResourceResolveSystem::resolvePendingEnvironmentLighting(Scene* scene)
{
    auto*           app                = App::get();
    auto*           render             = app->getRender();
    auto&           registry           = scene->getRegistry();
    stdptr<Texture> sceneSkyboxTexture = nullptr;

    for (auto&& [entity, sc] : registry.view<SkyboxComponent>().each()) {
        (void)entity;
        if (sc.hasRenderableCubemap()) {
            sceneSkyboxTexture = sc.cubemapTexture;
            break;
        }
    }

    for (auto&& [entity, elc] : registry.view<EnvironmentLightingComponent>().each()) {
        auto& pendingState = _environmentPendingStates[entity];
        if (!elc.hasSource()) {
            if (elc.resolveState != EEnvironmentLightingResolveState::Empty) {
                elc.invalidate();
                clearEnvironmentLightingPendingState(pendingState);
            }
            continue;
        }

        if (elc.resolveState == EEnvironmentLightingResolveState::Dirty ||
            elc.resolveState == EEnvironmentLightingResolveState::Empty) {
            clearEnvironmentLightingPendingState(pendingState);
        }

        syncEnvironmentLightingSceneSkyboxDependency(elc, pendingState, sceneSkyboxTexture);

        switch (elc.resolveState) {
        case EEnvironmentLightingResolveState::Dirty:
        case EEnvironmentLightingResolveState::ResolvingSource:
            checkEnvironmentLightingSourceLoad(elc, pendingState);
            break;

        case EEnvironmentLightingResolveState::PreprocessingEnvironment:
        {
            consumeEnvironmentCubemapPreprocess(elc, pendingState);
            if (elc.resolveState != EEnvironmentLightingResolveState::PreprocessingEnvironment ||
                !pendingState.pendingEnvironmentOffscreen ||
                !pendingState.pendingEnvironmentOffscreen->sourceTexture ||
                pendingState.pendingEnvironmentOffscreen->bTaskQueued) {
                break;
            }

            auto pending         = pendingState.pendingEnvironmentOffscreen;
            pending->bTaskQueued = true;

            const auto cubemapLabel = std::format("EnvironmentCubemap_{}", static_cast<uint32_t>(entity));
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
                    auto       outputTexture = createRenderableSkyboxCubemap(
                        render,
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
        } break;

        case EEnvironmentLightingResolveState::PreprocessingIrradiance:
            consumeEnvironmentIrradiancePreprocess(elc, pendingState);
            if (elc.resolveState != EEnvironmentLightingResolveState::PreprocessingIrradiance ||
                !pendingState.pendingIrradianceOffscreen ||
                !pendingState.pendingIrradianceOffscreen->sourceTexture ||
                pendingState.pendingIrradianceOffscreen->bTaskQueued) {
                break;
            }

            {
                auto pending         = pendingState.pendingIrradianceOffscreen;
                pending->bTaskQueued = true;

                const auto irradianceLabel = std::format("EnvironmentIrradiance_{}", static_cast<uint32_t>(entity));
                app->taskManager.enqueueOffscreenTask(
                    [this, render, pending, irradianceLabel, faceSize = elc.getResolvedIrradianceFaceSize()](ICommandBuffer* cmdBuf) {
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

                        const auto outputFaceSize = computeEnvironmentIrradianceFaceSize(pending->sourceTexture.get(), faceSize);
                        auto       outputTexture  = createRenderableSkyboxCubemap(
                            render,
                            irradianceLabel,
                            outputFaceSize,
                            chooseEnvironmentIrradianceFormat(pending->sourceTexture->getFormat()));
                        if (!outputTexture) {
                            failTask();
                            return;
                        }

                        auto executeResult = _cubeMap2IrradianceMap.execute(
                            CubeMap2IrradianceMap::ExecuteContext{
                                .cmdBuf = cmdBuf,
                                .input  = pending->sourceTexture.get(),
                                .output = outputTexture.get(),
                            });
                        if (!executeResult.bSuccess) {
                            DeferredDeletionQueue::get().retireResource(outputTexture);
                            failTask();
                            return;
                        }

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
            break;

        case EEnvironmentLightingResolveState::Empty:
        case EEnvironmentLightingResolveState::Ready:
        case EEnvironmentLightingResolveState::Failed:
        default:
            break;
        }
    }

    for (auto it = _environmentPendingStates.begin(); it != _environmentPendingStates.end();) {
        if (!registry.valid(it->first) || !registry.all_of<EnvironmentLightingComponent>(it->first)) {
            clearEnvironmentLightingPendingState(it->second);
            it = _environmentPendingStates.erase(it);
        }
        else {
            ++it;
        }
    }
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
    auto* app      = App::get();
    auto* render   = app->getRender();
    auto& registry = scene->getRegistry();

    for (auto&& [entity, sc] : registry.view<SkyboxComponent>().each()) {
        auto& pendingState = _skyboxPendingStates[entity];
        if (sc.resolveState == ESkyboxResolveState::Dirty ||
            sc.resolveState == ESkyboxResolveState::Empty) {
            clearSkyboxPendingState(pendingState);
        }

        if (!sc.hasSource()) {
            if (sc.resolveState != ESkyboxResolveState::Empty) {
                sc.invalidate();
                clearSkyboxPendingState(pendingState);
            }
            continue;
        }

        switch (sc.resolveState) {
        case ESkyboxResolveState::Dirty:
        case ESkyboxResolveState::ResolvingSource:
            checkSkyboxSourceLoad(sc, pendingState);
            break;

        case ESkyboxResolveState::Preprocessing:
        {
            consumeSkyboxPreprocess(sc, pendingState);
            if (sc.resolveState != ESkyboxResolveState::Preprocessing ||
                !pendingState.pendingOffscreenProcess ||
                !pendingState.pendingOffscreenProcess->sourceTexture ||
                pendingState.pendingOffscreenProcess->bTaskQueued) {
                break;
            }

            auto pending         = pendingState.pendingOffscreenProcess;
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
        } break;
        case ESkyboxResolveState::Empty:
        case ESkyboxResolveState::Ready:
        case ESkyboxResolveState::Failed:
        default:
            break;
        }
    }

    for (auto it = _skyboxPendingStates.begin(); it != _skyboxPendingStates.end();) {
        if (!registry.valid(it->first) || !registry.all_of<SkyboxComponent>(it->first)) {
            clearSkyboxPendingState(it->second);
            it = _skyboxPendingStates.erase(it);
        }
        else {
            ++it;
        }
    }
}

} // namespace ya


/*

StateMachine()
    .state(A, onTick)
    .state(B, onTick)
    .state(C, onTick)
    ....
    .transition(A, [](){
        some condition -> B
        some another condtion ->C
    })
    .build();

*/
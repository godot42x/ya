#include "ResourceResolveSystem.Detail.h"

#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Runtime/Application/App.h"
#include "Runtime/Application/Utility/OffscreenJobRunner.h"
#include "Scene/Scene.h"

#include <format>
#include <vector>

namespace ya
{

namespace
{

void clearSkyboxViews(SkyboxRuntimeState& state)
{
    for (auto& faceView : state.cubemapFacePreviewViews) {
        if (!faceView) {
            continue;
        }

        DeferredDeletionQueue::get().retireResource(std::move(faceView));
    }
}

std::string buildSkyboxDerivedKey(const SkyboxComponent& skybox, AssetManager* assets)
{
    if (!assets || !skybox.hasSource()) {
        return {};
    }

    if (skybox.hasCubemapSource()) {
        std::string key = std::format("skybox|cubefaces|flip={}", skybox.cubemapSource.flipVertical ? 1 : 0);
        for (const auto& path : skybox.cubemapSource.files) {
            const auto normalized = AssetManager::normalizeAssetPath(path);
            key += std::format("|{}|v{}", normalized, assets->getResourceVersion(normalized));
        }
        return key;
    }

    const auto normalized = AssetManager::normalizeAssetPath(skybox.cylindricalSource.filepath);
    return std::format("skybox|cyl|{}|v{}|flip={}",
                       normalized,
                       assets->getResourceVersion(normalized),
                       skybox.cylindricalSource.flipVertical ? 1 : 0);
}

void applySkyboxResource(const std::shared_ptr<SkyboxDerivedResource>& resource,
                         SkyboxRuntimeState&                           state)
{
    state.boundResource        = resource;
    state.cubemapRenderImage   = resource ? resource->cubemapRenderImage : nullptr;
    state.cubemapTexture       = resource ? resource->cubemapTexture : nullptr;
    state.sourcePreviewTexture = resource ? resource->sourcePreviewTexture : nullptr;
    state.cubemapFacePreviewViews.fill(nullptr);
    if (!resource) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        state.cubemapFacePreviewViews[faceIndex] = resource->cubemapFacePreviewViews[faceIndex];
    }
}

std::shared_ptr<SkyboxDerivedResource> snapshotSkyboxResource(const SkyboxRuntimeState& state)
{
    auto resource                    = std::make_shared<SkyboxDerivedResource>();
    resource->cubemapRenderImage     = state.cubemapRenderImage;
    resource->cubemapTexture         = state.cubemapTexture;
    resource->sourcePreviewTexture   = state.sourcePreviewTexture;
    resource->cubemapFacePreviewViews = state.cubemapFacePreviewViews;
    resource->lastUsedFrame          = App::currentFrameIndex();
    return resource;
}

} // namespace

namespace detail
{

void rebuildSkyboxViews(IRender* render, SkyboxRuntimeState& state)
{
    clearSkyboxViews(state);
    const auto cubemapImage = getImageShared(state.cubemapRenderImage, state.cubemapTexture);
    if (!cubemapImage || !getImageView(state.cubemapRenderImage, state.cubemapTexture)) {
        return;
    }

    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        state.cubemapFacePreviewViews[faceIndex] = resourceFactory->createImageView(
            cubemapImage,
            ImageViewCreateInfo{
                .label          = std::format("SkyboxPreviewFace{}", faceIndex),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = faceIndex,
                .layerCount     = 1,
            });
    }
}

void retireSkyboxResources(SkyboxRuntimeState& state)
{
    retireTexture(state.cubemapTexture);
    retireRenderImage(state.cubemapRenderImage);
    retireTexture(state.sourcePreviewTexture);
    clearSkyboxViews(state);
}

void resetSkyboxPending(SkyboxRuntimeState& state)
{
    state.pendingBatchLoadState.reset();
    state.pendingCylindricalFuture.reset();
    cancelOffscreenJob(state.pendingOffscreenProcess);
}

void resetSkyboxState(SkyboxRuntimeState& state)
{
    resetSkyboxPending(state);
    retireSkyboxResources(state);
    state.resultVersion = 0;
}

} // namespace detail

void ResourceResolveSystem::resolvePendingSkybox(Scene* scene)
{
    YA_PROFILE_FUNCTION();
    auto& registry = scene->getRegistry();
    auto* assets   = AssetManager::get();

    auto pumpOne = [&](entt::entity entity) {
        YA_PROFILE_SCOPE("ResourceResolve/Skybox/Entity");
        if (!registry.valid(entity) || !registry.all_of<SkyboxComponent>(entity)) {
            cleanupSkyboxState(entity);
            return;
        }

        auto& sc           = registry.get<SkyboxComponent>(entity);
        auto& pendingState = _skyboxStates[entity];
        const auto previousResultVersion = pendingState.resultVersion;
        const std::string derivedKey = buildSkyboxDerivedKey(sc, assets);
        // clear invalid version
        if (pendingState.authoringVersion != sc.authoringVersion) {
            detail::resetSkyboxState(pendingState);
            pendingState.authoringVersion = sc.authoringVersion;
            pendingState.lastStartedAuthoringVersion = sc.authoringVersion;
        }

        if (!sc.hasSource()) {
            if (pendingState.resolveState != ESkyboxResolveState::Empty) {
                makeTransition(pendingState.resolveState, "Skybox")
                    .to(ESkyboxResolveState::Empty, "no source");
                detail::resetSkyboxState(pendingState);
            }
            pendingState.derivedKey.clear();
            pendingState.boundResource.reset();
            pendingState.lastCompletedAuthoringVersion = sc.authoringVersion;
            _activeSkybox.erase(entity);
            return;
        }

        if (!derivedKey.empty() && pendingState.derivedKey != derivedKey &&
            pendingState.resultVersion > 0) {
            detail::resetSkyboxState(pendingState);
            pendingState.resultVersion = 0;
            makeTransition(pendingState.resolveState, "Skybox")
                .to(ESkyboxResolveState::Dirty, "derived key changed");
        }

        if (!derivedKey.empty()) {
            if (auto it = _skyboxDerivedResources.find(derivedKey); it != _skyboxDerivedResources.end() &&
                it->second && it->second->hasRenderableCubemap()) {
                const bool bCacheRebound = pendingState.resultVersion == 0 ||
                                           pendingState.derivedKey != derivedKey ||
                                           pendingState.boundResource.get() != it->second.get() ||
                                           pendingState.resolveState != ESkyboxResolveState::Ready;
                it->second->lastUsedFrame = App::currentFrameIndex();
                applySkyboxResource(it->second, pendingState);
                pendingState.derivedKey = derivedKey;
                if (bCacheRebound) {
                    makeTransition(pendingState.resolveState, "Skybox")
                        .to(ESkyboxResolveState::Ready, "derived cache hit");
                    ++pendingState.resultVersion;
                    markAllSceneSkyboxEnvironmentDependentsDirty("scene skybox projection rebound");
                }
                pendingState.lastCompletedAuthoringVersion = sc.authoringVersion;
                _activeSkybox.erase(entity);
                return;
            }
        }

        if (pendingState.resolveState == ESkyboxResolveState::Dirty ||
            pendingState.resolveState == ESkyboxResolveState::Empty) {
            detail::resetSkyboxPending(pendingState);
            pendingState.lastStartedAuthoringVersion = sc.authoringVersion;
        }

        auto transition = makeTransition(pendingState.resolveState, "Skybox");
        switch (pendingState.resolveState) {
        case ESkyboxResolveState::Dirty:
        {
            YA_PROFILE_SCOPE("ResourceResolve/Skybox/Dirty");
            if (sc.hasCubemapSource()) {
                std::vector<std::string> facePaths(sc.cubemapSource.files.begin(),
                                                   sc.cubemapSource.files.end());
                pendingState.pendingBatchLoadState =
                    std::make_shared<SkyboxPendingBatchLoadState>();
                pendingState.pendingBatchLoadState->batchHandle =
                    AssetManager::get()->loadTextureBatchIntoMemory(
                        AssetManager::TextureBatchMemoryLoadRequest{
                            .filepaths = facePaths,
                        });
            }
            else if (sc.hasCylindricalSource()) {
                pendingState.pendingCylindricalFuture =
                    AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
                        .filepath        = sc.cylindricalSource.filepath,
                        .name            = "SkyboxCylindricalSource",
                        .onReady         = {},
                        .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
                        .textureSemantic = std::nullopt,
                    });
            }
            transition.to(ESkyboxResolveState::ResolvingSource,
                          "source load requested");
        } break;

        case ESkyboxResolveState::ResolvingSource:
        {
            YA_PROFILE_SCOPE("ResourceResolve/Skybox/ResolvingSource");
            if (sc.hasCubemapSource()) {
                AssetManager::TextureBatchMemory batchMemory;

                if (!pendingState.pendingBatchLoadState) {
                    break;
                }
                if (!AssetManager::get()->consumeTextureBatchMemory(
                        pendingState.pendingBatchLoadState->batchHandle, batchMemory)) {
                    break;
                }

                pendingState.pendingBatchLoadState.reset();
                if (batchMemory.textures.size() != CubeFace_Count ||
                    !batchMemory.isValid()) {
                    detail::retireSkyboxResources(pendingState);
                    transition.fail("cubemap batch invalid");
                    break;
                }

                CubeMapMemoryCreateInfo createInfo;
                createInfo.label        = "SkyboxCubemap";
                createInfo.flipVertical = sc.cubemapSource.flipVertical;

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

                auto* render = getRender();
                auto cubemap = render ? Texture::createCubeMapFromMemory(*render, createInfo) : nullptr;
                if (!cubemap || !cubemap->isValid()) {
                    detail::retireSkyboxResources(pendingState);
                    transition.fail("cubemap creation failed");
                    break;
                }

                pendingState.cubemapTexture = std::move(cubemap);
                pendingState.cubemapRenderImage.reset();
                detail::rebuildSkyboxViews(getRender(), pendingState);
                pendingState.boundResource.reset();
                pendingState.derivedKey = derivedKey;
                ++pendingState.resultVersion;
                _skyboxDerivedResources[derivedKey] = snapshotSkyboxResource(pendingState);
                transition.to(ESkyboxResolveState::Ready, "cubemap source resolved");
                break;
            }
            else if (sc.hasCylindricalSource()) {
                if (!pendingState.pendingCylindricalFuture.has_value() ||
                    !pendingState.pendingCylindricalFuture->isReady()) {
                    pendingState.pendingCylindricalFuture =
                        AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
                            .filepath        = sc.cylindricalSource.filepath,
                            .name            = "SkyboxCylindricalSource",
                            .onReady         = {},
                            .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
                            .textureSemantic = std::nullopt,
                        });
                }
                if (!pendingState.pendingCylindricalFuture.has_value() ||
                    !pendingState.pendingCylindricalFuture->isReady()) {
                    break;
                }

                auto sourceTexture = pendingState.pendingCylindricalFuture->getShared();
                pendingState.pendingCylindricalFuture.reset();
                if (!sourceTexture || !sourceTexture->getImageView()) {
                    transition.fail("cylindrical source invalid");
                    break;
                }

                pendingState.sourcePreviewTexture = sourceTexture;

                // do the convert job
                auto job       = ya::makeShared<OffscreenJobState>();
                job->debugName = std::format("SkyboxCubemap_{}", static_cast<uint32_t>(entity));
                auto jobResult = job->result;
                job->executeFn = [&pipeline = getCylindrical2CubePipeline(),
                                  src       = sourceTexture,
                                  flipV     = sc.cylindricalSource.flipVertical,
                                  jobResult](
                                     ICommandBuffer* cmdBuf, RenderImage* output) -> bool
                {
                    auto result = pipeline.execute({
                        .cmdBuf        = cmdBuf,
                        .input         = src.get(),
                        .output        = output,
                        .bFlipVertical = flipV,
                    });
                    if (jobResult && !result.keepAliveResources.empty()) {
                        auto& retained = jobResult->retainedResources;
                        retained.insert(retained.end(),
                                        std::make_move_iterator(result.keepAliveResources.begin()),
                                        std::make_move_iterator(result.keepAliveResources.end()));
                    }
                    if (result.transientOutputArrayView) {
                        DeferredDeletionQueue::get().retireResource(
                            result.transientOutputArrayView);
                    }
                    return result.bSuccess;
                };
                job->createOutputFn = detail::makeCubemapOutputFn(
                    job->debugName, detail::computeSkyboxFaceSize(sourceTexture.get()), detail::chooseSkyboxCubemapFormat(sourceTexture->getFormat()));

                pendingState.pendingOffscreenProcess = std::move(job);
                transition.to(ESkyboxResolveState::Preprocessing,
                              "queue cylindrical preprocess");
                break;
            }

            detail::resetSkyboxPending(pendingState);
            transition.to(ESkyboxResolveState::Empty,
                          "active source changed while resolving");
        } break;

        case ESkyboxResolveState::Preprocessing:
        {
            YA_PROFILE_SCOPE("ResourceResolve/Skybox/Preprocessing");
            if (!pendingState.pendingOffscreenProcess) {
                transition.fail("preprocess job missing");
                break;
            }

            if (pendingState.pendingOffscreenProcess->phase == EOffscreenJobPhase::Pending) {
                detail::tryQueueJob(getOffscreenJobQueueService(), getRender(), pendingState.pendingOffscreenProcess);
                break;
            }

            if (pendingState.pendingOffscreenProcess->phase == EOffscreenJobPhase::Queued ||
                pendingState.pendingOffscreenProcess->phase == EOffscreenJobPhase::Recorded) {
                break;
            }

            if (pendingState.pendingOffscreenProcess->hasFailed() ||
                !pendingState.pendingOffscreenProcess->result ||
                !pendingState.pendingOffscreenProcess->result->outputImage) {
                pendingState.pendingOffscreenProcess.reset();
                detail::retireSkyboxResources(pendingState);
                transition.fail("preprocess failed");
                break;
            }

            if (!pendingState.pendingOffscreenProcess->isGpuCompleted()) {
                break;
            }

            pendingState.cubemapRenderImage = pendingState.pendingOffscreenProcess->result->outputImage;
            detail::retireTextureNow(pendingState.cubemapTexture);
            pendingState.pendingOffscreenProcess.reset();
            detail::rebuildSkyboxViews(getRender(), pendingState);
            pendingState.boundResource.reset();
            pendingState.derivedKey = derivedKey;
            ++pendingState.resultVersion;
            _skyboxDerivedResources[derivedKey] = snapshotSkyboxResource(pendingState);
            makeTransition(pendingState.resolveState, "Skybox")
                .to(ESkyboxResolveState::Ready, "preprocess completed");
        } break;

        case ESkyboxResolveState::Empty:
        case ESkyboxResolveState::Ready:
        case ESkyboxResolveState::Failed:
        default:
            break;
        }

        const bool bActive = pendingState.resolveState == ESkyboxResolveState::ResolvingSource ||
                             pendingState.resolveState == ESkyboxResolveState::Preprocessing;
        if (bActive) {
            _activeSkybox.insert(entity);
        }
        else {
            pendingState.lastCompletedAuthoringVersion = sc.authoringVersion;
            _activeSkybox.erase(entity);
        }

        if (pendingState.resultVersion != previousResultVersion) {
            markAllSceneSkyboxEnvironmentDependentsDirty("scene skybox result changed");
        }
    };

    while (!_dirtySkyboxQueue.empty()) {
        const auto entity = _dirtySkyboxQueue.front();
        _dirtySkyboxQueue.pop_front();
        _dirtySkyboxSet.erase(entity);
        pumpOne(entity);
    }

    std::vector<entt::entity> activeEntities(_activeSkybox.begin(), _activeSkybox.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }
}

} // namespace ya

#include "ResourceResolveSystem.Detail.h"

#include "Render/Core/TextureFactory.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Runtime/App/OffscreenJobRunner.h"
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

} // namespace

namespace detail
{

void rebuildSkyboxViews(SkyboxRuntimeState& state)
{
    clearSkyboxViews(state);
    if (!state.cubemapTexture || !state.cubemapTexture->getImageShared() ||
        !state.cubemapTexture->getImageView()) {
        return;
    }

    auto* textureFactory = ITextureFactory::get();
    if (!textureFactory) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        state.cubemapFacePreviewViews[faceIndex] = textureFactory->createImageView(
            state.cubemapTexture->getImageShared(),
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
    auto& registry = scene->getRegistry();

    for (auto&& [entity, sc] : registry.view<SkyboxComponent>().each()) {

        auto& pendingState = _skyboxStates[entity];
        // clear invalid version
        if (pendingState.authoringVersion != sc.authoringVersion) {
            detail::resetSkyboxState(pendingState);
            pendingState.authoringVersion = sc.authoringVersion;
        }

        if (!sc.hasSource()) {
            if (sc.resolveState != ESkyboxResolveState::Empty) {
                makeTransition(sc.resolveState, "Skybox")
                    .to(ESkyboxResolveState::Empty, "no source");
                detail::resetSkyboxState(pendingState);
            }
            continue;
        }

        if (sc.resolveState == ESkyboxResolveState::Dirty ||
            sc.resolveState == ESkyboxResolveState::Empty) {
            detail::resetSkyboxPending(pendingState);
        }

        auto transition = makeTransition(sc.resolveState, "Skybox");
        switch (sc.resolveState) {
        case ESkyboxResolveState::Dirty:
        {
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

                auto cubemap = Texture::createCubeMapFromMemory(createInfo);
                if (!cubemap || !cubemap->isValid()) {
                    detail::retireSkyboxResources(pendingState);
                    transition.fail("cubemap creation failed");
                    break;
                }

                pendingState.cubemapTexture = std::move(cubemap);
                detail::rebuildSkyboxViews(pendingState);
                ++pendingState.resultVersion;
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
                job->executeFn = [&pipeline = getCylindrical2CubePipeline(),
                                  src       = sourceTexture,
                                  flipV     = sc.cylindricalSource.flipVertical](
                                     ICommandBuffer* cmdBuf, Texture* output) -> bool {
                    auto result = pipeline.execute({
                        .cmdBuf        = cmdBuf,
                        .input         = src.get(),
                        .output        = output,
                        .bFlipVertical = flipV,
                    });
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
            // not completed
            if (!pendingState.pendingOffscreenProcess ||
                !pendingState.pendingOffscreenProcess->bTaskFinished) {
                detail::tryQueueJob(pendingState.pendingOffscreenProcess);
                break;
            }

            // failed
            if (!pendingState.pendingOffscreenProcess->bTaskSucceeded ||
                !pendingState.pendingOffscreenProcess->result ||
                !pendingState.pendingOffscreenProcess->result->outputTexture) {
                pendingState.pendingOffscreenProcess.reset();
                detail::retireSkyboxResources(pendingState);
                transition.fail("preprocess failed");
                break;
            }

            pendingState.cubemapTexture = std::move(pendingState.pendingOffscreenProcess->result->outputTexture);
            pendingState.pendingOffscreenProcess.reset();
            detail::rebuildSkyboxViews(pendingState);
            ++pendingState.resultVersion;
            makeTransition(sc.resolveState, "Skybox")
                .to(ESkyboxResolveState::Ready, "preprocess completed");
        } break;

        case ESkyboxResolveState::Empty:
        case ESkyboxResolveState::Ready:
        case ESkyboxResolveState::Failed:
        default:
            break;
        }
    }

    for (auto it = _skyboxStates.begin(); it != _skyboxStates.end();) {
        if (!registry.valid(it->first) ||
            !registry.all_of<SkyboxComponent>(it->first)) {
            detail::resetSkyboxState(it->second);
            it = _skyboxStates.erase(it);
        }
        else {
            ++it;
        }
    }
}

} // namespace ya
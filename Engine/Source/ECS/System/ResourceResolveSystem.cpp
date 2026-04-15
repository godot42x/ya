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



#include "Render/Core/TextureFactory.h"
#include "Resource/DeferredDeletionQueue.h"



namespace ya
{

namespace
{
void retireTexture(stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    auto& ddq = DeferredDeletionQueue::get();
    ddq.enqueueResource(ddq.currentFrame(), std::move(texture));
    texture = nullptr;
}

void retireTextureNow(stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    DeferredDeletionQueue::get().retireResource(texture);
    texture.reset();
}

void cancelJob(std::shared_ptr<OffscreenPrecomputeJobState>& job)
{
    if (!job) {
        return;
    }

    job->bCancelled = true;
    retireTextureNow(job->outputTexture);
    job.reset();
}

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
    (void)sourceFormat;
    return EFormat::R16G16B16A16_SFLOAT;
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
                                              EFormat::T         format,
                                              int                mips = -1)
{
    auto* textureFactory = render ? render->getTextureFactory() : nullptr;
    if (!textureFactory || faceSize == 0 || format == EFormat::Undefined) {
        return nullptr;
    }
    ImageCreateInfo ci{
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
    };
    if (mips > 0) {
        ci.mipLevels = mips;
        ci.usage     = static_cast<EImageUsage::T>(ci.usage | EImageUsage::TransferDst | EImageUsage::TransferSrc);
    }

    auto image = textureFactory->createImage(ci);
    if (!image) {
        return nullptr;
    }

    auto cubeView = textureFactory->createCubeMapImageView(image, EImageAspect::Color);
    if (!cubeView) {
        return nullptr;
    }

    return Texture::wrap(image, cubeView, label);
}

OffscreenPrecomputeOutputSpec makeCubemapSpec(const std::string& label,
                                              uint32_t           faceSize,
                                              EFormat::T         format,
                                              int                mipLevels = 1)
{
    return OffscreenPrecomputeOutputSpec{
        .outputType = EOffscreenPrecomputeOutputType::Cubemap,
        .label      = label,
        .width      = faceSize,
        .height     = faceSize,
        .format     = format,
        .mipLevels  = mipLevels,
        .layerCount = CubeFace_Count,
    };
}

stdptr<Texture> createJobTexture(IRender* const render, const OffscreenPrecomputeOutputSpec& spec)
{
    if (!render || !spec.isValid()) {
        return nullptr;
    }

    if (spec.outputType == EOffscreenPrecomputeOutputType::Cubemap) {
        return createRenderableSkyboxCubemap(render, spec.label, spec.width, spec.format, spec.mipLevels);
    }

    return Texture::createRenderTexture(RenderTextureCreateInfo{
        .label      = spec.label,
        .width      = spec.width,
        .height     = spec.height,
        .format     = spec.format,
        .usage      = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::Sampled),
        .samples    = ESampleCount::Sample_1,
        .isDepth    = false,
        .layerCount = spec.layerCount,
        .mipLevels  = static_cast<uint32_t>(spec.mipLevels),
    });
}

TextureFuture requestSkyboxCyl(const SkyboxComponent& component)
{
    return AssetManager::get()->loadTexture(
        AssetManager::TextureLoadRequest{
            .filepath        = component.cylindricalSource.filepath,
            .name            = "SkyboxCylindricalSource",
            .onReady         = {},
            .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
            .textureSemantic = std::nullopt,
        });
}

TextureFuture requestEnvCyl(const EnvironmentLightingComponent& component)
{
    return AssetManager::get()->loadTexture(
        AssetManager::TextureLoadRequest{
            .filepath        = component.cylindricalSource.filepath,
            .name            = "EnvironmentLightingCylindricalSource",
            .onReady         = {},
            .colorSpace      = AssetManager::ETextureColorSpace::Linear,
            .textureSemantic = std::nullopt,
        });
}

// ── Common helpers ───────────────────────────────────────────────────

void clearSkyboxViews(SkyboxRuntimeState& state)
{
    for (auto& faceView : state.cubemapFacePreviewViews) {
        if (!faceView) {
            continue;
        }

        DeferredDeletionQueue::get().retireResource(std::move(faceView));
    }
}

void retireSkyboxResources(SkyboxRuntimeState& state)
{
    retireTexture(state.cubemapTexture);
    retireTexture(state.sourcePreviewTexture);
    clearSkyboxViews(state);
}

void rebuildSkyboxViews(SkyboxRuntimeState& state)
{
    clearSkyboxViews(state);
    if (!state.cubemapTexture || !state.cubemapTexture->getImageShared() || !state.cubemapTexture->getImageView()) {
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

void resetSkyboxPending(SkyboxRuntimeState& state)
{
    state.pendingBatchLoad.reset();
    state.pendingCylindricalFuture.reset();
    cancelJob(state.pendingOffscreenProcess);
}

void resetSkyboxState(SkyboxRuntimeState& state)
{
    resetSkyboxPending(state);
    retireSkyboxResources(state);
    state.resultVersion = 0;
}

void retireEnvTextures(EnvironmentLightingRuntimeState& state)
{
    retireTexture(state.cubemapTexture);
    retireTexture(state.irradianceTexture);
}

void resetEnvPending(EnvironmentLightingRuntimeState& state)
{
    state.pendingBatchLoad.reset();
    state.pendingCylindricalFuture.reset();
    state.lastSceneSkyboxResultVersion = 0;
    cancelJob(state.pendingEnvironmentOffscreen);
    cancelJob(state.pendingIrradianceOffscreen);
}

void resetEnvState(EnvironmentLightingRuntimeState& state)
{
    resetEnvPending(state);
    retireEnvTextures(state);
    state.resultVersion = 0;
}

// ── Generic resolve helpers (Phase 3: template-based deduplication) ──

/// Check if an offscreen job has finished and extract its output texture.
/// Returns the outputTexture on success, nullptr if not yet finished or failed.
/// On failure, calls onFail and transitions to Failed state.
template <typename TResolveState>
stdptr<Texture> consumeFinishedJob(
    TResolveState&                                resolveState,
    std::shared_ptr<OffscreenPrecomputeJobState>& job,
    TResolveState                                 expectedState,
    std::string_view                              label,
    std::function<void()>                         onFail = {})
{
    auto transition = makeTransition(resolveState, label);
    if (resolveState != expectedState || !job || !job->bTaskFinished) {
        return nullptr;
    }

    if (!job->bTaskSucceeded || !job->outputTexture) {
        job.reset();
        if (onFail) onFail();
        transition.fail("preprocess failed");
        return nullptr;
    }

    auto output = std::move(job->outputTexture);
    job.reset();
    return output;
}

/// Check authoring version and source availability.
/// Resets state on version mismatch. Returns true if the component has a source and resolve should continue.
template <typename TComponent, typename TState, typename TEnum, typename FnResetState, typename FnResetPending>
bool checkVersionAndSource(
    TComponent&      component,
    TState&          state,
    TEnum            emptyState,
    TEnum            dirtyState,
    std::string_view label,
    FnResetState     resetStateFn,
    FnResetPending   resetPendingFn)
{
    if (state.authoringVersion != component.authoringVersion) {
        resetStateFn(state);
        state.authoringVersion = component.authoringVersion;
    }

    if (!component.hasSource()) {
        if (component.resolveState != emptyState) {
            makeTransition(component.resolveState, label).to(emptyState, "no source");
            resetStateFn(state);
        }
        return false;
    }

    if (component.resolveState == dirtyState || component.resolveState == emptyState) {
        resetPendingFn(state);
    }
    return true;
}

// ── Skybox resolve ───────────────────────────────────────────────────

void stepSkyboxSource(ResourceResolveSystem& system,
                      entt::entity           entity,
                      SkyboxComponent&       component,
                      SkyboxRuntimeState&    state)
{
    auto transition = makeTransition(component.resolveState, "Skybox");
    if (component.resolveState == ESkyboxResolveState::Dirty) {
        if (component.hasCubemapSource()) {
            std::vector<std::string> facePaths(component.cubemapSource.files.begin(), component.cubemapSource.files.end());
            state.pendingBatchLoad              = std::make_shared<SkyboxPendingBatchLoadState>();
            state.pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
                AssetManager::TextureBatchMemoryLoadRequest{
                    .filepaths = facePaths,
                });
        }
        else if (component.hasCylindricalSource()) {
            state.pendingCylindricalFuture = requestSkyboxCyl(component);
        }
        transition.to(ESkyboxResolveState::ResolvingSource, "source load requested");
        return;
    }

    if (component.resolveState != ESkyboxResolveState::ResolvingSource) {
        return;
    }

    if (component.hasCubemapSource()) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!state.pendingBatchLoad ||
            !AssetManager::get()->consumeTextureBatchMemory(state.pendingBatchLoad->batchHandle, batchMemory)) {
            return;
        }

        state.pendingBatchLoad.reset();
        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            retireSkyboxResources(state);
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
            retireSkyboxResources(state);
            transition.fail("cubemap creation failed");
            return;
        }

        state.cubemapTexture = std::move(cubemap);
        rebuildSkyboxViews(state);
        ++state.resultVersion;
        transition.to(ESkyboxResolveState::Ready, "cubemap source resolved");
        return;
    }

    if (component.hasCylindricalSource()) {
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            state.pendingCylindricalFuture = requestSkyboxCyl(component);
        }
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            return;
        }

        auto sourceTexture = state.pendingCylindricalFuture->getShared();
        state.pendingCylindricalFuture.reset();
        if (!sourceTexture || !sourceTexture->getImageView()) {
            transition.fail("cylindrical source invalid");
            return;
        }

        state.sourcePreviewTexture = sourceTexture;

        // Create a self-describing job — the lambda captures the pipeline directly.
        auto job      = std::make_shared<OffscreenPrecomputeJobState>();
        job->executeFn = [&pipeline = system.getCylindrical2CubePipeline(),
                          src       = sourceTexture.get(),
                          flipV     = component.cylindricalSource.flipVertical]
                         (ICommandBuffer* cmdBuf, Texture* output) -> bool {
            auto result = pipeline.execute({
                .cmdBuf        = cmdBuf,
                .input         = src,
                .output        = output,
                .bFlipVertical = flipV,
            });
            if (result.transientOutputArrayView) {
                DeferredDeletionQueue::get().retireResource(result.transientOutputArrayView);
            }
            return result.bSuccess;
        };
        job->outputSpec    = makeCubemapSpec(
            std::format("SkyboxCubemap_{}", static_cast<uint32_t>(entity)),
            computeSkyboxFaceSize(sourceTexture.get()),
            chooseSkyboxCubemapFormat(sourceTexture->getFormat()));
        job->sourceTexture = std::move(sourceTexture);

        state.pendingOffscreenProcess = std::move(job);
        transition.to(ESkyboxResolveState::Preprocessing, "queue cylindrical preprocess");
        return;
    }

    resetSkyboxPending(state);
    transition.to(ESkyboxResolveState::Empty, "active source changed while resolving");
}

void tryQueueJob(ResourceResolveSystem& system, const std::shared_ptr<OffscreenPrecomputeJobState>& job)
{
    if (!job || !job->isReadyToQueue()) {
        return;
    }

    system.queueOffscreenPrecomputeJob(job);
}

void stepSkybox(ResourceResolveSystem& system,
                entt::entity           entity,
                SkyboxComponent&       component,
                SkyboxRuntimeState&    state)
{
    if (!checkVersionAndSource(
            component, state,
            ESkyboxResolveState::Empty, ESkyboxResolveState::Dirty,
            "Skybox", resetSkyboxState, resetSkyboxPending)) {
        return;
    }

    switch (component.resolveState) {
    case ESkyboxResolveState::Dirty:
    case ESkyboxResolveState::ResolvingSource:
        stepSkyboxSource(system, entity, component, state);
        break;

    case ESkyboxResolveState::Preprocessing:
        if (auto tex = consumeFinishedJob(
                component.resolveState,
                state.pendingOffscreenProcess,
                ESkyboxResolveState::Preprocessing,
                "Skybox",
                [&] { retireSkyboxResources(state); })) {
            state.cubemapTexture = std::move(tex);
            rebuildSkyboxViews(state);
            ++state.resultVersion;
            makeTransition(component.resolveState, "Skybox").to(ESkyboxResolveState::Ready, "preprocess completed");
        }
        else if (component.resolveState == ESkyboxResolveState::Preprocessing) {
            tryQueueJob(system, state.pendingOffscreenProcess);
        }
        break;

    case ESkyboxResolveState::Empty:
    case ESkyboxResolveState::Ready:
    case ESkyboxResolveState::Failed:
    default:
        break;
    }
}

// ── Environment lighting resolve ─────────────────────────────────────

void beginEnvIrradiance(ResourceResolveSystem&          system,
                        entt::entity                    entity,
                        EnvironmentLightingComponent&   component,
                        EnvironmentLightingRuntimeState& state,
                        stdptr<Texture>                 sourceCubemap)
{
    auto transition = makeTransition(component.resolveState, "EnvironmentLighting");
    if (!sourceCubemap || !sourceCubemap->getImageView()) {
        transition.fail("irradiance source invalid");
        return;
    }

    retireTextureNow(state.irradianceTexture);

    auto job       = std::make_shared<OffscreenPrecomputeJobState>();
    job->executeFn = [&pipeline = system.getCube2IrradiancePipeline(),
                      src       = sourceCubemap.get()]
                     (ICommandBuffer* cmdBuf, Texture* output) -> bool {
        auto result = pipeline.execute({
            .cmdBuf = cmdBuf,
            .input  = src,
            .output = output,
        });
        return result.bSuccess;
    };
    job->outputSpec    = makeCubemapSpec(
        std::format("EnvironmentIrradiance_{}", static_cast<uint32_t>(entity)),
        computeEnvironmentIrradianceFaceSize(sourceCubemap.get(), component.getResolvedIrradianceFaceSize()),
        chooseEnvironmentIrradianceFormat(sourceCubemap->getFormat()));
    job->sourceTexture = std::move(sourceCubemap);

    state.pendingIrradianceOffscreen = std::move(job);
    transition.to(EEnvironmentLightingResolveState::PreprocessingIrradiance, "queue irradiance preprocess");
}

stdptr<Texture> syncEnvSkybox(EnvironmentLightingComponent&   component,
                              EnvironmentLightingRuntimeState& state,
                              const SkyboxRuntimeState*       sceneSkyboxState)
{
    if (!component.usesSceneSkybox()) {
        return nullptr;
    }

    const uint64_t currentResultVersion = sceneSkyboxState ? sceneSkyboxState->resultVersion : 0;
    if (state.lastSceneSkyboxResultVersion != currentResultVersion) {
        resetEnvState(state);
        state.lastSceneSkyboxResultVersion = currentResultVersion;
        auto transition = makeTransition(component.resolveState, "EnvironmentLighting");
        transition.to(currentResultVersion == 0 ? EEnvironmentLightingResolveState::Empty : EEnvironmentLightingResolveState::Dirty,
                      "scene skybox dependency changed");
    }

    if (sceneSkyboxState && sceneSkyboxState->hasRenderableCubemap() &&
        component.resolveState == EEnvironmentLightingResolveState::Dirty) {
        return sceneSkyboxState->cubemapTexture;
    }

    return nullptr;
}

stdptr<Texture> stepEnvSource(ResourceResolveSystem&          system,
                              entt::entity                    entity,
                              EnvironmentLightingComponent&   component,
                              EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(component.resolveState, "EnvironmentLighting");

    if (component.resolveState == EEnvironmentLightingResolveState::Dirty) {
        if (component.hasCubemapSource()) {
            std::vector<std::string> facePaths(component.cubemapSource.files.begin(), component.cubemapSource.files.end());
            state.pendingBatchLoad              = std::make_shared<EnvironmentLightingPendingBatchLoadState>();
            state.pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
                AssetManager::TextureBatchMemoryLoadRequest{
                    .filepaths  = facePaths,
                    .colorSpace = AssetManager::ETextureColorSpace::Linear,
                });
            transition.to(EEnvironmentLightingResolveState::ResolvingSource, "source load requested");
        }
        else if (component.hasCylindricalSource()) {
            state.pendingCylindricalFuture = requestEnvCyl(component);
            transition.to(EEnvironmentLightingResolveState::ResolvingSource, "source load requested");
        }
        return nullptr;
    }

    if (component.resolveState != EEnvironmentLightingResolveState::ResolvingSource) {
        return nullptr;
    }

    if (component.hasCubemapSource()) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!state.pendingBatchLoad ||
            !AssetManager::get()->consumeTextureBatchMemory(state.pendingBatchLoad->batchHandle, batchMemory)) {
            return nullptr;
        }

        state.pendingBatchLoad.reset();
        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            retireEnvTextures(state);
            transition.fail("cubemap batch invalid");
            return nullptr;
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
            retireEnvTextures(state);
            transition.fail("cubemap creation failed");
            return nullptr;
        }

        state.cubemapTexture = std::move(cubemap);
        return state.cubemapTexture;
    }

    if (component.hasCylindricalSource()) {
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            state.pendingCylindricalFuture = requestEnvCyl(component);
        }
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            return nullptr;
        }

        auto sourceTexture = state.pendingCylindricalFuture->getShared();
        state.pendingCylindricalFuture.reset();
        if (!sourceTexture || !sourceTexture->getImageView()) {
            retireEnvTextures(state);
            transition.fail("cylindrical source invalid");
            return nullptr;
        }

        // Self-describing job for cylindrical→cubemap conversion
        auto job       = std::make_shared<OffscreenPrecomputeJobState>();
        job->executeFn = [&pipeline = system.getCylindrical2CubePipeline(),
                          src       = sourceTexture.get(),
                          flipV     = component.cylindricalSource.flipVertical]
                         (ICommandBuffer* cmdBuf, Texture* output) -> bool {
            auto result = pipeline.execute({
                .cmdBuf        = cmdBuf,
                .input         = src,
                .output        = output,
                .bFlipVertical = flipV,
            });
            if (result.transientOutputArrayView) {
                DeferredDeletionQueue::get().retireResource(result.transientOutputArrayView);
            }
            return result.bSuccess;
        };
        job->outputSpec    = makeCubemapSpec(
            std::format("EnvironmentCubemap_{}", static_cast<uint32_t>(entity)),
            computeSkyboxFaceSize(sourceTexture.get()),
            chooseSkyboxCubemapFormat(sourceTexture->getFormat()));
        job->sourceTexture = std::move(sourceTexture);

        state.pendingEnvironmentOffscreen = std::move(job);
        transition.to(EEnvironmentLightingResolveState::PreprocessingEnvironment,
                      "queue environment preprocess");
    }

    return nullptr;
}

void stepEnvLighting(ResourceResolveSystem&          system,
                     entt::entity                    entity,
                     EnvironmentLightingComponent&   component,
                     EnvironmentLightingRuntimeState& state,
                     const SkyboxRuntimeState*       sceneSkyboxState)
{
    if (!checkVersionAndSource(
            component, state,
            EEnvironmentLightingResolveState::Empty,
            EEnvironmentLightingResolveState::Dirty,
            "EnvironmentLighting", resetEnvState, resetEnvPending)) {
        return;
    }

    if (const auto sourceCubemap = syncEnvSkybox(component, state, sceneSkyboxState)) {
        beginEnvIrradiance(system, entity, component, state, sourceCubemap);
        return;
    }

    switch (component.resolveState) {
    case EEnvironmentLightingResolveState::Dirty:
    case EEnvironmentLightingResolveState::ResolvingSource:
        if (const auto sourceCubemap = stepEnvSource(system, entity, component, state)) {
            beginEnvIrradiance(system, entity, component, state, sourceCubemap);
        }
        break;

    case EEnvironmentLightingResolveState::PreprocessingEnvironment:
        if (auto tex = consumeFinishedJob(
                component.resolveState,
                state.pendingEnvironmentOffscreen,
                EEnvironmentLightingResolveState::PreprocessingEnvironment,
                "EnvironmentLighting",
                [&] { retireEnvTextures(state); })) {
            state.cubemapTexture = std::move(tex);
            beginEnvIrradiance(system, entity, component, state, state.cubemapTexture);
        }
        else if (component.resolveState == EEnvironmentLightingResolveState::PreprocessingEnvironment) {
            tryQueueJob(system, state.pendingEnvironmentOffscreen);
        }
        break;

    case EEnvironmentLightingResolveState::PreprocessingIrradiance:
        if (auto tex = consumeFinishedJob(
                component.resolveState,
                state.pendingIrradianceOffscreen,
                EEnvironmentLightingResolveState::PreprocessingIrradiance,
                "EnvironmentLighting",
                [&] { retireTextureNow(state.irradianceTexture); })) {
            state.irradianceTexture = std::move(tex);
            ++state.resultVersion;
            makeTransition(component.resolveState, "EnvironmentLighting")
                .to(EEnvironmentLightingResolveState::Ready, "irradiance preprocess completed");
        }
        else if (component.resolveState == EEnvironmentLightingResolveState::PreprocessingIrradiance) {
            tryQueueJob(system, state.pendingIrradianceOffscreen);
        }
        break;

    case EEnvironmentLightingResolveState::Empty:
    case EEnvironmentLightingResolveState::Ready:
    case EEnvironmentLightingResolveState::Failed:
    default:
        break;
    }
}

// ── Material resolve (unchanged) ─────────────────────────────────────

template <typename TMaterialComponent>
void resolvePendingMaterialComponents(entt::registry& registry)
{
    registry.view<TMaterialComponent>().each([](auto entity, TMaterialComponent& materialComponent) {
        (void)entity;
        if (materialComponent.needsResolve()) {
            materialComponent.resolve();
        }
        else if (materialComponent.isResolved()) {
            materialComponent.checkTexturesStaleness();
        }
    });
}

template <typename TComponent, typename TRuntimeState, typename TMap, typename TClearFn>
void eraseInvalidRuntimeStates(entt::registry& registry, TMap& states, TClearFn clearFn)
{
    for (auto it = states.begin(); it != states.end();) {
        if (!registry.valid(it->first) || !registry.all_of<TComponent>(it->first)) {
            clearFn(it->second);
            it = states.erase(it);
        }
        else {
            ++it;
        }
    }
}
} // namespace

void ResourceResolveSystem::init()
{
    _equidistantCylindrical2CubeMap.init(App::get()->getRender());
    _cubeMap2IrradianceMap.init(App::get()->getRender());
}

void ResourceResolveSystem::clearPendingResolveStates()
{
    for (auto& [entity, pendingState] : _skyboxStates) {
        (void)entity;
        resetSkyboxState(pendingState);
    }
    for (auto& [entity, pendingState] : _environmentStates) {
        (void)entity;
        resetEnvState(pendingState);
    }
    _skyboxStates.clear();
    _environmentStates.clear();
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
}

void ResourceResolveSystem::queueOffscreenPrecomputeJob(const std::shared_ptr<OffscreenPrecomputeJobState>& job)
{
    auto* const app    = App::get();
    auto* const render = app ? app->getRender() : nullptr;
    if (!job || !app || !render || !job->isReadyToQueue()) {
        if (job) {
            job->bTaskFinished  = true;
            job->bTaskSucceeded = false;
        }
        return;
    }

    // Create the output texture on the main thread, before recording commands.
    job->outputTexture = createJobTexture(render, job->outputSpec);
    if (!job->outputTexture) {
        job->bTaskFinished  = true;
        job->bTaskSucceeded = false;
        return;
    }

    job->bTaskQueued = true;
    app->taskManager.enqueueOffscreenTask(
        [job](ICommandBuffer* cmdBuf) {
            if (!job || job->bCancelled || !cmdBuf || !job->executeFn) {
                if (job) {
                    job->bTaskFinished  = true;
                    job->bTaskSucceeded = false;
                }
                return;
            }

            bool bSuccess = job->executeFn(cmdBuf, job->outputTexture.get());

            if (!bSuccess || job->bCancelled) {
                DeferredDeletionQueue::get().retireResource(job->outputTexture);
                job->outputTexture  = nullptr;
                job->bTaskFinished  = true;
                job->bTaskSucceeded = false;
                return;
            }

            job->bTaskFinished  = true;
            job->bTaskSucceeded = true;
        });
}

void ResourceResolveSystem::resolvePendingEnvironmentLighting(Scene* scene)
{
    auto&       registry         = scene->getRegistry();
    const auto* sceneSkyboxState = findFirstSceneSkyboxState(scene);

    for (auto&& [entity, elc] : registry.view<EnvironmentLightingComponent>().each()) {
        auto& pendingState = _environmentStates[entity];
        stepEnvLighting(*this, entity, elc, pendingState, sceneSkyboxState);
    }

    eraseInvalidRuntimeStates<EnvironmentLightingComponent, EnvironmentLightingRuntimeState>(
        registry,
        _environmentStates,
        [](EnvironmentLightingRuntimeState& state) {
            resetEnvState(state);
        });
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

    resolvePendingMaterialComponents<PhongMaterialComponent>(registry);
    resolvePendingMaterialComponents<PBRMaterialComponent>(registry);
    resolvePendingMaterialComponents<UnlitMaterialComponent>(registry);
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
    auto& registry = scene->getRegistry();

    for (auto&& [entity, sc] : registry.view<SkyboxComponent>().each()) {
        auto& pendingState = _skyboxStates[entity];
        stepSkybox(*this, entity, sc, pendingState);
    }

    eraseInvalidRuntimeStates<SkyboxComponent, SkyboxRuntimeState>(
        registry,
        _skyboxStates,
        [](SkyboxRuntimeState& state) {
            resetSkyboxState(state);
        });
}

const SkyboxRuntimeState* ResourceResolveSystem::findSkyboxState(entt::entity entity) const
{
    const auto it = _skyboxStates.find(entity);
    return it == _skyboxStates.end() ? nullptr : &it->second;
}

const SkyboxRuntimeState* ResourceResolveSystem::findFirstSceneSkyboxState(Scene* scene) const
{
    if (!scene) {
        return nullptr;
    }

    for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
        const auto* state = findSkyboxState(entity);
        if (sc.resolveState == ESkyboxResolveState::Ready && state && state->hasRenderableCubemap()) {
            return state;
        }
    }

    return nullptr;
}

const EnvironmentLightingRuntimeState* ResourceResolveSystem::findEnvironmentLightingState(entt::entity entity) const
{
    const auto it = _environmentStates.find(entity);
    return it == _environmentStates.end() ? nullptr : &it->second;
}

const EnvironmentLightingRuntimeState* ResourceResolveSystem::findFirstSceneEnvironmentLightingState(Scene* scene) const
{
    if (!scene) {
        return nullptr;
    }

    for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
        const auto* state = findEnvironmentLightingState(entity);
        if (elc.resolveState == EEnvironmentLightingResolveState::Ready && state && state->hasIrradianceMap()) {
            return state;
        }
    }

    return nullptr;
}

Texture* ResourceResolveSystem::findSceneSkyboxTexture(Scene* scene) const
{
    const auto* state = findFirstSceneSkyboxState(scene);
    return state ? state->cubemapTexture.get() : nullptr;
}

Texture* ResourceResolveSystem::findSceneEnvironmentCubemapTexture(Scene* scene) const
{
    if (!scene) {
        return nullptr;
    }

    const auto* skyboxState = findFirstSceneSkyboxState(scene);
    for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
        (void)entity;
        const auto* state = findEnvironmentLightingState(entity);
        if (!state || elc.resolveState != EEnvironmentLightingResolveState::Ready) {
            continue;
        }

        if (elc.usesSceneSkybox()) {
            return skyboxState ? skyboxState->cubemapTexture.get() : nullptr;
        }

        if (state->hasRenderableCubemap()) {
            return state->cubemapTexture.get();
        }
    }

    return skyboxState ? skyboxState->cubemapTexture.get() : nullptr;
}

Texture* ResourceResolveSystem::findSceneEnvironmentIrradianceTexture(Scene* scene) const
{
    const auto* state = findFirstSceneEnvironmentLightingState(scene);
    return state ? state->irradianceTexture.get() : nullptr;
}

stdptr<Texture> ResourceResolveSystem::findSceneSkyboxTextureShared(Scene* scene) const
{
    const auto* state = findFirstSceneSkyboxState(scene);
    return state ? state->cubemapTexture : nullptr;
}

stdptr<Texture> ResourceResolveSystem::findSceneEnvironmentIrradianceTextureShared(Scene* scene) const
{
    const auto* state = findFirstSceneEnvironmentLightingState(scene);
    return state ? state->irradianceTexture : nullptr;
}

// ── Read-only preview queries (Phase 4: Editor decoupling) ───────────

SkyboxPreviewInfo ResourceResolveSystem::getSkyboxPreview(entt::entity entity) const
{
    SkyboxPreviewInfo info{};

    const auto* state = findSkyboxState(entity);
    if (!state) {
        return info;
    }

    info.sourcePreviewTexture  = state->sourcePreviewTexture.get();
    info.cubemapTexture        = state->cubemapTexture.get();
    info.bHasRenderableCubemap = state->hasRenderableCubemap();

    for (uint32_t i = 0; i < CubeFace_Count; ++i) {
        info.cubemapFaceViews[i] = state->cubemapFacePreviewViews[i].get();
    }

    return info;
}

EnvironmentLightingPreviewInfo ResourceResolveSystem::getEnvironmentLightingPreview(entt::entity entity) const
{
    EnvironmentLightingPreviewInfo info{};

    const auto* state = findEnvironmentLightingState(entity);
    if (!state) {
        return info;
    }

    info.cubemapTexture        = state->cubemapTexture.get();
    info.irradianceTexture     = state->irradianceTexture.get();
    info.bHasRenderableCubemap = state->hasRenderableCubemap();
    info.bHasIrradianceMap     = state->hasIrradianceMap();

    return info;
}

} // namespace ya

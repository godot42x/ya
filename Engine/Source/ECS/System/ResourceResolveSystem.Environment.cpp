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


std::shared_ptr<OffscreenJobState> createEnvironmentCubemapJob(ResourceResolveSystem&              system,
                                                               entt::entity                        entity,
                                                               const EnvironmentLightingComponent& component,
                                                               const stdptr<Texture>&              sourceTexture);
std::shared_ptr<OffscreenJobState> createEnvironmentIrradianceJob(ResourceResolveSystem&              system,
                                                                  entt::entity                        entity,
                                                                  const EnvironmentLightingComponent& component,
                                                                  const stdptr<Texture>&              sourceCubemap);
std::shared_ptr<OffscreenJobState> createEnvironmentPrefilterJob(ResourceResolveSystem&              system,
                                                                 entt::entity                        entity,
                                                                 const EnvironmentLightingComponent& component,
                                                                 const stdptr<Texture>&              sourceCubemap);



uint32_t computeEnvironmentPrefilterFaceSize(const Texture* sourceCubemap)
{
    if (!sourceCubemap) {
        return 0;
    }

    return std::max(1u, std::min(sourceCubemap->getWidth(), sourceCubemap->getHeight()));
}

uint32_t computeEnvironmentPrefilterMipLevels(uint32_t faceSize)
{
    uint32_t mipLevels = 1;
    while (faceSize > 1) {
        faceSize = std::max(1u, faceSize / 2u);
        ++mipLevels;
    }
    return mipLevels;
}

EFormat::T chooseEnvironmentPrefilterFormat(EFormat::T sourceFormat)
{
    (void)sourceFormat;
    return EFormat::R16G16B16A16_SFLOAT;
}

void completeEnvironmentSource(EnvironmentLightingComponent&    component,
                               EnvironmentLightingRuntimeState& state,
                               const char*                      reason)
{
    detail::rebuildEnvironmentCubemapViews(state);
    makeTransition(component.sourceState, "EnvironmentLighting.Source")
        .to(EEnvironmentLightingSourceResolveState::Ready, reason);
    ++state.resultVersion;

    if (!component.bEnableIrradiance) {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
    }
    if (!component.bEnablePrefilter) {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
    }
}

void syncEnvironmentDerivedBranchEnablement(EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    if (!component.bEnableIrradiance) {
        cancelOffscreenJob(state.pendingIrradianceOffscreen);
        detail::retireTextureNow(state.irradianceTexture);
        detail::rebuildEnvironmentIrradianceViews(state);
        if (component.irradianceState != EEnvironmentLightingIrradianceResolveState::Disabled) {
            makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
                .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
        }
    }
    else if (component.hasReadySource() && component.irradianceState == EEnvironmentLightingIrradianceResolveState::Disabled) {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Dirty, "irradiance enabled");
    }

    if (!component.bEnablePrefilter) {
        cancelOffscreenJob(state.pendingPrefilterOffscreen);
        detail::retireTextureNow(state.prefilterTexture);
        detail::rebuildPrefilterViews(state);
        if (component.prefilterState != EEnvironmentLightingPrefilterResolveState::Disabled) {
            makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
                .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
        }
    }
    else if (component.hasReadySource() && component.prefilterState == EEnvironmentLightingPrefilterResolveState::Disabled) {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Dirty, "prefilter enabled");
    }
}

void failEnvironmentDerivedBranches(EnvironmentLightingComponent& component, const char* reason)
{
    if (component.bEnableIrradiance) {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance").fail(reason);
    }
    else {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, reason);
    }

    if (component.bEnablePrefilter) {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter").fail(reason);
    }
    else {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, reason);
    }
}

std::shared_ptr<OffscreenJobState> createEnvironmentCubemapJob(ResourceResolveSystem&              system,
                                                               entt::entity                        entity,
                                                               const EnvironmentLightingComponent& component,
                                                               const stdptr<Texture>&              sourceTexture)
{
    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentCubemap_{}", static_cast<uint32_t>(entity));

    // TODO(user): this is the single cubemap-preprocess job hook. Replace or extend executeFn here.
    job->executeFn = [&pipeline = system.getCylindrical2CubePipeline(),
                      src       = sourceTexture,
                      flipV     = component.cylindricalSource.flipVertical](ICommandBuffer* cmdBuf, Texture* output) -> bool
    {
        auto result = pipeline.execute({
            .cmdBuf        = cmdBuf,
            .input         = src.get(),
            .output        = output,
            .bFlipVertical = flipV,
        });
        if (result.transientOutputArrayView) {
            DeferredDeletionQueue::get().retireResource(result.transientOutputArrayView);
        }
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(job->debugName,
                                                      detail::computeSkyboxFaceSize(sourceTexture.get()),
                                                      detail::chooseSkyboxCubemapFormat(sourceTexture->getFormat()));
    return job;
}

std::shared_ptr<OffscreenJobState> createEnvironmentIrradianceJob(ResourceResolveSystem&              system,
                                                                  entt::entity                        entity,
                                                                  const EnvironmentLightingComponent& component,
                                                                  const stdptr<Texture>&              sourceCubemap)
{
    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentIrradiance_{}", static_cast<uint32_t>(entity));

    // TODO(user): this is the single irradiance job hook. Replace or extend executeFn here.
    job->executeFn = [src = sourceCubemap, &system](ICommandBuffer* cmdBuf, Texture* output) -> bool
    {
        auto result =
            system
                .getCube2IrradiancePipeline()
                .execute({
                    .cmdBuf = cmdBuf,
                    .input  = src.get(),
                    .output = output,
                });
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(
        job->debugName,
        detail::computeEnvironmentIrradianceFaceSize(sourceCubemap.get(), component.getResolvedIrradianceFaceSize()),
        detail::chooseEnvironmentIrradianceFormat(sourceCubemap->getFormat()));
    return job;
}

std::shared_ptr<OffscreenJobState> createEnvironmentPrefilterJob(ResourceResolveSystem&              system,
                                                                 entt::entity                        entity,
                                                                 const EnvironmentLightingComponent& component,
                                                                 const stdptr<Texture>&              sourceCubemap)
{
    (void)component;
    if (!sourceCubemap) {
        return nullptr;
    }

    const uint32_t faceSize  = computeEnvironmentPrefilterFaceSize(sourceCubemap.get());
    const uint32_t mipLevels = computeEnvironmentPrefilterMipLevels(faceSize);

    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentPrefilter_{}", static_cast<uint32_t>(entity));

    // TODO(user): this is the single prefilter job hook. Replace or extend executeFn here.
    job->executeFn = [&pipeline = system.getCube2PrefilterPipeline(),
                      src       = sourceCubemap](ICommandBuffer* cmdBuf, Texture* output) -> bool
    {
        auto result = pipeline.execute({
            .cmdBuf = cmdBuf,
            .input  = src.get(),
            .output = output,
        });
        if (result.transientOutputArrayView) {
            DeferredDeletionQueue::get().retireResource(result.transientOutputArrayView);
        }
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(
        job->debugName,
        faceSize,
        chooseEnvironmentPrefilterFormat(sourceCubemap->getFormat()),
        static_cast<int>(mipLevels));
    return job;
}


void tryBeginEnvIrradianceJob(ResourceResolveSystem&           system,
                              entt::entity                     entity,
                              EnvironmentLightingComponent&    component,
                              EnvironmentLightingRuntimeState& state,
                              stdptr<Texture>                  sourceCubemap)
{
    if (!sourceCubemap || !sourceCubemap->getImageView()) {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .fail("irradiance source invalid");
        return;
    }

    if (!component.bEnableIrradiance) {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
        return;
    }

    detail::retireTextureNow(state.irradianceTexture);

    auto job = createEnvironmentIrradianceJob(system, entity, component, sourceCubemap);
    if (!job) {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .fail("irradiance job not wired");
        return;
    }

    state.pendingIrradianceOffscreen = std::move(job);
    makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
        .to(EEnvironmentLightingIrradianceResolveState::Building, "queue irradiance preprocess");
}

void tryBeginEnvPrefilterJob(ResourceResolveSystem&           system,
                             entt::entity                     entity,
                             EnvironmentLightingComponent&    component,
                             EnvironmentLightingRuntimeState& state,
                             const stdptr<Texture>&           sourceCubemap)
{
    if (!sourceCubemap || !sourceCubemap->getImageView()) {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter").fail("prefilter source invalid");
        return;
    }

    if (!component.bEnablePrefilter) {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
        return;
    }

    detail::retireTextureNow(state.prefilterTexture);

    auto job = createEnvironmentPrefilterJob(system, entity, component, sourceCubemap);
    if (!job) {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter").fail("prefilter job not wired");
        return;
    }

    state.pendingPrefilterOffscreen = std::move(job);
    makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
        .to(EEnvironmentLightingPrefilterResolveState::Building, "queue prefilter preprocess");
}

stdptr<Texture> syncEnvSkybox(EnvironmentLightingComponent&    component,
                              EnvironmentLightingRuntimeState& state,
                              const SkyboxRuntimeState*        sceneSkyboxState)
{
    if (!component.usesSceneSkybox()) {
        return nullptr;
    }

    const uint64_t currentResultVersion = sceneSkyboxState ? sceneSkyboxState->resultVersion : 0;
    if (state.lastSceneSkyboxResultVersion != currentResultVersion) {
        detail::resetEnvState(state);
        state.lastSceneSkyboxResultVersion = currentResultVersion;
        const auto nextSourceState         = currentResultVersion == 0 ? EEnvironmentLightingSourceResolveState::Empty
                                                                       : EEnvironmentLightingSourceResolveState::Dirty;
        makeTransition(component.sourceState, "EnvironmentLighting.Source")
            .to(nextSourceState, "scene skybox dependency changed");
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .to(component.bEnableIrradiance
                    ? (currentResultVersion == 0 ? EEnvironmentLightingIrradianceResolveState::Empty
                                                 : EEnvironmentLightingIrradianceResolveState::Dirty)
                    : EEnvironmentLightingIrradianceResolveState::Disabled,
                "scene skybox dependency changed");
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
            .to(component.bEnablePrefilter
                    ? (currentResultVersion == 0 ? EEnvironmentLightingPrefilterResolveState::Empty
                                                 : EEnvironmentLightingPrefilterResolveState::Dirty)
                    : EEnvironmentLightingPrefilterResolveState::Disabled,
                "scene skybox dependency changed");
    }

    if (sceneSkyboxState && sceneSkyboxState->hasRenderableCubemap() &&
        component.sourceState == EEnvironmentLightingSourceResolveState::Dirty) {
        return sceneSkyboxState->cubemapTexture;
    }

    return nullptr;
}

} // namespace

namespace detail
{

namespace
{

template <typename TViews>
void clearCubeFaceViews(TViews& views)
{
    for (auto& faceView : views) {
        if (!faceView) {
            continue;
        }

        DeferredDeletionQueue::get().retireResource(std::move(faceView));
    }
}

void clearPrefilterViews(EnvironmentLightingRuntimeState& state)
{
    for (auto& mipViews : state.prefilterMipFacePreviewViews) {
        clearCubeFaceViews(mipViews);
    }
    state.prefilterPreviewMipCount = 0;
}

void rebuildCubeFaceViews(const stdptr<Texture>& texture,
                          std::array<stdptr<IImageView>, CubeFace_Count>& outViews,
                          const std::string& labelPrefix)
{
    clearCubeFaceViews(outViews);
    if (!texture || !texture->getImageShared() || !texture->getImageView()) {
        return;
    }

    auto* textureFactory = ITextureFactory::get();
    if (!textureFactory) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        outViews[faceIndex] = textureFactory->createImageView(
            texture->getImageShared(),
            ImageViewCreateInfo{
                .label          = std::format("{}_Face_{}", labelPrefix, faceIndex),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = faceIndex,
                .layerCount     = 1,
            });
    }
}

} // namespace

void rebuildEnvironmentCubemapViews(EnvironmentLightingRuntimeState& state)
{
    rebuildCubeFaceViews(state.cubemapTexture, state.cubemapFacePreviewViews, "EnvironmentCubemap");
}

void rebuildEnvironmentIrradianceViews(EnvironmentLightingRuntimeState& state)
{
    rebuildCubeFaceViews(state.irradianceTexture, state.irradianceFacePreviewViews, "EnvironmentIrradiance");
}

void rebuildPrefilterViews(EnvironmentLightingRuntimeState& state)
{
    clearPrefilterViews(state);
    if (!state.prefilterTexture || !state.prefilterTexture->getImageShared() ||
        !state.prefilterTexture->getImageView()) {
        return;
    }

    auto* textureFactory = ITextureFactory::get();
    if (!textureFactory) {
        return;
    }

    const auto prefilterImage = state.prefilterTexture->getImageShared();
    const uint32_t mipLevels  = std::min(prefilterImage->getMipLevels(), EnvironmentLightingRuntimeState::MAX_PREFILTER_PREVIEW_MIPS);
    state.prefilterPreviewMipCount = mipLevels;

    for (uint32_t mipIndex = 0; mipIndex < mipLevels; ++mipIndex) {
        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
            state.prefilterMipFacePreviewViews[mipIndex][faceIndex] = textureFactory->createImageView(
                prefilterImage,
                ImageViewCreateInfo{
                    .label          = std::format("EnvironmentPrefilter_Mip_{}_Face_{}", mipIndex, faceIndex),
                    .viewType       = EImageViewType::View2D,
                    .aspectFlags    = EImageAspect::Color,
                    .baseMipLevel   = mipIndex,
                    .levelCount     = 1,
                    .baseArrayLayer = faceIndex,
                    .layerCount     = 1,
                });
        }
    }
}

void retireEnvTextures(EnvironmentLightingRuntimeState& state)
{
    clearCubeFaceViews(state.cubemapFacePreviewViews);
    clearCubeFaceViews(state.irradianceFacePreviewViews);
    clearPrefilterViews(state);
    retireTexture(state.cubemapTexture);
    retireTexture(state.irradianceTexture);
    retireTexture(state.prefilterTexture);
}

void resetEnvPending(EnvironmentLightingRuntimeState& state)
{
    state.pendingBatchLoad.reset();
    state.pendingCylindricalFuture.reset();
    state.lastSceneSkyboxResultVersion = 0;
    cancelOffscreenJob(state.pendingEnvironmentOffscreen);
    cancelOffscreenJob(state.pendingIrradianceOffscreen);
    cancelOffscreenJob(state.pendingPrefilterOffscreen);
}

void resetEnvState(EnvironmentLightingRuntimeState& state)
{
    resetEnvPending(state);
    retireEnvTextures(state);
    state.resultVersion = 0;
}

} // namespace detail

namespace
{

void handleEnvironmentNoSource(EnvironmentLightingComponent&    component,
                               EnvironmentLightingRuntimeState& state)
{
    if (component.sourceState != EEnvironmentLightingSourceResolveState::Empty) {
        makeTransition(component.sourceState, "EnvironmentLighting.Source")
            .to(EEnvironmentLightingSourceResolveState::Empty, "no source");
    }
    if (component.irradianceState != EEnvironmentLightingIrradianceResolveState::Empty &&
        component.irradianceState != EEnvironmentLightingIrradianceResolveState::Disabled) {
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
            .to(component.bEnableIrradiance ? EEnvironmentLightingIrradianceResolveState::Empty
                                            : EEnvironmentLightingIrradianceResolveState::Disabled,
                "no source");
    }
    if (component.prefilterState != EEnvironmentLightingPrefilterResolveState::Empty &&
        component.prefilterState != EEnvironmentLightingPrefilterResolveState::Disabled) {
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
            .to(component.bEnablePrefilter ? EEnvironmentLightingPrefilterResolveState::Empty
                                           : EEnvironmentLightingPrefilterResolveState::Disabled,
                "no source");
    }
    detail::resetEnvState(state);
}

void handleEnvironmentSourceDirty(EnvironmentLightingComponent&    component,
                                  EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(component.sourceState, "EnvironmentLighting.Source");
    if (component.hasCubemapSource()) {
        std::vector<std::string> facePaths(component.cubemapSource.files.begin(), component.cubemapSource.files.end());
        state.pendingBatchLoad              = std::make_shared<EnvironmentLightingPendingBatchLoadState>();
        state.pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
            AssetManager::TextureBatchMemoryLoadRequest{
                .filepaths  = facePaths,
                .colorSpace = AssetManager::ETextureColorSpace::Linear,
            });
        transition.to(EEnvironmentLightingSourceResolveState::ResolvingSource, "source load requested");
        return;
    }

    if (component.hasCylindricalSource()) {
        state.pendingCylindricalFuture = AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
            .filepath        = component.cylindricalSource.filepath,
            .name            = "EnvironmentLightingCylindricalSource",
            .onReady         = {},
            .colorSpace      = AssetManager::ETextureColorSpace::Linear,
            .textureSemantic = std::nullopt,
        });
        transition.to(EEnvironmentLightingSourceResolveState::ResolvingSource, "source load requested");
        return;
    }

    transition.to(EEnvironmentLightingSourceResolveState::Empty, "source removed while dirty");
}

void handleEnvironmentSourceResolving(ResourceResolveSystem&           system,
                                      entt::entity                     entity,
                                      EnvironmentLightingComponent&    component,
                                      EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(component.sourceState, "EnvironmentLighting.Source");
    if (component.hasCubemapSource()) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!state.pendingBatchLoad || !AssetManager::get()->consumeTextureBatchMemory(state.pendingBatchLoad->batchHandle, batchMemory)) {
            return;
        }

        state.pendingBatchLoad.reset();
        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap batch invalid");
            failEnvironmentDerivedBranches(component, "cubemap batch invalid");
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
            detail::retireEnvTextures(state);
            transition.fail("cubemap creation failed");
            failEnvironmentDerivedBranches(component, "cubemap creation failed");
            return;
        }

        state.cubemapTexture = std::move(cubemap);
        completeEnvironmentSource(component, state, "cubemap source resolved");
        return;
    }

    if (component.hasCylindricalSource()) {
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            state.pendingCylindricalFuture = AssetManager::get()->loadTexture(AssetManager::TextureLoadRequest{
                .filepath        = component.cylindricalSource.filepath,
                .name            = "EnvironmentLightingCylindricalSource",
                .onReady         = {},
                .colorSpace      = AssetManager::ETextureColorSpace::Linear,
                .textureSemantic = std::nullopt,
            });
        }
        if (!state.pendingCylindricalFuture.has_value() || !state.pendingCylindricalFuture->isReady()) {
            return;
        }

        const auto sourceTexture = state.pendingCylindricalFuture->getShared();
        state.pendingCylindricalFuture.reset();
        if (!sourceTexture || !sourceTexture->getImageView()) {
            detail::retireEnvTextures(state);
            transition.fail("cylindrical source invalid");
            failEnvironmentDerivedBranches(component, "cylindrical source invalid");
            return;
        }

        auto job = createEnvironmentCubemapJob(system, entity, component, sourceTexture);
        if (!job) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap job not wired");
            failEnvironmentDerivedBranches(component, "cubemap job not wired");
            return;
        }

        state.pendingEnvironmentOffscreen = std::move(job);
        transition.to(EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap, "queue environment preprocess");
        return;
    }

    detail::resetEnvPending(state);
    transition.to(EEnvironmentLightingSourceResolveState::Empty, "active source changed while resolving");
    makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
        .to(component.bEnableIrradiance ? EEnvironmentLightingIrradianceResolveState::Empty
                                        : EEnvironmentLightingIrradianceResolveState::Disabled,
            "active source changed while resolving");
    makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
        .to(component.bEnablePrefilter ? EEnvironmentLightingPrefilterResolveState::Empty
                                       : EEnvironmentLightingPrefilterResolveState::Disabled,
            "active source changed while resolving");
}

void handleEnvironmentSourceBuildingCubemap(EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(component.sourceState, "EnvironmentLighting.Source");
    if (!state.pendingEnvironmentOffscreen || !state.pendingEnvironmentOffscreen->bTaskFinished) {
        detail::tryQueueJob(state.pendingEnvironmentOffscreen);
        return;
    }

    if (!state.pendingEnvironmentOffscreen->bTaskSucceeded || !state.pendingEnvironmentOffscreen->result ||
        !state.pendingEnvironmentOffscreen->result->outputTexture) {
        state.pendingEnvironmentOffscreen.reset();
        detail::retireEnvTextures(state);
        transition.fail("preprocess failed");
        failEnvironmentDerivedBranches(component, "preprocess failed");
        return;
    }

    state.cubemapTexture = std::move(state.pendingEnvironmentOffscreen->result->outputTexture);
    state.pendingEnvironmentOffscreen.reset();
    completeEnvironmentSource(component, state, "environment cubemap preprocess completed");
}

void handleEnvironmentIrradianceDirty(ResourceResolveSystem&           system,
                                      entt::entity                     entity,
                                      EnvironmentLightingComponent&    component,
                                      EnvironmentLightingRuntimeState& state)
{
    if (!component.bEnableIrradiance || !component.hasReadySource() || !state.cubemapTexture) {
        return;
    }

    tryBeginEnvIrradianceJob(system, entity, component, state, state.cubemapTexture);
}

void handleEnvironmentIrradianceBuilding(EnvironmentLightingComponent&    component,
                                         EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingIrradianceOffscreen || !state.pendingIrradianceOffscreen->bTaskFinished) {
        detail::tryQueueJob(state.pendingIrradianceOffscreen);
        return;
    }

    if (!state.pendingIrradianceOffscreen->bTaskSucceeded ||
        !state.pendingIrradianceOffscreen->result ||
        !state.pendingIrradianceOffscreen->result->outputTexture) {
        state.pendingIrradianceOffscreen.reset();
        detail::retireTextureNow(state.irradianceTexture);
        makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance").fail("preprocess failed");
        return;
    }

    // ok
    state.irradianceTexture = std::move(state.pendingIrradianceOffscreen->result->outputTexture);
    detail::rebuildEnvironmentIrradianceViews(state);
    state.pendingIrradianceOffscreen.reset();
    ++state.resultVersion;
    makeTransition(component.irradianceState, "EnvironmentLighting.Irradiance")
        .to(EEnvironmentLightingIrradianceResolveState::Ready, "irradiance preprocess completed");
}

void handleEnvironmentPrefilterDirty(ResourceResolveSystem&           system,
                                     entt::entity                     entity,
                                     EnvironmentLightingComponent&    component,
                                     EnvironmentLightingRuntimeState& state)
{
    if (!component.bEnablePrefilter || !component.hasReadySource() || !state.cubemapTexture) {
        return;
    }

    tryBeginEnvPrefilterJob(system, entity, component, state, state.cubemapTexture);
}

void handleEnvironmentPrefilterBuilding(EnvironmentLightingComponent&    component,
                                        EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingPrefilterOffscreen || !state.pendingPrefilterOffscreen->bTaskFinished) {
        detail::tryQueueJob(state.pendingPrefilterOffscreen);
        return;
    }

    if (!state.pendingPrefilterOffscreen->bTaskSucceeded || !state.pendingPrefilterOffscreen->result ||
        !state.pendingPrefilterOffscreen->result->outputTexture) {
        state.pendingPrefilterOffscreen.reset();
        detail::retireTextureNow(state.prefilterTexture);
        detail::rebuildPrefilterViews(state);
        makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter").fail("preprocess failed");
        return;
    }

    state.prefilterTexture = std::move(state.pendingPrefilterOffscreen->result->outputTexture);
    detail::rebuildPrefilterViews(state);
    state.pendingPrefilterOffscreen.reset();
    ++state.resultVersion;
    makeTransition(component.prefilterState, "EnvironmentLighting.Prefilter")
        .to(EEnvironmentLightingPrefilterResolveState::Ready, "prefilter preprocess completed");
}

void resolveEnvironmentSourceState(ResourceResolveSystem&           system,
                                   entt::entity                     entity,
                                   EnvironmentLightingComponent&    component,
                                   EnvironmentLightingRuntimeState& state)
{
    switch (component.sourceState) {
    case EEnvironmentLightingSourceResolveState::Dirty:
    {
        handleEnvironmentSourceDirty(component, state);
    } break;
    case EEnvironmentLightingSourceResolveState::ResolvingSource:
    {
        handleEnvironmentSourceResolving(system, entity, component, state);
    } break;
    case EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap:
    {
        handleEnvironmentSourceBuildingCubemap(component, state);
    } break;
    case EEnvironmentLightingSourceResolveState::Empty:
    case EEnvironmentLightingSourceResolveState::Ready:
    case EEnvironmentLightingSourceResolveState::Failed:
    default:
    {
    } break;
    }
}

void resolveEnvironmentIrradianceState(ResourceResolveSystem&           system,
                                       entt::entity                     entity,
                                       EnvironmentLightingComponent&    component,
                                       EnvironmentLightingRuntimeState& state)
{
    switch (component.irradianceState) {
    case EEnvironmentLightingIrradianceResolveState::Dirty:
    {
        handleEnvironmentIrradianceDirty(system, entity, component, state);
    } break;
    case EEnvironmentLightingIrradianceResolveState::Building:
    {
        handleEnvironmentIrradianceBuilding(component, state);
    } break;
    case EEnvironmentLightingIrradianceResolveState::Empty:
    case EEnvironmentLightingIrradianceResolveState::Disabled:
    case EEnvironmentLightingIrradianceResolveState::Ready:
    case EEnvironmentLightingIrradianceResolveState::Failed:
    default:
    {
    } break;
    }
}

void resolveEnvironmentPrefilterState(ResourceResolveSystem&           system,
                                      entt::entity                     entity,
                                      EnvironmentLightingComponent&    component,
                                      EnvironmentLightingRuntimeState& state)
{
    switch (component.prefilterState) {
    case EEnvironmentLightingPrefilterResolveState::Dirty:
    {
        handleEnvironmentPrefilterDirty(system, entity, component, state);
    } break;
    case EEnvironmentLightingPrefilterResolveState::Building:
    {
        handleEnvironmentPrefilterBuilding(component, state);
    } break;
    case EEnvironmentLightingPrefilterResolveState::Empty:
    case EEnvironmentLightingPrefilterResolveState::Disabled:
    case EEnvironmentLightingPrefilterResolveState::Ready:
    case EEnvironmentLightingPrefilterResolveState::Failed:
    default:
    {
    } break;
    }
}

} // namespace

void ResourceResolveSystem::resolvePendingEnvironmentLighting(Scene* scene)
{
    YA_PROFILE_FUNCTION();
    auto&       registry         = scene->getRegistry();
    const auto* sceneSkyboxState = findFirstSceneSkyboxState(scene);

    for (auto&& [entity, elc] : registry.view<EnvironmentLightingComponent>().each()) {
        auto& pendingState = _environmentStates[entity];
        if (pendingState.authoringVersion != elc.authoringVersion) {
            detail::resetEnvState(pendingState);
            pendingState.authoringVersion = elc.authoringVersion;
        }

        if (!elc.hasSource()) {
            handleEnvironmentNoSource(elc, pendingState);
            continue;
        }

        if (elc.sourceState == EEnvironmentLightingSourceResolveState::Dirty ||
            elc.sourceState == EEnvironmentLightingSourceResolveState::Empty) {
            detail::resetEnvPending(pendingState);
        }

        syncEnvironmentDerivedBranchEnablement(elc, pendingState);

        if (const auto sourceCubemap = syncEnvSkybox(elc, pendingState, sceneSkyboxState)) {
            pendingState.cubemapTexture = sourceCubemap;
            completeEnvironmentSource(elc, pendingState, "scene skybox source resolved");
        }

        resolveEnvironmentSourceState(*this, entity, elc, pendingState);
        resolveEnvironmentIrradianceState(*this, entity, elc, pendingState);
        resolveEnvironmentPrefilterState(*this, entity, elc, pendingState);
    }

    for (auto it = _environmentStates.begin(); it != _environmentStates.end();) {
        if (!registry.valid(it->first) || !registry.all_of<EnvironmentLightingComponent>(it->first)) {
            detail::resetEnvState(it->second);
            it = _environmentStates.erase(it);
        }
        else {
            ++it;
        }
    }
}

} // namespace ya
#include "ResourceResolveSystem.Detail.h"

#include "Render/Render.h"
#include "Render/Core/RenderResourceFactory.h"
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


std::shared_ptr<OffscreenJobState> createEnvironmentCubemapJob(ResourceResolveSystem&              system,
                                                               entt::entity                        entity,
                                                               const EnvironmentLightingComponent& component,
                                                               const stdptr<Texture>&              sourceTexture);
std::shared_ptr<OffscreenJobState> createEnvironmentIrradianceJob(ResourceResolveSystem&              system,
                                                                  entt::entity                        entity,
                                                                  const EnvironmentLightingComponent& component,
                                                                  const ImageResourceRef&             sourceCubemap);
std::shared_ptr<OffscreenJobState> createEnvironmentPrefilterJob(ResourceResolveSystem&              system,
                                                                 entt::entity                        entity,
                                                                 const EnvironmentLightingComponent& component,
                                                                 const ImageResourceRef&             sourceCubemap);



uint32_t computeEnvironmentPrefilterFaceSize(const Texture* sourceCubemap)
{
    static constexpr uint32_t MAX_PREFILTER_FACE_SIZE = 64;

    if (!sourceCubemap) {
        return 0;
    }

    return std::max(1u,
                    std::min({sourceCubemap->getWidth(),
                              sourceCubemap->getHeight(),
                              MAX_PREFILTER_FACE_SIZE}));
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
    makeTransition(state.sourceState, "EnvironmentLighting.Source")
        .to(EEnvironmentLightingSourceResolveState::Ready, reason);
    ++state.resultVersion;

    if (!component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
    }
    if (!component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
    }
}

void completeEnvironmentSourceFromDependency(EnvironmentLightingComponent&    component,
                                             EnvironmentLightingRuntimeState& state,
                                             const char*                      reason)
{
    makeTransition(state.sourceState, "EnvironmentLighting.Source")
        .to(EEnvironmentLightingSourceResolveState::Ready, reason);
    ++state.resultVersion;

    if (!component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
    }
    if (!component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
    }
}

[[nodiscard]] ImageResourceRef resolveEnvironmentSourceCubemap(const EnvironmentLightingComponent&    component,
                                                               const EnvironmentLightingRuntimeState& state,
                                                               const SkyboxRuntimeState*             sceneSkyboxState)
{
    if (component.usesSceneSkybox()) {
        return ImageResourceRef{
            .renderImage = sceneSkyboxState ? sceneSkyboxState->cubemapRenderImage : nullptr,
            .texture     = sceneSkyboxState ? sceneSkyboxState->cubemapTexture : nullptr,
        };
    }

    return ImageResourceRef{
        .renderImage = state.cubemapRenderImage,
        .texture     = state.cubemapTexture,
    };
}

void syncEnvironmentDerivedBranchEnablement(EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    if (!component.bEnableIrradiance) {
        cancelOffscreenJob(state.pendingIrradianceOffscreen);
        detail::retireRenderImageNow(state.irradianceRenderImage);
        detail::rebuildEnvironmentIrradianceViews(state);
        if (state.irradianceState != EEnvironmentLightingIrradianceResolveState::Disabled) {
            makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
                .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
        }
    }
    else if (state.sourceState == EEnvironmentLightingSourceResolveState::Ready && state.irradianceState == EEnvironmentLightingIrradianceResolveState::Disabled) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Dirty, "irradiance enabled");
    }

    if (!component.bEnablePrefilter) {
        cancelOffscreenJob(state.pendingPrefilterOffscreen);
        detail::retireRenderImageNow(state.prefilterRenderImage);
        detail::rebuildPrefilterViews(state);
        if (state.prefilterState != EEnvironmentLightingPrefilterResolveState::Disabled) {
            makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
                .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
        }
    }
    else if (state.sourceState == EEnvironmentLightingSourceResolveState::Ready && state.prefilterState == EEnvironmentLightingPrefilterResolveState::Disabled) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Dirty, "prefilter enabled");
    }
}

void failEnvironmentDerivedBranches(EnvironmentLightingComponent& component, EnvironmentLightingRuntimeState& state, const char* reason)
{
    if (component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance").fail(reason);
    }
    else {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, reason);
    }

    if (component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail(reason);
    }
    else {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
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
    auto jobResult = job->result;

    // TODO(user): this is the single cubemap-preprocess job hook. Replace or extend executeFn here.
    job->executeFn = [&pipeline = system.getCylindrical2CubePipeline(),
                      src       = sourceTexture,
                      flipV     = component.cylindricalSource.flipVertical,
                      jobResult](ICommandBuffer* cmdBuf, RenderImage* output) -> bool
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
                                                                  const ImageResourceRef&             sourceCubemap)
{
    const auto sourceImageShared = sourceCubemap.getImageShared();
    const uint32_t sourceWidth = sourceImageShared ? sourceImageShared->getWidth() : 0;
    const auto sourceFormat = sourceImageShared ? sourceImageShared->getFormat() : EFormat::Undefined;
    if (sourceWidth == 0) {
        return nullptr;
    }
    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentIrradiance_{}", static_cast<uint32_t>(entity));
    auto jobResult = job->result;

    // TODO(user): this is the single irradiance job hook. Replace or extend executeFn here.
    job->executeFn = [srcImage = sourceCubemap.renderImage, srcTexture = sourceCubemap.texture, &system, jobResult](ICommandBuffer* cmdBuf, RenderImage* output) -> bool
    {
        auto result =
            system
                .getCube2IrradiancePipeline()
                .execute({
                    .cmdBuf       = cmdBuf,
                    .inputImage   = srcImage.get(),
                    .inputTexture = srcTexture.get(),
                    .output       = output,
                });
        if (jobResult && !result.keepAliveResources.empty()) {
            auto& retained = jobResult->retainedResources;
            retained.insert(retained.end(),
                            std::make_move_iterator(result.keepAliveResources.begin()),
                            std::make_move_iterator(result.keepAliveResources.end()));
        }
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(
        job->debugName,
        std::max(4u, std::min(sourceWidth, std::max(4u, component.getResolvedIrradianceFaceSize()))),
        detail::chooseEnvironmentIrradianceFormat(sourceFormat));
    return job;
}

std::shared_ptr<OffscreenJobState> createEnvironmentPrefilterJob(ResourceResolveSystem&              system,
                                                                 entt::entity                        entity,
                                                                 const EnvironmentLightingComponent& component,
                                                                 const ImageResourceRef&             sourceCubemap)
{
    (void)component;
    const auto     sourceImageShared = sourceCubemap.getImageShared();
    const uint32_t sourceWidth  = sourceImageShared ? sourceImageShared->getWidth() : 0;
    const uint32_t sourceHeight = sourceImageShared ? sourceImageShared->getHeight() : 0;
    const auto     sourceFormat = sourceImageShared ? sourceImageShared->getFormat() : EFormat::Undefined;
    if (sourceWidth == 0 || sourceHeight == 0) {
        return nullptr;
    }

    const uint32_t faceSize  = std::max(1u, std::min({sourceWidth, sourceHeight, 64u}));
    const uint32_t mipLevels = computeEnvironmentPrefilterMipLevels(faceSize);

    auto job       = std::make_shared<OffscreenJobState>();
    job->debugName = std::format("EnvironmentPrefilter_{}", static_cast<uint32_t>(entity));
    auto jobResult = job->result;

    // TODO(user): this is the single prefilter job hook. Replace or extend executeFn here.
    job->executeFn = [&pipeline = system.getCube2PrefilterPipeline(),
                      srcImage  = sourceCubemap.renderImage,
                      srcTexture = sourceCubemap.texture,
                      jobResult](ICommandBuffer* cmdBuf, RenderImage* output) -> bool
    {
        auto result = pipeline.execute({
            .cmdBuf       = cmdBuf,
            .inputImage   = srcImage.get(),
            .inputTexture = srcTexture.get(),
            .output       = output,
        });
        if (jobResult && !result.keepAliveResources.empty()) {
            auto& retained = jobResult->retainedResources;
            retained.insert(retained.end(),
                            std::make_move_iterator(result.keepAliveResources.begin()),
                            std::make_move_iterator(result.keepAliveResources.end()));
        }
        if (result.transientOutputArrayView) {
            DeferredDeletionQueue::get().retireResource(result.transientOutputArrayView);
        }
        return result.bSuccess;
    };
    job->createOutputFn = detail::makeCubemapOutputFn(
        job->debugName,
        faceSize,
        chooseEnvironmentPrefilterFormat(sourceFormat),
        static_cast<int>(mipLevels));
    return job;
}


void tryBeginEnvIrradianceJob(ResourceResolveSystem&           system,
                              entt::entity                     entity,
                              EnvironmentLightingComponent&    component,
                              EnvironmentLightingRuntimeState& state,
                              const ImageResourceRef&          sourceCubemap)
{
    if (!sourceCubemap.isValid()) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .fail("irradiance source invalid");
        return;
    }

    if (!component.bEnableIrradiance) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(EEnvironmentLightingIrradianceResolveState::Disabled, "irradiance disabled");
        return;
    }

    detail::retireRenderImageNow(state.irradianceRenderImage);

    auto job = createEnvironmentIrradianceJob(system, entity, component, sourceCubemap);
    if (!job) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .fail("irradiance job not wired");
        return;
    }

    state.pendingIrradianceOffscreen = std::move(job);
    makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
        .to(EEnvironmentLightingIrradianceResolveState::Building, "queue irradiance preprocess");
}

void tryBeginEnvPrefilterJob(ResourceResolveSystem&           system,
                             entt::entity                     entity,
                             EnvironmentLightingComponent&    component,
                             EnvironmentLightingRuntimeState& state,
                             const ImageResourceRef&          sourceCubemap)
{
    if (!sourceCubemap.isValid()) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("prefilter source invalid");
        return;
    }

    if (!component.bEnablePrefilter) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(EEnvironmentLightingPrefilterResolveState::Disabled, "prefilter disabled");
        return;
    }

    detail::retireRenderImageNow(state.prefilterRenderImage);

    auto job = createEnvironmentPrefilterJob(system, entity, component, sourceCubemap);
    if (!job) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("prefilter job not wired");
        return;
    }

    state.pendingPrefilterOffscreen = std::move(job);
    makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
        .to(EEnvironmentLightingPrefilterResolveState::Building, "queue prefilter preprocess");
}

const SkyboxRuntimeState* syncEnvSkybox(EnvironmentLightingComponent&    component,
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
        makeTransition(state.sourceState, "EnvironmentLighting.Source")
            .to(nextSourceState, "scene skybox dependency changed");
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(component.bEnableIrradiance
                    ? (currentResultVersion == 0 ? EEnvironmentLightingIrradianceResolveState::Empty
                                                 : EEnvironmentLightingIrradianceResolveState::Dirty)
                    : EEnvironmentLightingIrradianceResolveState::Disabled,
                "scene skybox dependency changed");
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(component.bEnablePrefilter
                    ? (currentResultVersion == 0 ? EEnvironmentLightingPrefilterResolveState::Empty
                                                 : EEnvironmentLightingPrefilterResolveState::Dirty)
                    : EEnvironmentLightingPrefilterResolveState::Disabled,
                "scene skybox dependency changed");
    }

    if (sceneSkyboxState && sceneSkyboxState->hasRenderableCubemap() &&
        state.sourceState == EEnvironmentLightingSourceResolveState::Dirty) {
        return sceneSkyboxState;
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

void rebuildCubeFaceViews(const stdptr<Texture>&                          texture,
                          const std::shared_ptr<RenderImage>&             renderImage,
                          std::array<stdptr<IImageView>, CubeFace_Count>& outViews,
                          const std::string&                              labelPrefix)
{
    clearCubeFaceViews(outViews);
    const auto image = getImageShared(renderImage, texture);
    if (!image || !getImageView(renderImage, texture)) {
        return;
    }

    auto* const app             = App::get();
    auto* const render          = app ? app->getRenderServices().getRender() : nullptr;
    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        outViews[faceIndex] = resourceFactory->createImageView(
            image,
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
    rebuildCubeFaceViews(state.cubemapTexture, state.cubemapRenderImage, state.cubemapFacePreviewViews, "EnvironmentCubemap");
}

void rebuildEnvironmentIrradianceViews(EnvironmentLightingRuntimeState& state)
{
    rebuildCubeFaceViews(nullptr, state.irradianceRenderImage, state.irradianceFacePreviewViews, "EnvironmentIrradiance");
}

void rebuildPrefilterViews(EnvironmentLightingRuntimeState& state)
{
    clearPrefilterViews(state);
    const auto prefilterImage = state.prefilterRenderImage ? state.prefilterRenderImage->getImageShared() : nullptr;
    if (!prefilterImage || !state.prefilterRenderImage || !state.prefilterRenderImage->getImageView()) {
        return;
    }

    auto* const app             = App::get();
    auto* const render          = app ? app->getRenderServices().getRender() : nullptr;
    auto* const resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory) {
        return;
    }

    const uint32_t mipLevels       = std::min(prefilterImage->getMipLevels(), EnvironmentLightingRuntimeState::MAX_PREFILTER_PREVIEW_MIPS);
    state.prefilterPreviewMipCount = mipLevels;

    for (uint32_t mipIndex = 0; mipIndex < mipLevels; ++mipIndex) {
        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
            state.prefilterMipFacePreviewViews[mipIndex][faceIndex] = resourceFactory->createImageView(
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
    retireRenderImage(state.cubemapRenderImage);
    retireRenderImage(state.irradianceRenderImage);
    retireRenderImage(state.prefilterRenderImage);
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
    if (state.sourceState != EEnvironmentLightingSourceResolveState::Empty) {
        makeTransition(state.sourceState, "EnvironmentLighting.Source")
            .to(EEnvironmentLightingSourceResolveState::Empty, "no source");
    }
    if (state.irradianceState != EEnvironmentLightingIrradianceResolveState::Empty &&
        state.irradianceState != EEnvironmentLightingIrradianceResolveState::Disabled) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(component.bEnableIrradiance ? EEnvironmentLightingIrradianceResolveState::Empty
                                            : EEnvironmentLightingIrradianceResolveState::Disabled,
                "no source");
    }
    if (state.prefilterState != EEnvironmentLightingPrefilterResolveState::Empty &&
        state.prefilterState != EEnvironmentLightingPrefilterResolveState::Disabled) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(component.bEnablePrefilter ? EEnvironmentLightingPrefilterResolveState::Empty
                                           : EEnvironmentLightingPrefilterResolveState::Disabled,
                "no source");
    }
    detail::resetEnvState(state);
}

void handleEnvironmentSourceDirty(EnvironmentLightingComponent&    component,
                                  EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(state.sourceState, "EnvironmentLighting.Source");
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
    auto transition = makeTransition(state.sourceState, "EnvironmentLighting.Source");
    if (component.hasCubemapSource()) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!state.pendingBatchLoad || !AssetManager::get()->consumeTextureBatchMemory(state.pendingBatchLoad->batchHandle, batchMemory)) {
            return;
        }

        state.pendingBatchLoad.reset();
        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap batch invalid");
            failEnvironmentDerivedBranches(component, state, "cubemap batch invalid");
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
            failEnvironmentDerivedBranches(component, state, "cubemap creation failed");
            return;
        }

        state.cubemapTexture = std::move(cubemap);
        state.cubemapRenderImage.reset();
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
            failEnvironmentDerivedBranches(component, state, "cylindrical source invalid");
            return;
        }

        auto job = createEnvironmentCubemapJob(system, entity, component, sourceTexture);
        if (!job) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap job not wired");
            failEnvironmentDerivedBranches(component, state, "cubemap job not wired");
            return;
        }

        state.pendingEnvironmentOffscreen = std::move(job);
        transition.to(EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap, "queue environment preprocess");
        return;
    }

    detail::resetEnvPending(state);
    transition.to(EEnvironmentLightingSourceResolveState::Empty, "active source changed while resolving");
    makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
        .to(component.bEnableIrradiance ? EEnvironmentLightingIrradianceResolveState::Empty
                                        : EEnvironmentLightingIrradianceResolveState::Disabled,
            "active source changed while resolving");
    makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
        .to(component.bEnablePrefilter ? EEnvironmentLightingPrefilterResolveState::Empty
                                       : EEnvironmentLightingPrefilterResolveState::Disabled,
            "active source changed while resolving");
}

void handleEnvironmentSourceBuildingCubemap(EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(state.sourceState, "EnvironmentLighting.Source");
    if (!state.pendingEnvironmentOffscreen) {
        transition.fail("preprocess job missing");
        failEnvironmentDerivedBranches(component, state, "preprocess job missing");
        return;
    }

    if (state.pendingEnvironmentOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(state.pendingEnvironmentOffscreen);
        return;
    }

    if (state.pendingEnvironmentOffscreen->phase == EOffscreenJobPhase::Queued ||
        state.pendingEnvironmentOffscreen->phase == EOffscreenJobPhase::Recorded) {
        return;
    }

    if (state.pendingEnvironmentOffscreen->hasFailed() || !state.pendingEnvironmentOffscreen->result ||
        !state.pendingEnvironmentOffscreen->result->outputImage) {
        state.pendingEnvironmentOffscreen.reset();
        detail::retireEnvTextures(state);
        transition.fail("preprocess failed");
        failEnvironmentDerivedBranches(component, state, "preprocess failed");
        return;
    }

    if (!state.pendingEnvironmentOffscreen->isGpuCompleted()) {
        return;
    }

    state.cubemapRenderImage = state.pendingEnvironmentOffscreen->result->outputImage;
    detail::retireTextureNow(state.cubemapTexture);
    state.pendingEnvironmentOffscreen.reset();
    completeEnvironmentSource(component, state, "environment cubemap preprocess completed");
}

void handleEnvironmentIrradianceDirty(ResourceResolveSystem&           system,
                                      entt::entity                     entity,
                                      EnvironmentLightingComponent&    component,
                                      EnvironmentLightingRuntimeState& state,
                                      const SkyboxRuntimeState*        sceneSkyboxState)
{
    const auto sourceCubemap = resolveEnvironmentSourceCubemap(component, state, sceneSkyboxState);
    if (!component.bEnableIrradiance || state.sourceState != EEnvironmentLightingSourceResolveState::Ready || !sourceCubemap.isValid()) {
        return;
    }

    tryBeginEnvIrradianceJob(system,
                             entity,
                             component,
                             state,
                             sourceCubemap);
}

void handleEnvironmentIrradianceBuilding(EnvironmentLightingComponent&    component,
                                         EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingIrradianceOffscreen) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance").fail("preprocess job missing");
        return;
    }

    if (state.pendingIrradianceOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(state.pendingIrradianceOffscreen);
        return;
    }

    if (state.pendingIrradianceOffscreen->phase == EOffscreenJobPhase::Queued ||
        state.pendingIrradianceOffscreen->phase == EOffscreenJobPhase::Recorded) {
        return;
    }

    if (state.pendingIrradianceOffscreen->hasFailed() ||
        !state.pendingIrradianceOffscreen->result ||
        !state.pendingIrradianceOffscreen->result->outputImage) {
        state.pendingIrradianceOffscreen.reset();
        detail::retireRenderImageNow(state.irradianceRenderImage);
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance").fail("preprocess failed");
        return;
    }

    if (!state.pendingIrradianceOffscreen->isGpuCompleted()) {
        return;
    }

    // ok
    state.irradianceRenderImage = state.pendingIrradianceOffscreen->result->outputImage;
    detail::rebuildEnvironmentIrradianceViews(state);
    state.pendingIrradianceOffscreen.reset();
    ++state.resultVersion;
    makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
        .to(EEnvironmentLightingIrradianceResolveState::Ready, "irradiance preprocess completed");
}

void handleEnvironmentPrefilterDirty(ResourceResolveSystem&           system,
                                     entt::entity                     entity,
                                     EnvironmentLightingComponent&    component,
                                     EnvironmentLightingRuntimeState& state,
                                     const SkyboxRuntimeState*        sceneSkyboxState)
{
    const auto sourceCubemap = resolveEnvironmentSourceCubemap(component, state, sceneSkyboxState);
    if (!component.bEnablePrefilter || state.sourceState != EEnvironmentLightingSourceResolveState::Ready || !sourceCubemap.isValid()) {
        return;
    }

    tryBeginEnvPrefilterJob(system,
                            entity,
                            component,
                            state,
                            sourceCubemap);
}

void handleEnvironmentPrefilterBuilding(EnvironmentLightingComponent&    component,
                                        EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingPrefilterOffscreen) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("preprocess job missing");
        return;
    }

    if (state.pendingPrefilterOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(state.pendingPrefilterOffscreen);
        return;
    }

    if (state.pendingPrefilterOffscreen->phase == EOffscreenJobPhase::Queued ||
        state.pendingPrefilterOffscreen->phase == EOffscreenJobPhase::Recorded) {
        return;
    }

    if (state.pendingPrefilterOffscreen->hasFailed() || !state.pendingPrefilterOffscreen->result ||
        !state.pendingPrefilterOffscreen->result->outputImage) {
        state.pendingPrefilterOffscreen.reset();
        detail::retireRenderImageNow(state.prefilterRenderImage);
        detail::rebuildPrefilterViews(state);
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("preprocess failed");
        return;
    }

    if (!state.pendingPrefilterOffscreen->isGpuCompleted()) {
        return;
    }

    state.prefilterRenderImage = state.pendingPrefilterOffscreen->result->outputImage;
    detail::rebuildPrefilterViews(state);
    state.pendingPrefilterOffscreen.reset();
    ++state.resultVersion;
    makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
        .to(EEnvironmentLightingPrefilterResolveState::Ready, "prefilter preprocess completed");
}

void resolveEnvironmentSourceState(ResourceResolveSystem&           system,
                                   entt::entity                     entity,
                                   EnvironmentLightingComponent&    component,
                                   EnvironmentLightingRuntimeState& state)
{
    switch (state.sourceState) {
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
                                       EnvironmentLightingRuntimeState& state,
                                       const SkyboxRuntimeState*        sceneSkyboxState)
{
    switch (state.irradianceState) {
    case EEnvironmentLightingIrradianceResolveState::Dirty:
    {
        handleEnvironmentIrradianceDirty(system, entity, component, state, sceneSkyboxState);
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
                                      EnvironmentLightingRuntimeState& state,
                                      const SkyboxRuntimeState*        sceneSkyboxState)
{
    switch (state.prefilterState) {
    case EEnvironmentLightingPrefilterResolveState::Dirty:
    {
        handleEnvironmentPrefilterDirty(system, entity, component, state, sceneSkyboxState);
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
        YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Entity");

        auto& pendingState = _environmentStates[entity];
        if (pendingState.authoringVersion != elc.authoringVersion) {
            detail::resetEnvState(pendingState);
            pendingState.authoringVersion = elc.authoringVersion;
        }

        if (!elc.hasSource()) {
            handleEnvironmentNoSource(elc, pendingState);
            continue;
        }

        if (pendingState.sourceState == EEnvironmentLightingSourceResolveState::Dirty ||
            pendingState.sourceState == EEnvironmentLightingSourceResolveState::Empty) {
            detail::resetEnvPending(pendingState);
        }

        syncEnvironmentDerivedBranchEnablement(elc, pendingState);

        if (const auto* sourceSkyboxState = syncEnvSkybox(elc, pendingState, sceneSkyboxState)) {
            completeEnvironmentSourceFromDependency(elc, pendingState, "scene skybox source resolved");
        }

        {
            YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Source");
            resolveEnvironmentSourceState(*this, entity, elc, pendingState);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Irradiance");
            resolveEnvironmentIrradianceState(*this, entity, elc, pendingState, sceneSkyboxState);
        }
        {
            YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Prefilter");
            resolveEnvironmentPrefilterState(*this, entity, elc, pendingState, sceneSkyboxState);
        }
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

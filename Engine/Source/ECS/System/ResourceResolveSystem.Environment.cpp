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

std::string buildEnvironmentDerivedKey(const EnvironmentLightingComponent& component,
                                      AssetManager*                       assets,
                                      const SkyboxRuntimeState*          sceneSkyboxState)
{
    const auto baseSuffix = std::format("|irr={}|pref={}|irrsize={}",
                                        component.bEnableIrradiance ? 1 : 0,
                                        component.bEnablePrefilter ? 1 : 0,
                                        component.getResolvedIrradianceFaceSize());

    if (component.usesSceneSkybox()) {
        if (!sceneSkyboxState || !sceneSkyboxState->hasRenderableCubemap() || sceneSkyboxState->derivedKey.empty()) {
            return {};
        }
        return std::format("env|scene-skybox|{}{}", sceneSkyboxState->derivedKey, baseSuffix);
    }

    if (!assets || !component.hasSource()) {
        return {};
    }

    if (component.hasCubemapSource()) {
        std::string key = std::format("env|cubefaces|flip={}", component.cubemapSource.flipVertical ? 1 : 0);
        for (const auto& path : component.cubemapSource.files) {
            const auto normalized = AssetManager::normalizeAssetPath(path);
            key += std::format("|{}|v{}", normalized, assets->getResourceVersion(normalized));
        }
        key += baseSuffix;
        return key;
    }

    const auto normalized = AssetManager::normalizeAssetPath(component.cylindricalSource.filepath);
    return std::format("env|cyl|{}|v{}|flip={}{}",
                       normalized,
                       assets->getResourceVersion(normalized),
                       component.cylindricalSource.flipVertical ? 1 : 0,
                       baseSuffix);
}

void applyEnvironmentResource(const std::shared_ptr<EnvironmentLightingDerivedResource>& resource,
                              EnvironmentLightingRuntimeState&                            state)
{
    state.boundResource         = resource;
    state.cubemapRenderImage    = resource ? resource->cubemapRenderImage : nullptr;
    state.cubemapTexture        = resource ? resource->cubemapTexture : nullptr;
    state.irradianceRenderImage = resource ? resource->irradianceRenderImage : nullptr;
    state.prefilterRenderImage  = resource ? resource->prefilterRenderImage : nullptr;
    state.cubemapFacePreviewViews.fill(nullptr);
    state.irradianceFacePreviewViews.fill(nullptr);
    for (auto& mipViews : state.prefilterMipFacePreviewViews) {
        mipViews.fill(nullptr);
    }
    state.prefilterPreviewMipCount = resource ? resource->prefilterPreviewMipCount : 0;

    if (!resource) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        state.cubemapFacePreviewViews[faceIndex]    = resource->cubemapFacePreviewViews[faceIndex];
        state.irradianceFacePreviewViews[faceIndex] = resource->irradianceFacePreviewViews[faceIndex];
    }

    for (uint32_t mipIndex = 0; mipIndex < resource->prefilterPreviewMipCount; ++mipIndex) {
        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
            state.prefilterMipFacePreviewViews[mipIndex][faceIndex] =
                resource->prefilterMipFacePreviewViews[mipIndex][faceIndex];
        }
    }
}

std::shared_ptr<EnvironmentLightingDerivedResource> snapshotEnvironmentResource(const EnvironmentLightingRuntimeState& state)
{
    auto resource                         = std::make_shared<EnvironmentLightingDerivedResource>();
    resource->cubemapRenderImage         = state.cubemapRenderImage;
    resource->cubemapTexture             = state.cubemapTexture;
    resource->cubemapFacePreviewViews    = state.cubemapFacePreviewViews;
    resource->irradianceRenderImage      = state.irradianceRenderImage;
    resource->irradianceFacePreviewViews = state.irradianceFacePreviewViews;
    resource->prefilterRenderImage       = state.prefilterRenderImage;
    resource->prefilterMipFacePreviewViews = state.prefilterMipFacePreviewViews;
    resource->prefilterPreviewMipCount   = state.prefilterPreviewMipCount;
    resource->lastUsedFrame              = App::currentFrameIndex();
    return resource;
}

bool isEnvironmentResolveComplete(const EnvironmentLightingComponent& component,
                                  const EnvironmentLightingRuntimeState& state)
{
    return state.sourceState == EEnvironmentLightingSourceResolveState::Ready &&
           (!component.bEnableIrradiance || state.irradianceState == EEnvironmentLightingIrradianceResolveState::Ready) &&
           (!component.bEnablePrefilter || state.prefilterState == EEnvironmentLightingPrefilterResolveState::Ready);
}

bool canUseEnvironmentDerivedResource(const EnvironmentLightingComponent& component,
                                      const EnvironmentLightingDerivedResource& resource)
{
    return resource.hasRenderableCubemap() &&
           (!component.bEnableIrradiance || resource.hasIrradianceMap()) &&
           (!component.bEnablePrefilter || resource.hasPrefilterMap());
}

void completeEnvironmentSource(IRender*                         render,
                               EnvironmentLightingComponent&    component,
                               EnvironmentLightingRuntimeState& state,
                               const char*                      reason)
{
    detail::rebuildEnvironmentCubemapViews(render, state);
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

void syncEnvironmentDerivedBranchEnablement(IRender*                         render,
                                            EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    if (!component.bEnableIrradiance) {
        cancelOffscreenJob(state.pendingIrradianceOffscreen);
        detail::retireRenderImageNow(state.irradianceRenderImage);
        detail::rebuildEnvironmentIrradianceViews(render, state);
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
        detail::rebuildPrefilterViews(render, state);
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
        state.bSceneSkyboxDependencyReady = false;
        return nullptr;
    }

    const bool     bDependencyReady      = sceneSkyboxState && sceneSkyboxState->hasRenderableCubemap();
    const uint64_t currentResultVersion  = bDependencyReady ? sceneSkyboxState->resultVersion : 0;
    const bool     bDependencyChanged    = state.lastSceneSkyboxResultVersion != currentResultVersion ||
                                           state.bSceneSkyboxDependencyReady != bDependencyReady;
    if (bDependencyChanged) {
        detail::resetEnvState(state);
        state.lastSceneSkyboxResultVersion = currentResultVersion;
        state.bSceneSkyboxDependencyReady  = bDependencyReady;
        const auto nextSourceState         = bDependencyReady ? EEnvironmentLightingSourceResolveState::Dirty
                                                              : EEnvironmentLightingSourceResolveState::Empty;
        makeTransition(state.sourceState, "EnvironmentLighting.Source")
            .to(nextSourceState, "scene skybox dependency changed");
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance")
            .to(component.bEnableIrradiance
                    ? (bDependencyReady ? EEnvironmentLightingIrradianceResolveState::Dirty
                                        : EEnvironmentLightingIrradianceResolveState::Empty)
                    : EEnvironmentLightingIrradianceResolveState::Disabled,
                "scene skybox dependency changed");
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter")
            .to(component.bEnablePrefilter
                    ? (bDependencyReady ? EEnvironmentLightingPrefilterResolveState::Dirty
                                        : EEnvironmentLightingPrefilterResolveState::Empty)
                    : EEnvironmentLightingPrefilterResolveState::Disabled,
                "scene skybox dependency changed");
    }

    if (bDependencyReady && state.sourceState == EEnvironmentLightingSourceResolveState::Dirty) {
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

void rebuildCubeFaceViews(IRender*                                         render,
                          const stdptr<Texture>&                          texture,
                          const std::shared_ptr<RenderImage>&             renderImage,
                          std::array<stdptr<IImageView>, CubeFace_Count>& outViews,
                          const std::string&                              labelPrefix)
{
    clearCubeFaceViews(outViews);
    const auto image = getImageShared(renderImage, texture);
    if (!image || !getImageView(renderImage, texture)) {
        return;
    }

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

void rebuildEnvironmentCubemapViews(IRender* render, EnvironmentLightingRuntimeState& state)
{
    rebuildCubeFaceViews(render, state.cubemapTexture, state.cubemapRenderImage, state.cubemapFacePreviewViews, "EnvironmentCubemap");
}

void rebuildEnvironmentIrradianceViews(IRender* render, EnvironmentLightingRuntimeState& state)
{
    rebuildCubeFaceViews(render, nullptr, state.irradianceRenderImage, state.irradianceFacePreviewViews, "EnvironmentIrradiance");
}

void rebuildPrefilterViews(IRender* render, EnvironmentLightingRuntimeState& state)
{
    clearPrefilterViews(state);
    const auto prefilterImage = state.prefilterRenderImage ? state.prefilterRenderImage->getImageShared() : nullptr;
    if (!prefilterImage || !state.prefilterRenderImage || !state.prefilterRenderImage->getImageView()) {
        return;
    }

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
    cancelOffscreenJob(state.pendingEnvironmentOffscreen);
    cancelOffscreenJob(state.pendingIrradianceOffscreen);
    cancelOffscreenJob(state.pendingPrefilterOffscreen);
}

void resetEnvState(EnvironmentLightingRuntimeState& state)
{
    resetEnvPending(state);
    retireEnvTextures(state);
    state.resultVersion = 0;
    state.lastSceneSkyboxResultVersion = 0;
    state.bSceneSkyboxDependencyReady  = false;
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

        auto* render = system.getRender();
        auto cubemap = render ? Texture::createCubeMapFromMemory(*render, createInfo) : nullptr;
        if (!cubemap || !cubemap->isValid()) {
            detail::retireEnvTextures(state);
            transition.fail("cubemap creation failed");
            failEnvironmentDerivedBranches(component, state, "cubemap creation failed");
            return;
        }

        state.cubemapTexture = std::move(cubemap);
        state.cubemapRenderImage.reset();
        completeEnvironmentSource(system.getRender(), component, state, "cubemap source resolved");
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

void handleEnvironmentSourceBuildingCubemap(const OffscreenJobQueueService&  queueService,
                                            IRender*                         render,
                                            EnvironmentLightingComponent&    component,
                                            EnvironmentLightingRuntimeState& state)
{
    auto transition = makeTransition(state.sourceState, "EnvironmentLighting.Source");
    if (!state.pendingEnvironmentOffscreen) {
        transition.fail("preprocess job missing");
        failEnvironmentDerivedBranches(component, state, "preprocess job missing");
        return;
    }

    if (state.pendingEnvironmentOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(queueService, render, state.pendingEnvironmentOffscreen);
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
    completeEnvironmentSource(render, component, state, "environment cubemap preprocess completed");
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

void handleEnvironmentIrradianceBuilding(const OffscreenJobQueueService&  queueService,
                                         IRender*                         render,
                                         EnvironmentLightingComponent&    component,
                                         EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingIrradianceOffscreen) {
        makeTransition(state.irradianceState, "EnvironmentLighting.Irradiance").fail("preprocess job missing");
        return;
    }

    if (state.pendingIrradianceOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(queueService, render, state.pendingIrradianceOffscreen);
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
    detail::rebuildEnvironmentIrradianceViews(render, state);
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

void handleEnvironmentPrefilterBuilding(const OffscreenJobQueueService&  queueService,
                                        IRender*                         render,
                                        EnvironmentLightingComponent&    component,
                                        EnvironmentLightingRuntimeState& state)
{
    if (!state.pendingPrefilterOffscreen) {
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("preprocess job missing");
        return;
    }

    if (state.pendingPrefilterOffscreen->phase == EOffscreenJobPhase::Pending) {
        detail::tryQueueJob(queueService, render, state.pendingPrefilterOffscreen);
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
        detail::rebuildPrefilterViews(render, state);
        makeTransition(state.prefilterState, "EnvironmentLighting.Prefilter").fail("preprocess failed");
        return;
    }

    if (!state.pendingPrefilterOffscreen->isGpuCompleted()) {
        return;
    }

    state.prefilterRenderImage = state.pendingPrefilterOffscreen->result->outputImage;
    detail::rebuildPrefilterViews(render, state);
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
        handleEnvironmentSourceBuildingCubemap(system.getOffscreenJobQueueService(), system.getRender(), component, state);
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
        handleEnvironmentIrradianceBuilding(system.getOffscreenJobQueueService(), system.getRender(), component, state);
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
        handleEnvironmentPrefilterBuilding(system.getOffscreenJobQueueService(), system.getRender(), component, state);
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
    auto*       assets           = AssetManager::get();
    const auto* sceneSkyboxState = findFirstSceneSkyboxState(scene);

    auto pumpOne = [&](entt::entity entity) {
        YA_PROFILE_SCOPE("ResourceResolve/EnvironmentLighting/Entity");
        if (!registry.valid(entity) || !registry.all_of<EnvironmentLightingComponent>(entity)) {
            cleanupEnvironmentLightingState(entity);
            return;
        }

        auto& elc          = registry.get<EnvironmentLightingComponent>(entity);
        auto& pendingState = _environmentStates[entity];
        const auto previousResultVersion = pendingState.resultVersion;
        const std::string derivedKey = buildEnvironmentDerivedKey(elc, assets, sceneSkyboxState);

        if (pendingState.authoringVersion != elc.authoringVersion) {
            detail::resetEnvState(pendingState);
            pendingState.authoringVersion = elc.authoringVersion;
            pendingState.lastStartedAuthoringVersion = elc.authoringVersion;
        }

        if (elc.usesSceneSkybox()) {
            _sceneSkyboxEnvironmentDependents.insert(entity);
        }
        else {
            _sceneSkyboxEnvironmentDependents.erase(entity);
        }

        if (!elc.hasSource()) {
            handleEnvironmentNoSource(elc, pendingState);
            pendingState.derivedKey.clear();
            pendingState.boundResource.reset();
            pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
            _activeEnvironment.erase(entity);
            return;
        }

        if (!derivedKey.empty() && !pendingState.derivedKey.empty() && pendingState.derivedKey != derivedKey) {
            detail::resetEnvState(pendingState);
            pendingState.resultVersion = 0;
            makeTransition(pendingState.sourceState, "EnvironmentLighting.Source")
                .to(EEnvironmentLightingSourceResolveState::Dirty, "derived key changed");
            makeTransition(pendingState.irradianceState, "EnvironmentLighting.Irradiance")
                .to(elc.bEnableIrradiance
                        ? EEnvironmentLightingIrradianceResolveState::Dirty
                        : EEnvironmentLightingIrradianceResolveState::Disabled,
                    "derived key changed");
            makeTransition(pendingState.prefilterState, "EnvironmentLighting.Prefilter")
                .to(elc.bEnablePrefilter
                        ? EEnvironmentLightingPrefilterResolveState::Dirty
                        : EEnvironmentLightingPrefilterResolveState::Disabled,
                    "derived key changed");
        }

        if (!derivedKey.empty()) {
            if (auto it = _environmentDerivedResources.find(derivedKey); it != _environmentDerivedResources.end() &&
                it->second && canUseEnvironmentDerivedResource(elc, *it->second)) {
                const bool bCacheRebound = pendingState.resultVersion == 0 ||
                                           pendingState.derivedKey != derivedKey ||
                                           pendingState.boundResource.get() != it->second.get() ||
                                           pendingState.sourceState != EEnvironmentLightingSourceResolveState::Ready ||
                                           (elc.bEnableIrradiance && pendingState.irradianceState != EEnvironmentLightingIrradianceResolveState::Ready) ||
                                           (elc.bEnablePrefilter && pendingState.prefilterState != EEnvironmentLightingPrefilterResolveState::Ready);
                it->second->lastUsedFrame = App::currentFrameIndex();
                applyEnvironmentResource(it->second, pendingState);
                pendingState.derivedKey = derivedKey;
                if (bCacheRebound) {
                    makeTransition(pendingState.sourceState, "EnvironmentLighting.Source")
                        .to(EEnvironmentLightingSourceResolveState::Ready, "derived cache hit");
                    makeTransition(pendingState.irradianceState, "EnvironmentLighting.Irradiance")
                        .to(elc.bEnableIrradiance
                                ? EEnvironmentLightingIrradianceResolveState::Ready
                                : EEnvironmentLightingIrradianceResolveState::Disabled,
                            "derived cache hit");
                    makeTransition(pendingState.prefilterState, "EnvironmentLighting.Prefilter")
                        .to(elc.bEnablePrefilter
                                ? EEnvironmentLightingPrefilterResolveState::Ready
                                : EEnvironmentLightingPrefilterResolveState::Disabled,
                            "derived cache hit");
                    ++pendingState.resultVersion;
                }
                pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
                _activeEnvironment.erase(entity);
                return;
            }
        }

        if (pendingState.sourceState == EEnvironmentLightingSourceResolveState::Dirty ||
            pendingState.sourceState == EEnvironmentLightingSourceResolveState::Empty) {
            detail::resetEnvPending(pendingState);
            pendingState.lastStartedAuthoringVersion = elc.authoringVersion;
        }

        syncEnvironmentDerivedBranchEnablement(getRender(), elc, pendingState);

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

        if (!derivedKey.empty() && isEnvironmentResolveComplete(elc, pendingState)) {
            pendingState.derivedKey = derivedKey;
            auto resource = snapshotEnvironmentResource(pendingState);
            _environmentDerivedResources[derivedKey] = resource;
            pendingState.boundResource = resource;
        }

        const bool bActive = pendingState.sourceState == EEnvironmentLightingSourceResolveState::ResolvingSource ||
                             pendingState.sourceState == EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap ||
                             pendingState.irradianceState == EEnvironmentLightingIrradianceResolveState::Building ||
                             pendingState.prefilterState == EEnvironmentLightingPrefilterResolveState::Building;
        if (bActive) {
            _activeEnvironment.insert(entity);
        }
        else {
            pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
            _activeEnvironment.erase(entity);
        }

        if (pendingState.resultVersion != previousResultVersion) {
            pendingState.lastCompletedAuthoringVersion = elc.authoringVersion;
        }
    };

    while (!_dirtyEnvironmentQueue.empty()) {
        const auto entity = _dirtyEnvironmentQueue.front();
        _dirtyEnvironmentQueue.pop_front();
        _dirtyEnvironmentSet.erase(entity);
        pumpOne(entity);
    }

    std::vector<entt::entity> activeEntities(_activeEnvironment.begin(), _activeEnvironment.end());
    for (const auto entity : activeEntities) {
        pumpOne(entity);
    }

    std::vector<entt::entity> staleEntities;
    staleEntities.reserve(_environmentStates.size());
    for (const auto& [entity, state] : _environmentStates) {
        (void)state;
        if (!registry.valid(entity) || !registry.all_of<EnvironmentLightingComponent>(entity)) {
            staleEntities.push_back(entity);
        }
    }
    for (const auto entity : staleEntities) {
        cleanupEnvironmentLightingState(entity);
    }
}

} // namespace ya

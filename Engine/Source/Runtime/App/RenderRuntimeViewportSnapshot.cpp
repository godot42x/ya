#include "Render/Core/RenderImage.h"
#include "RenderRuntime.h"

#include "App.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

#include <bit>

namespace ya
{

namespace
{

constexpr uint32_t CATEGORY_SHADOW      = 0;
constexpr uint32_t CATEGORY_SKYBOX      = 1;
constexpr uint32_t CATEGORY_ENVIRONMENT = 2;
constexpr uint32_t CATEGORY_GBUFFER     = 3;
constexpr uint32_t CATEGORY_VIEWPORT    = 4;
constexpr uint32_t CATEGORY_SHARED      = 5;
constexpr uint32_t CATEGORY_POSTPROCESS = 6;

void hashCombine(size_t& seed, size_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

template <typename TValue>
void hashCombineValue(size_t& seed, const TValue& value)
{
    hashCombine(seed, std::hash<TValue>{}(value));
}

struct ViewportDebugBuilder
{
    RenderViewportDebugCatalog*                catalog = nullptr;
    std::vector<RenderViewportDebugImageSlot>& images;

    [[nodiscard]] uint32_t slotCount() const { return static_cast<uint32_t>(images.size()); }

    void addSlot(const RenderViewportDebugCatalog::Slot& meta, RenderViewportDebugImageSlot image)
    {
        if (catalog) {
            catalog->slots.push_back(meta);
        }
        images.push_back(std::move(image));
    }

    void addGroup(RenderViewportDebugCatalog::Group group)
    {
        if (catalog) {
            catalog->groups.push_back(std::move(group));
        }
    }
};

template <typename TGetter>
void appendShadowDebugSlots(ViewportDebugBuilder&          builder,
                            IImageView*                    directionalDepth,
                            const std::shared_ptr<IImage>& shadowDepthImage,
                            TGetter&&                      pointFaceGetter,
                            uint32_t                       categoryIndex)
{
    if (directionalDepth) {
        builder.addSlot({
                            .label         = "ShadowDirectionalDepth",
                            .categoryIndex = categoryIndex,
                            .aspectFlags   = EImageAspect::Depth,
                        },
                        {
                            .defaultView = directionalDepth,
                            .ownedView   = nullptr,
                            .image       = shadowDepthImage,
                        });
    }

    RenderViewportDebugCatalog::Group pointShadowGroup{
        .label         = "Point Shadow Cubemap",
        .type          = RenderViewportDebugCatalog::EGroupType::CubeMapFaces,
        .categoryIndex = categoryIndex,
        .beginIndex    = builder.slotCount(),
        .groupSize     = 6,
        .itemLabels    = {},
    };

    for (uint32_t pointLightIndex = 0; pointLightIndex < MAX_POINT_LIGHTS; ++pointLightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            if (auto* faceIV = pointFaceGetter(pointLightIndex, faceIndex)) {
                builder.addSlot({
                                    .label         = std::format("ShadowPoint{}_Face{}", pointLightIndex, faceIndex),
                                    .categoryIndex = categoryIndex,
                                    .aspectFlags   = EImageAspect::Depth,
                                },
                                {
                                    .defaultView = faceIV,
                                    .ownedView   = nullptr,
                                    .image       = shadowDepthImage,
                                });
            }
        }
    }

    pointShadowGroup.slotCount = builder.slotCount() - pointShadowGroup.beginIndex;
    if (pointShadowGroup.slotCount >= pointShadowGroup.groupSize) {
        builder.addGroup(std::move(pointShadowGroup));
    }
}

void appendForwardDebugSlots(const RenderRuntime& runtime, ViewportDebugBuilder& builder, const RenderPipelineDebugOutputCatalog& debugOutputs)
{
    if (!runtime._forwardPipeline) {
        return;
    }

    if (debugOutputs.bShadowMappingEnabled) {
        appendShadowDebugSlots(
            builder,
            debugOutputs.shadowDirectionalDepth,
            debugOutputs.shadowDepthImage,
            [&runtime](uint32_t pointLightIndex, uint32_t faceIndex)
            { return runtime.getShadowPointFaceDepthIV(pointLightIndex, faceIndex); },
            CATEGORY_SHADOW);
    }

    if (auto* scene = runtime._app->getSceneManager()->getActiveScene()) {
        auto* resolver = runtime._app->getResourceResolveSystem();
        if (resolver) {
            for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
                auto preview = resolver->getSkyboxPreview(entity);
                if (!preview.bHasRenderableCubemap || !preview.cubemapImage) {
                    continue;
                }

                RenderViewportDebugCatalog::Group skyboxGroup{
                    .label         = "Skybox Cubemap",
                    .type          = RenderViewportDebugCatalog::EGroupType::CubeMapFaces,
                    .categoryIndex = CATEGORY_SKYBOX,
                    .beginIndex    = builder.slotCount(),
                    .groupSize     = CubeFace_Count,
                    .itemLabels    = {},
                };

                for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                    auto* faceView = preview.cubemapFaceViews[faceIndex];
                    if (!faceView) {
                        continue;
                    }

                    builder.addSlot({
                                        .label         = std::format("SkyboxFace{}", faceIndex),
                                        .categoryIndex = CATEGORY_SKYBOX,
                                    },
                                    {
                                        .defaultView = faceView,
                                        .ownedView   = nullptr,
                                        .image       = preview.cubemapImage,
                                    });
                }

                skyboxGroup.slotCount = builder.slotCount() - skyboxGroup.beginIndex;
                if (skyboxGroup.slotCount >= skyboxGroup.groupSize) {
                    builder.addGroup(std::move(skyboxGroup));
                }
                break;
            }
        }
    }

    if (auto viewportDepth = debugOutputs.viewportDepthImageOwner; viewportDepth && viewportDepth->getImageView()) {
        builder.addSlot({
                            .label         = "ViewportDepth",
                            .categoryIndex = CATEGORY_VIEWPORT,
                            .aspectFlags   = EImageAspect::Depth,
                        },
                        {
                            .defaultView = viewportDepth->getImageView(),
                            .ownedView   = nullptr,
                            .image       = viewportDepth->getImageShared(),
                        });
    }
}

void appendDeferredDebugSlots(const RenderRuntime&                    runtime,
                              ViewportDebugBuilder&                   builder,
                              const RenderPipelineDebugOutputCatalog& debugOutputs,
                              const DeferredPipelineDebugViews&       deferredViews)
{
    if (!runtime._deferredPipeline) {
        return;
    }

    auto* positionTexture      = deferredViews.gBufferResources.color[0];
    auto* normalTexture        = deferredViews.gBufferResources.color[1];
    auto* albedoSpecTexture    = deferredViews.gBufferResources.color[2];
    auto* shadingModelTexture  = deferredViews.gBufferResources.color[3];
    auto* gbufferDepthTexture  = deferredViews.gBufferResources.depth;
    auto* viewportColorTexture = deferredViews.viewportResources.color;
    auto* viewportDepthTexture = deferredViews.viewportResources.depth;
    if (!positionTexture || !normalTexture || !albedoSpecTexture || !shadingModelTexture || !gbufferDepthTexture ||
        !viewportColorTexture || !viewportDepthTexture) {
        return;
    }

    builder.addSlot({
                        .label         = "Position",
                        .categoryIndex = CATEGORY_GBUFFER,
                    },
                    {
                        .defaultView = positionTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = positionTexture->getImageShared(),
                    });
    builder.addSlot({
                        .label         = "Normal",
                        .categoryIndex = CATEGORY_GBUFFER,
                    },
                    {
                        .defaultView = normalTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = normalTexture->getImageShared(),
                    });
    builder.addSlot({
                        .label         = "AlbedoSpec",
                        .categoryIndex = CATEGORY_GBUFFER,
                    },
                    {
                        .defaultView = albedoSpecTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = albedoSpecTexture->getImageShared(),
                    });
    builder.addSlot({
                        .label         = "ShadingModel",
                        .categoryIndex = CATEGORY_GBUFFER,
                    },
                    {
                        .defaultView = shadingModelTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = shadingModelTexture->getImageShared(),
                    });
    builder.addSlot({
                        .label         = "Depth",
                        .categoryIndex = CATEGORY_GBUFFER,
                        .aspectFlags   = EImageAspect::Depth,
                        .tint          = {1, 0, 0, 1},
                    },
                    {
                        .defaultView = gbufferDepthTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = gbufferDepthTexture->getImageShared(),
                    });

    if (auto ssaoTexture = deferredViews.ssaoTextureOwner; ssaoTexture && ssaoTexture->getImageView()) {
        builder.addSlot({
                            .label         = "SSAO",
                            .categoryIndex = CATEGORY_GBUFFER,
                        },
                        {
                            .defaultView = ssaoTexture->getImageView(),
                            .ownedView   = ssaoTexture->getImageViewShared(),
                            .image       = ssaoTexture->getImageShared(),
                        });
    }

    builder.addSlot({
                        .label         = "ViewPortColor0",
                        .categoryIndex = CATEGORY_VIEWPORT,
                    },
                    {
                        .defaultView = viewportColorTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = viewportColorTexture->getImageShared(),
                    });
    builder.addSlot({
                        .label         = "ViewportDepth",
                        .categoryIndex = CATEGORY_VIEWPORT,
                        .aspectFlags   = EImageAspect::Depth,
                        .tint          = {1, 0, 0, 1},
                    },
                    {
                        .defaultView = viewportDepthTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = viewportDepthTexture->getImageShared(),
                    });

    if (auto bloomExtract = debugOutputs.bloomExtractOwner; bloomExtract && bloomExtract->getImageView()) {
        builder.addSlot({
                            .label         = "BloomExtract",
                            .categoryIndex = CATEGORY_POSTPROCESS,
                        },
                        {
                            .defaultView = bloomExtract->getImageView(),
                            .ownedView   = nullptr,
                            .image       = bloomExtract->getImageShared(),
                        });
    }

    if (auto bloomBlur = debugOutputs.bloomBlurOwner; bloomBlur && bloomBlur->getImageView()) {
        builder.addSlot({
                            .label         = "BloomBlur",
                            .categoryIndex = CATEGORY_POSTPROCESS,
                        },
                        {
                            .defaultView = bloomBlur->getImageView(),
                            .ownedView   = nullptr,
                            .image       = bloomBlur->getImageShared(),
                        });
    }

    if (auto bloomComposite = debugOutputs.bloomCompositeOwner; bloomComposite && bloomComposite->getImageView()) {
        builder.addSlot({
                            .label         = "BloomComposite",
                            .categoryIndex = CATEGORY_POSTPROCESS,
                        },
                        {
                            .defaultView = bloomComposite->getImageView(),
                            .ownedView   = nullptr,
                            .image       = bloomComposite->getImageShared(),
                        });
    }

    if (auto postprocessOutput = debugOutputs.postprocessOutputImageOwner; postprocessOutput && postprocessOutput->getImageView()) {
        builder.addSlot({
                            .label         = "PostprocessOutput",
                            .categoryIndex = CATEGORY_POSTPROCESS,
                        },
                        {
                            .defaultView = postprocessOutput->getImageView(),
                            .ownedView   = nullptr,
                            .image       = postprocessOutput->getImageShared(),
                        });
    }

    if (debugOutputs.shadowDepthImage) {
        appendShadowDebugSlots(
            builder,
            debugOutputs.shadowDirectionalDepth,
            debugOutputs.shadowDepthImage,
            [&runtime](uint32_t pointLightIndex, uint32_t faceIndex)
            { return runtime.getShadowPointFaceDepthIV(pointLightIndex, faceIndex); },
            CATEGORY_SHADOW);
    }
}

void appendEnvironmentDebugSlots(const RenderRuntime& runtime, ViewportDebugBuilder& builder)
{
    if (!runtime._app || !runtime._app->getSceneManager()) {
        return;
    }

    if (auto* scene = runtime._app->getSceneManager()->getActiveScene()) {
        auto* resolver = runtime._app->getResourceResolveSystem();
        if (!resolver) {
            return;
        }

        for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
            (void)elc;
            auto preview = resolver->getEnvironmentLightingPreview(entity);

            if (preview.bHasRenderableCubemap && preview.cubemapImage) {
                RenderViewportDebugCatalog::Group cubemapGroup{
                    .label         = "Environment Cubemap",
                    .type          = RenderViewportDebugCatalog::EGroupType::CubeMapFaces,
                    .categoryIndex = CATEGORY_ENVIRONMENT,
                    .beginIndex    = builder.slotCount(),
                    .groupSize     = CubeFace_Count,
                    .itemLabels    = {},
                };

                for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                    auto* faceView = preview.cubemapFaceViews[faceIndex];
                    if (!faceView) {
                        continue;
                    }

                    builder.addSlot({
                                        .label         = std::format("EnvironmentFace{}", faceIndex),
                                        .categoryIndex = CATEGORY_ENVIRONMENT,
                                    },
                                    {
                                        .defaultView = faceView,
                                        .ownedView   = nullptr,
                                        .image       = preview.cubemapImage,
                                    });
                }

                cubemapGroup.slotCount = builder.slotCount() - cubemapGroup.beginIndex;
                if (cubemapGroup.slotCount >= cubemapGroup.groupSize) {
                    builder.addGroup(std::move(cubemapGroup));
                }
            }

            if (preview.bHasIrradianceMap && preview.irradianceImage) {
                RenderViewportDebugCatalog::Group irradianceGroup{
                    .label         = "Environment Irradiance Cubemap",
                    .type          = RenderViewportDebugCatalog::EGroupType::CubeMapFaces,
                    .categoryIndex = CATEGORY_ENVIRONMENT,
                    .beginIndex    = builder.slotCount(),
                    .groupSize     = CubeFace_Count,
                    .itemLabels    = {},
                };

                for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                    auto* faceView = preview.irradianceFaceViews[faceIndex];
                    if (!faceView) {
                        continue;
                    }

                    builder.addSlot({
                                        .label         = std::format("IrradianceFace{}", faceIndex),
                                        .categoryIndex = CATEGORY_ENVIRONMENT,
                                    },
                                    {
                                        .defaultView = faceView,
                                        .ownedView   = nullptr,
                                        .image       = preview.irradianceImage,
                                    });
                }

                irradianceGroup.slotCount = builder.slotCount() - irradianceGroup.beginIndex;
                if (irradianceGroup.slotCount >= irradianceGroup.groupSize) {
                    builder.addGroup(std::move(irradianceGroup));
                }
            }

            if (preview.bHasPrefilterMap && preview.prefilterImage && preview.prefilterMipCount > 0) {
                const uint32_t                    mipLevels = preview.prefilterMipCount;
                RenderViewportDebugCatalog::Group prefilterGroup{
                    .label         = "Environment Prefilter Cubemap",
                    .type          = RenderViewportDebugCatalog::EGroupType::CubeMapMipFaces,
                    .categoryIndex = CATEGORY_ENVIRONMENT,
                    .beginIndex    = builder.slotCount(),
                    .groupSize     = CubeFace_Count,
                    .itemLabels    = {},
                };
                prefilterGroup.itemLabels.reserve(mipLevels);

                for (uint32_t mipIndex = 0; mipIndex < mipLevels; ++mipIndex) {
                    const float roughness = mipLevels <= 1 ? 0.0f : static_cast<float>(mipIndex) / static_cast<float>(mipLevels - 1);
                    prefilterGroup.itemLabels.push_back(std::format("Mip {} (Roughness {:.2f})", mipIndex, roughness));

                    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                        auto* faceView = preview.prefilterMipFaceViews[mipIndex][faceIndex];
                        if (!faceView) {
                            continue;
                        }

                        builder.addSlot({
                                            .label         = std::format("Prefilter_Mip{}_Face{}", mipIndex, faceIndex),
                                            .categoryIndex = CATEGORY_ENVIRONMENT,
                                        },
                                        {
                                            .defaultView = faceView,
                                            .ownedView   = nullptr,
                                            .image       = preview.prefilterImage,
                                        });
                    }
                }

                prefilterGroup.slotCount = builder.slotCount() - prefilterGroup.beginIndex;
                if (prefilterGroup.slotCount >= prefilterGroup.groupSize) {
                    builder.addGroup(std::move(prefilterGroup));
                }
            }

            break;
        }
    }
}

} // namespace

std::shared_ptr<RenderImage> RenderRuntime::getViewportSnapshotImageShared() const
{
    if (_renderPipeline == ERenderPipeline::Forward) {
        if (!_forwardPipeline) {
            return nullptr;
        }
        if (auto postprocessOutput = _forwardPipeline->getPostprocessOutputImageShared()) {
            return postprocessOutput;
        }
        return _forwardPipeline->getViewportOutputImageShared();
    }

    if (_renderPipeline == ERenderPipeline::Deferred) {
        if (!_deferredPipeline) {
            return nullptr;
        }
        if (auto postprocessOutput = _deferredPipeline->getPostprocessOutputImageShared()) {
            return postprocessOutput;
        }
        return _deferredPipeline->getViewportOutputImageShared();
    }

    return nullptr;
}

size_t RenderRuntime::buildViewportDebugCatalogSignature() const
{
    size_t seed = 0;
    hashCombineValue(seed, static_cast<int>(_renderPipeline));

    const auto debugOutputs  = buildPipelineDebugOutputCatalog();
    const auto deferredViews = getDeferredPipelineDebugViews();

    hashCombineValue(seed, debugOutputs.bShadowMappingEnabled);
    hashCombineValue(seed, debugOutputs.shadowDirectionalDepth != nullptr);
    hashCombineValue(seed, debugOutputs.viewportDepthImageOwner != nullptr);
    hashCombineValue(seed, debugOutputs.bloomExtractOwner != nullptr);
    hashCombineValue(seed, debugOutputs.bloomBlurOwner != nullptr);
    hashCombineValue(seed, debugOutputs.bloomCompositeOwner != nullptr);
    hashCombineValue(seed, debugOutputs.postprocessOutputImageOwner != nullptr);
    hashCombineValue(seed, _sharedResourceProvider.getBrdfLutTextureShared() != nullptr);

    uint64_t pointShadowFaceMask = 0;
    for (uint32_t pointLightIndex = 0; pointLightIndex < MAX_POINT_LIGHTS; ++pointLightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const uint32_t bitIndex = pointLightIndex * 6 + faceIndex;
            if (bitIndex >= 64) {
                break;
            }
            if (getShadowPointFaceDepthIV(pointLightIndex, faceIndex)) {
                pointShadowFaceMask |= (uint64_t{1} << bitIndex);
            }
        }
    }
    hashCombineValue(seed, pointShadowFaceMask);

    if (_renderPipeline == ERenderPipeline::Deferred) {
        hashCombineValue(seed, deferredViews.gBufferResources.color[0] != nullptr);
        hashCombineValue(seed, deferredViews.gBufferResources.color[1] != nullptr);
        hashCombineValue(seed, deferredViews.gBufferResources.color[2] != nullptr);
        hashCombineValue(seed, deferredViews.gBufferResources.color[3] != nullptr);
        hashCombineValue(seed, deferredViews.gBufferResources.depth != nullptr);
        hashCombineValue(seed, deferredViews.viewportResources.color != nullptr);
        hashCombineValue(seed, deferredViews.viewportResources.depth != nullptr);
        hashCombineValue(seed, deferredViews.ssaoTextureOwner != nullptr);
    }

    if (_app && _app->getSceneManager()) {
        if (auto* scene = _app->getSceneManager()->getActiveScene()) {
            auto* resolver = _app->getResourceResolveSystem();
            if (resolver) {
                bool     bHasSkybox     = false;
                uint32_t skyboxFaceMask = 0;
                for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
                    auto preview = resolver->getSkyboxPreview(entity);
                    if (preview.bHasRenderableCubemap && preview.cubemapImage) {
                        bHasSkybox = true;
                        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                            if (preview.cubemapFaceViews[faceIndex]) {
                                skyboxFaceMask |= (1u << faceIndex);
                            }
                        }
                        break;
                    }
                }
                hashCombineValue(seed, bHasSkybox);
                hashCombineValue(seed, skyboxFaceMask);

                bool     bHasEnvironmentCubemap    = false;
                uint32_t environmentFaceMask       = 0;
                bool     bHasEnvironmentIrradiance = false;
                uint32_t irradianceFaceMask        = 0;
                uint32_t prefilterMipCount         = 0;
                for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
                    (void)elc;
                    auto preview              = resolver->getEnvironmentLightingPreview(entity);
                    bHasEnvironmentCubemap    = preview.bHasRenderableCubemap && preview.cubemapImage;
                    bHasEnvironmentIrradiance = preview.bHasIrradianceMap && preview.irradianceImage;
                    if (bHasEnvironmentCubemap) {
                        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                            if (preview.cubemapFaceViews[faceIndex]) {
                                environmentFaceMask |= (1u << faceIndex);
                            }
                        }
                    }
                    if (bHasEnvironmentIrradiance) {
                        for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                            if (preview.irradianceFaceViews[faceIndex]) {
                                irradianceFaceMask |= (1u << faceIndex);
                            }
                        }
                    }
                    if (preview.bHasPrefilterMap && preview.prefilterImage) {
                        prefilterMipCount = preview.prefilterMipCount;
                    }
                    break;
                }

                hashCombineValue(seed, bHasEnvironmentCubemap);
                hashCombineValue(seed, environmentFaceMask);
                hashCombineValue(seed, bHasEnvironmentIrradiance);
                hashCombineValue(seed, irradianceFaceMask);
                hashCombineValue(seed, prefilterMipCount);
            }
        }
    }

    return seed;
}

void RenderRuntime::buildViewportDebugCatalog(RenderViewportDebugCatalog& catalog) const
{
    catalog.categories = {
        {.id = "shadow", .label = "Shadow"},
        {.id = "skybox", .label = "Skybox"},
        {.id = "environment", .label = "Environment"},
        {.id = "gbuffer", .label = "GBuffer"},
        {.id = "viewport", .label = "Viewport"},
        {.id = "shared", .label = "Shared"},
        {.id = "postprocess", .label = "PostFX"},
    };
    catalog.slots.clear();
    catalog.groups.clear();

    std::vector<RenderViewportDebugImageSlot> scratchImages;
    const auto                                debugOutputs  = buildPipelineDebugOutputCatalog();
    const auto                                deferredViews = getDeferredPipelineDebugViews();
    ViewportDebugBuilder                      builder{.catalog = &catalog, .images = scratchImages};

    if (_renderPipeline == ERenderPipeline::Forward) {
        appendForwardDebugSlots(*this, builder, debugOutputs);
    }
    else {
        appendDeferredDebugSlots(*this, builder, debugOutputs, deferredViews);
    }

    if (auto pbrLut = _sharedResourceProvider.getBrdfLutTextureShared(); pbrLut && pbrLut->getImageView()) {
        builder.addSlot({
                            .label         = "PBR_BRDF_LUT",
                            .categoryIndex = CATEGORY_SHARED,
                        },
                        {
                            .defaultView = pbrLut->getImageView(),
                            .ownedView   = nullptr,
                            .image       = pbrLut->getImageShared(),
                        });
    }

    appendEnvironmentDebugSlots(*this, builder);
}

void RenderRuntime::ensureViewportDebugCatalog() const
{
    const size_t signature = buildViewportDebugCatalogSignature();
    if (_viewportDebugCatalog && _viewportDebugCatalogSignature == signature) {
        return;
    }

    auto catalog = std::make_shared<RenderViewportDebugCatalog>();
    buildViewportDebugCatalog(*catalog);
    _viewportDebugCatalog          = std::move(catalog);
    _viewportDebugCatalogSignature = signature;
}

RenderViewportSnapshot RenderRuntime::buildViewportSnapshot() const
{
    const auto debugOutputs = buildPipelineDebugOutputCatalog();

    RenderViewportSnapshot snapshot;
    snapshot.bForwardPipeline       = (_renderPipeline == ERenderPipeline::Forward);
    snapshot.bPostprocessingEnabled = debugOutputs.bPostprocessingEnabled;
    snapshot.viewportImageOwner     = getViewportSnapshotImageShared();
    snapshot.viewportImageView      = snapshot.viewportImageOwner && snapshot.viewportImageOwner->getImageView()
                                        ? snapshot.viewportImageOwner->getImageView()
                                        : nullptr;

    ensureViewportDebugCatalog();
    snapshot.debugCatalog = _viewportDebugCatalog;
    if (snapshot.debugCatalog) {
        snapshot.debugImages.reserve(snapshot.debugCatalog->slots.size());
    }

    const auto           deferredViews = getDeferredPipelineDebugViews();
    ViewportDebugBuilder builder{.catalog = nullptr, .images = snapshot.debugImages};
    if (_renderPipeline == ERenderPipeline::Forward) {
        appendForwardDebugSlots(*this, builder, debugOutputs);
    }
    else {
        appendDeferredDebugSlots(*this, builder, debugOutputs, deferredViews);
    }

    if (auto pbrLut = _sharedResourceProvider.getBrdfLutTextureShared(); pbrLut && pbrLut->getImageView()) {
        builder.addSlot({},
                        {
                            .defaultView = pbrLut->getImageView(),
                            .ownedView   = nullptr,
                            .image       = pbrLut->getImageShared(),
                        });
    }

    appendEnvironmentDebugSlots(*this, builder);
    return snapshot;
}

} // namespace ya

#include "Render3D/Debug/ViewportDebugCatalogBuilder.h"

#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Scene/Core/Scene.h"

#include <bit>
#include <format>

namespace ya
{

using ShadowConstants::FACES_PER_POINT_LIGHT;
using ShadowConstants::POINT_SHADOW_FACE_COUNT;

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
                            const std::shared_ptr<ImageResource>& directionalDepthResource,
                            TGetter&&                      pointFaceGetter,
                            uint32_t                       categoryIndex)
{
    if (directionalDepthResource && directionalDepthResource->getImageView() && directionalDepthResource->getImageShared()) {
        builder.addSlot({
                            .label         = "ShadowDirectionalDepth",
                            .categoryIndex = categoryIndex,
                            .aspectFlags   = EImageAspect::Depth,
                        },
                        {
                            .defaultView = directionalDepthResource->getImageView(),
                            .ownedView   = nullptr,
                            .image       = directionalDepthResource->getImageShared(),
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
            if (auto faceResource = pointFaceGetter(pointLightIndex, faceIndex); faceResource && faceResource->getImageView() && faceResource->getImageShared()) {
                builder.addSlot({
                                    .label         = std::format("ShadowPoint{}_Face{}", pointLightIndex, faceIndex),
                                    .categoryIndex = categoryIndex,
                                    .aspectFlags   = EImageAspect::Depth,
                                },
                                {
                                    .defaultView = faceResource->getImageView(),
                                    .ownedView   = nullptr,
                                    .image       = faceResource->getImageShared(),
                                });
            }
        }
    }

    pointShadowGroup.slotCount = builder.slotCount() - pointShadowGroup.beginIndex;
    if (pointShadowGroup.slotCount >= pointShadowGroup.groupSize) {
        builder.addGroup(std::move(pointShadowGroup));
    }
}

void appendSkyboxDebugSlots(const ViewportDebugCatalogInput& input, ViewportDebugBuilder& builder)
{
    Scene* scene = input.inspectScene;
    auto*  envProcessor = input.environmentLighting;
    if (!scene || !envProcessor) {
        return;
    }

    for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
        auto preview = envProcessor->getSkyboxPreview(entity);
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

void appendForwardDebugSlots(const ViewportDebugCatalogInput& input, ViewportDebugBuilder& builder)
{
    if (!input.bForwardPipeline) {
        return;
    }

    if (input.debugOutputs.bShadowMappingEnabled) {
        appendShadowDebugSlots(
            builder,
            input.debugOutputs.shadowDirectionalDepthResource,
            [&input](uint32_t pointLightIndex, uint32_t faceIndex)
            {
                const size_t index = static_cast<size_t>(pointLightIndex) * FACES_PER_POINT_LIGHT + faceIndex;
                return index < input.pointShadowFaces.size() ? input.pointShadowFaces[index] : nullptr;
            },
            CATEGORY_SHADOW);
    }

    appendSkyboxDebugSlots(input, builder);

    if (auto viewDepth = input.debugOutputs.viewDepthImageOwner;
        viewDepth && viewDepth->getImageView()) {
        builder.addSlot({
                            .label         = "ViewportDepth",
                            .categoryIndex = CATEGORY_VIEWPORT,
                            .aspectFlags   = EImageAspect::Depth,
                        },
                        {
                            .defaultView = viewDepth->getImageView(),
                            .ownedView   = nullptr,
                            .image       = viewDepth->getImageShared(),
                        });
    }
}

void appendDeferredDebugSlots(const ViewportDebugCatalogInput& input, ViewportDebugBuilder& builder)
{
    if (!input.bDeferredPipeline) {
        return;
    }

    const auto& debugOutputs       = input.debugOutputs;
    auto* positionTexture          = debugOutputs.gBufferColorOwners[0].get();
    auto* normalTexture            = debugOutputs.gBufferColorOwners[1].get();
    auto* albedoSpecTexture        = debugOutputs.gBufferColorOwners[2].get();
    auto* shadingModelTexture      = debugOutputs.gBufferColorOwners[3].get();
    auto* gbufferDepthTexture      = debugOutputs.viewDepthImageOwner.get();
    auto* viewColorTexture         = debugOutputs.viewOutputImageOwner.get();
    auto* viewDepthTexture         = debugOutputs.viewDepthImageOwner.get();
    if (!positionTexture || !normalTexture || !albedoSpecTexture || !shadingModelTexture || !gbufferDepthTexture ||
        !viewColorTexture || !viewDepthTexture) {
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

    if (auto ssaoTexture = debugOutputs.ssaoOwner; ssaoTexture && ssaoTexture->getImageView()) {
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
                        .defaultView = viewColorTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = viewColorTexture->getImageShared(),
                    });
    builder.addSlot({
                        .label         = "ViewportDepth",
                        .categoryIndex = CATEGORY_VIEWPORT,
                        .aspectFlags   = EImageAspect::Depth,
                        .tint          = {1, 0, 0, 1},
                    },
                    {
                        .defaultView = viewDepthTexture->getImageView(),
                        .ownedView   = nullptr,
                        .image       = viewDepthTexture->getImageShared(),
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

    if (debugOutputs.shadowDirectionalDepthResource) {
        appendShadowDebugSlots(
            builder,
            debugOutputs.shadowDirectionalDepthResource,
            [&input](uint32_t pointLightIndex, uint32_t faceIndex)
            {
                const size_t index = static_cast<size_t>(pointLightIndex) * FACES_PER_POINT_LIGHT + faceIndex;
                return index < input.pointShadowFaces.size() ? input.pointShadowFaces[index] : nullptr;
            },
            CATEGORY_SHADOW);
    }
}

void appendEnvironmentDebugSlots(const ViewportDebugCatalogInput& input, ViewportDebugBuilder& builder)
{
    Scene* scene        = input.inspectScene;
    auto*  envProcessor = input.environmentLighting;
    if (!scene || !envProcessor) {
        return;
    }

    {
        for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
            (void)elc;
            auto preview = envProcessor->getEnvironmentLightingPreview(entity);

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

size_t viewportDebugCatalogSignature(const ViewportDebugCatalogInput& input)
{
    size_t seed = 0;
    hashCombineValue(seed, input.bDeferredPipeline);

    const auto& debugOutputs = input.debugOutputs;

    hashCombineValue(seed, debugOutputs.bShadowMappingEnabled);
    hashCombineValue(seed, debugOutputs.shadowDirectionalDepthResource != nullptr);
    hashCombineValue(seed, debugOutputs.viewDepthImageOwner != nullptr);
    hashCombineValue(seed, debugOutputs.bloomExtractOwner != nullptr);
    hashCombineValue(seed, debugOutputs.bloomBlurOwner != nullptr);
    hashCombineValue(seed, debugOutputs.bloomCompositeOwner != nullptr);
    hashCombineValue(seed, debugOutputs.ssaoOwner != nullptr);
    hashCombineValue(seed, debugOutputs.postprocessOutputImageOwner != nullptr);
    hashCombineValue(seed, input.brdfLut != nullptr);

    uint64_t pointShadowFaceMask = 0;
    for (uint32_t pointLightIndex = 0; pointLightIndex < MAX_POINT_LIGHTS; ++pointLightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const uint32_t bitIndex = pointLightIndex * 6 + faceIndex;
            if (bitIndex >= 64) {
                break;
            }
            const size_t index = static_cast<size_t>(pointLightIndex) * FACES_PER_POINT_LIGHT + faceIndex;
            if (index < input.pointShadowFaces.size() && input.pointShadowFaces[index]) {
                pointShadowFaceMask |= (uint64_t{1} << bitIndex);
            }
        }
    }
    hashCombineValue(seed, pointShadowFaceMask);

    if (input.bDeferredPipeline) {
        for (const auto& gBufferColor : debugOutputs.gBufferColorOwners) {
            hashCombineValue(seed, gBufferColor != nullptr);
        }
        hashCombineValue(seed, debugOutputs.viewOutputImageOwner != nullptr);
        hashCombineValue(seed, debugOutputs.viewDepthImageOwner != nullptr);
    }

    if (input.inspectScene) {
        if (auto* scene = input.inspectScene) {
            auto* envProcessor = input.environmentLighting;
            if (envProcessor) {
                bool     bHasSkybox     = false;
                uint32_t skyboxFaceMask = 0;
                for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
                    auto preview = envProcessor->getSkyboxPreview(entity);
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
                    auto preview              = envProcessor->getEnvironmentLightingPreview(entity);
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

RenderViewportDebugCatalog buildViewportDebugCatalog(const ViewportDebugCatalogInput& input)
{
    RenderViewportDebugCatalog catalog;
    catalog.categories = {
        {.id = "shadow", .label = "Shadow"},
        {.id = "skybox", .label = "Skybox"},
        {.id = "environment", .label = "Environment"},
        {.id = "gbuffer", .label = "GBuffer"},
        {.id = "viewport", .label = "Viewport"},
        {.id = "shared", .label = "Shared"},
        {.id = "postprocess", .label = "PostFX"},
    };

    // The catalog path only needs metadata: the images are consumed straight
    // into the snapshot, so this builds the slot list without a second copy.
    std::vector<RenderViewportDebugImageSlot> scratchImages;
    appendViewportDebugImages(scratchImages, &catalog, input);
    return catalog;
}

void appendViewportDebugImages(std::vector<RenderViewportDebugImageSlot>& images,
                               RenderViewportDebugCatalog*                catalog,
                               const ViewportDebugCatalogInput&           input)
{
    ViewportDebugBuilder builder{.catalog = catalog, .images = images};

    if (input.bForwardPipeline) {
        appendForwardDebugSlots(input, builder);
    }
    else {
        appendDeferredDebugSlots(input, builder);
    }

    if (auto pbrLut = input.brdfLut; pbrLut && pbrLut->getImageView()) {
        builder.addSlot(catalog ? RenderViewportDebugCatalog::Slot{
                                     .label         = "PBR_BRDF_LUT",
                                     .categoryIndex = CATEGORY_SHARED,
                                 }
                               : RenderViewportDebugCatalog::Slot{},
                        {
                            .defaultView = pbrLut->getImageView(),
                            .ownedView   = nullptr,
                            .image       = pbrLut->getImageShared(),
                        });
    }

    appendEnvironmentDebugSlots(input, builder);
}

std::shared_ptr<const RenderViewportDebugCatalog> ViewportDebugCatalogCache::get(
    const ViewportDebugCatalogInput& input)
{
    const size_t signature = viewportDebugCatalogSignature(input);
    if (_catalog && _signature == signature) {
        return _catalog;
    }

    _catalog   = std::make_shared<const RenderViewportDebugCatalog>(buildViewportDebugCatalog(input));
    _signature = signature;
    return _catalog;
}

} // namespace ya

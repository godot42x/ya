#include "RenderRuntime.h"
#include "Render/Core/RenderImage.h"

#include "App.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Editor/EditorLayer.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

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

template <typename TGetter>
void appendShadowDebugSlots(EditorViewportContext& ctx,
                            IImageView*            directionalDepth,
                            Texture*               shadowDepthTexture,
                            TGetter&&              pointFaceGetter,
                            uint32_t               categoryIndex)
{
    if (directionalDepth) {
        ctx.debugSpec.slots.push_back({
            .label         = "ShadowDirectionalDepth",
            .defaultView   = directionalDepth,
            .ownedView     = nullptr,
            .image         = shadowDepthTexture ? shadowDepthTexture->getImageShared() : nullptr,
            .categoryIndex = categoryIndex,
            .aspectFlags   = EImageAspect::Depth,
        });
    }

    EditorViewportContext::DebugSpec::Group pointShadowGroup{
        .label         = "Point Shadow Cubemap",
        .type          = EditorViewportContext::DebugSpec::EGroupType::CubeMapFaces,
        .categoryIndex = categoryIndex,
        .beginIndex    = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
        .groupSize     = 6,
        .itemLabels    = {},
    };

    for (uint32_t pointLightIndex = 0; pointLightIndex < MAX_POINT_LIGHTS; ++pointLightIndex) {
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            if (auto* faceIV = pointFaceGetter(pointLightIndex, faceIndex)) {
                ctx.debugSpec.slots.push_back({
                    .label         = std::format("ShadowPoint{}_Face{}", pointLightIndex, faceIndex),
                    .defaultView   = faceIV,
                    .ownedView     = nullptr,
                    .image         = shadowDepthTexture ? shadowDepthTexture->getImageShared() : nullptr,
                    .categoryIndex = categoryIndex,
                    .aspectFlags   = EImageAspect::Depth,
                });
            }
        }
    }

    pointShadowGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - pointShadowGroup.beginIndex;
    if (pointShadowGroup.slotCount >= pointShadowGroup.groupSize) {
        ctx.debugSpec.groups.push_back(std::move(pointShadowGroup));
    }
}

} // namespace

void RenderRuntime::updateEditorViewportContext(EditorLayer* editorLayer)
{
    if (!editorLayer) {
        return;
    }

    const auto debugOutputs = buildPipelineDebugOutputCatalog();

    EditorViewportContext ctx;
    ctx.bForwardPipeline         = (_renderPipeline == ERenderPipeline::Forward);
    ctx.bPostprocessingEnabled   = debugOutputs.bPostprocessingEnabled;
    ctx.postprocessOutputTexture = debugOutputs.postprocessOutput;
    ctx.viewportTexture          = getActiveViewportTexture();
    ctx.debugSpec.categories     = {
        {.id = "shadow", .label = "Shadow"},
        {.id = "skybox", .label = "Skybox"},
        {.id = "environment", .label = "Environment"},
        {.id = "gbuffer", .label = "GBuffer"},
        {.id = "viewport", .label = "Viewport"},
        {.id = "shared", .label = "Shared"},
        {.id = "postprocess", .label = "PostFX"},
    };

    if (_renderPipeline == ERenderPipeline::Forward) {
        appendForwardDebugSlots(ctx);
    }
    else {
        appendDeferredDebugSlots(ctx);
    }

    if (auto* pbrLut = _sharedResourceProvider.getBrdfLutTexture(); pbrLut && pbrLut->getImageView()) {
        ctx.debugSpec.slots.push_back({
            .label         = "PBR_BRDF_LUT",
            .defaultView   = pbrLut->getImageView(),
            .ownedView     = nullptr,
            .image         = pbrLut->getImageShared(),
            .categoryIndex = CATEGORY_SHARED,
        });
    }

    appendEnvironmentDebugSlots(ctx);
    editorLayer->setViewportContext(ctx);
}

void RenderRuntime::appendForwardDebugSlots(EditorViewportContext& ctx)
{
    if (!_forwardPipeline) {
        return;
    }

    const auto debugOutputs = buildPipelineDebugOutputCatalog();

    if (debugOutputs.bShadowMappingEnabled) {
        appendShadowDebugSlots(
            ctx,
            debugOutputs.shadowDirectionalDepth,
            debugOutputs.shadowDepthTexture,
            [this](uint32_t pointLightIndex, uint32_t faceIndex)
            { return getShadowPointFaceDepthIV(pointLightIndex, faceIndex); },
            CATEGORY_SHADOW);
    }

    if (auto* scene = _app->getSceneManager()->getActiveScene()) {
        auto* resolver = _app->getResourceResolveSystem();
        if (resolver) {
            for (auto&& [entity, sc] : scene->getRegistry().view<SkyboxComponent>().each()) {
                auto preview = resolver->getSkyboxPreview(entity);
                if (!preview.bHasRenderableCubemap || !preview.cubemapTexture ||
                    !preview.cubemapTexture->getImageShared() || !preview.cubemapTexture->getImageView()) {
                    continue;
                }

                EditorViewportContext::DebugSpec::Group skyboxGroup{
                    .label         = "Skybox Cubemap",
                    .type          = EditorViewportContext::DebugSpec::EGroupType::CubeMapFaces,
                    .categoryIndex = CATEGORY_SKYBOX,
                    .beginIndex    = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
                    .groupSize     = CubeFace_Count,
                    .itemLabels    = {},
                };

                for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                    auto* faceView = preview.cubemapFaceViews[faceIndex];
                    if (!faceView) {
                        continue;
                    }

                    ctx.debugSpec.slots.push_back({
                        .label         = std::format("SkyboxFace{}", faceIndex),
                        .defaultView   = faceView,
                        .ownedView     = nullptr,
                        .image         = preview.cubemapTexture->getImageShared(),
                        .categoryIndex = CATEGORY_SKYBOX,
                    });
                }

                skyboxGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - skyboxGroup.beginIndex;
                if (skyboxGroup.slotCount >= skyboxGroup.groupSize) {
                    ctx.debugSpec.groups.push_back(std::move(skyboxGroup));
                }
                break;
            }
        }
    }

    if (auto* viewportDepth = debugOutputs.viewportDepthTexture) {
        ctx.debugSpec.slots.push_back({
            .label         = "ViewportDepth",
            .defaultView   = viewportDepth->getImageView(),
            .ownedView     = nullptr,
            .image         = viewportDepth->getImageShared(),
            .categoryIndex = CATEGORY_VIEWPORT,
            .aspectFlags   = EImageAspect::Depth,
        });
    }
}

void RenderRuntime::appendDeferredDebugSlots(EditorViewportContext& ctx)
{
    const auto debugOutputs = buildPipelineDebugOutputCatalog();
    const auto deferredViews = getDeferredPipelineDebugViews();
    if (!_deferredPipeline) {
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

    ctx.debugSpec.slots = {
        {
            .label         = "Position",
            .defaultView   = positionTexture->getImageView(),
            .ownedView     = nullptr,
            .image         = positionTexture->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        },
        {
            .label         = "Normal",
            .defaultView   = normalTexture->getImageView(),
            .ownedView     = nullptr,
            .image         = normalTexture->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        },
        {
            .label         = "AlbedoSpec",
            .defaultView   = albedoSpecTexture->getImageView(),
            .ownedView     = nullptr,
            .image         = albedoSpecTexture->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        },
        {
            .label         = "ShadingModel",
            .defaultView   = shadingModelTexture->getImageView(),
            .ownedView     = nullptr,
            .image         = shadingModelTexture->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        },
        {
            .label         = "Depth",
            .defaultView   = gbufferDepthTexture->getImageView(),
            .ownedView     = nullptr,
            .image         = gbufferDepthTexture->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
            .aspectFlags   = EImageAspect::Depth,
            .tint          = {1, 0, 0, 1},
        },
    };

    if (auto* ssaoTexture = deferredViews.ssaoTexture; ssaoTexture && ssaoTexture->getImageView()) {
        ctx.debugSpec.slots.push_back({
            .label         = "SSAO",
            .defaultView   = ssaoTexture->getImageView(),
            .ownedView     = nullptr,
            .image         = ssaoTexture->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        });
    }

    ctx.debugSpec.slots.push_back({
        .label         = "ViewPortColor0",
        .defaultView   = viewportColorTexture->getImageView(),
        .ownedView     = nullptr,
        .image         = viewportColorTexture->getImageShared(),
        .categoryIndex = CATEGORY_VIEWPORT,
    });
    ctx.debugSpec.slots.push_back({
        .label         = "ViewportDepth",
        .defaultView   = viewportDepthTexture->getImageView(),
        .ownedView     = nullptr,
        .image         = viewportDepthTexture->getImageShared(),
        .categoryIndex = CATEGORY_VIEWPORT,
        .aspectFlags   = EImageAspect::Depth,
        .tint          = {1, 0, 0, 1},
    });

    if (debugOutputs.bloomExtract && debugOutputs.bloomExtract->getImageView()) {
        ctx.debugSpec.slots.push_back({
            .label         = "BloomExtract",
            .defaultView   = debugOutputs.bloomExtract->getImageView(),
            .ownedView     = nullptr,
            .image         = debugOutputs.bloomExtract->getImageShared(),
            .categoryIndex = CATEGORY_POSTPROCESS,
        });
    }

    if (debugOutputs.bloomBlur && debugOutputs.bloomBlur->getImageView()) {
        ctx.debugSpec.slots.push_back({
            .label         = "BloomBlur",
            .defaultView   = debugOutputs.bloomBlur->getImageView(),
            .ownedView     = nullptr,
            .image         = debugOutputs.bloomBlur->getImageShared(),
            .categoryIndex = CATEGORY_POSTPROCESS,
        });
    }

    if (debugOutputs.bloomComposite && debugOutputs.bloomComposite->getImageView()) {
        ctx.debugSpec.slots.push_back({
            .label         = "BloomComposite",
            .defaultView   = debugOutputs.bloomComposite->getImageView(),
            .ownedView     = nullptr,
            .image         = debugOutputs.bloomComposite->getImageShared(),
            .categoryIndex = CATEGORY_POSTPROCESS,
        });
    }

    if (auto* postprocessOutput = debugOutputs.postprocessOutput; postprocessOutput && postprocessOutput->getImageView()) {
        ctx.debugSpec.slots.push_back({
            .label         = "PostprocessOutput",
            .defaultView   = postprocessOutput->getImageView(),
            .ownedView     = nullptr,
            .image         = postprocessOutput->getImageShared(),
            .categoryIndex = CATEGORY_POSTPROCESS,
        });
    }

    if (auto* shadowDepthTexture = debugOutputs.shadowDepthTexture) {
        appendShadowDebugSlots(
            ctx,
            debugOutputs.shadowDirectionalDepth,
            shadowDepthTexture,
            [this](uint32_t pointLightIndex, uint32_t faceIndex)
            { return getShadowPointFaceDepthIV(pointLightIndex, faceIndex); },
            CATEGORY_SHADOW);
    }
}

void RenderRuntime::appendEnvironmentDebugSlots(EditorViewportContext& ctx)
{
    if (!_app || !_app->getSceneManager()) {
        return;
    }

    if (auto* scene = _app->getSceneManager()->getActiveScene()) {
        auto* resolver = _app->getResourceResolveSystem();
        if (!resolver) {
            return;
        }

        for (auto&& [entity, elc] : scene->getRegistry().view<EnvironmentLightingComponent>().each()) {
            (void)elc;
            auto preview = resolver->getEnvironmentLightingPreview(entity);

            if (preview.bHasRenderableCubemap && preview.cubemapTexture &&
                preview.cubemapTexture->getImageShared() && preview.cubemapTexture->getImageView()) {
                EditorViewportContext::DebugSpec::Group cubemapGroup{
                    .label         = "Environment Cubemap",
                    .type          = EditorViewportContext::DebugSpec::EGroupType::CubeMapFaces,
                    .categoryIndex = CATEGORY_ENVIRONMENT,
                    .beginIndex    = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
                    .groupSize     = CubeFace_Count,
                    .itemLabels    = {},
                };

                for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                    auto* faceView = preview.cubemapFaceViews[faceIndex];
                    if (!faceView) {
                        continue;
                    }

                    ctx.debugSpec.slots.push_back({
                        .label         = std::format("EnvironmentFace{}", faceIndex),
                        .defaultView   = faceView,
                        .ownedView     = nullptr,
                        .image         = preview.cubemapTexture->getImageShared(),
                        .categoryIndex = CATEGORY_ENVIRONMENT,
                    });
                }

                cubemapGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - cubemapGroup.beginIndex;
                if (cubemapGroup.slotCount >= cubemapGroup.groupSize) {
                    ctx.debugSpec.groups.push_back(std::move(cubemapGroup));
                }
            }

            if (preview.bHasIrradianceMap && preview.irradianceTexture &&
                preview.irradianceTexture->getImageShared() && preview.irradianceTexture->getImageView()) {
                EditorViewportContext::DebugSpec::Group irradianceGroup{
                    .label         = "Environment Irradiance Cubemap",
                    .type          = EditorViewportContext::DebugSpec::EGroupType::CubeMapFaces,
                    .categoryIndex = CATEGORY_ENVIRONMENT,
                    .beginIndex    = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
                    .groupSize     = CubeFace_Count,
                    .itemLabels    = {},
                };

                for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                    auto* faceView = preview.irradianceFaceViews[faceIndex];
                    if (!faceView) {
                        continue;
                    }

                    ctx.debugSpec.slots.push_back({
                        .label         = std::format("IrradianceFace{}", faceIndex),
                        .defaultView   = faceView,
                        .ownedView     = nullptr,
                        .image         = preview.irradianceTexture->getImageShared(),
                        .categoryIndex = CATEGORY_ENVIRONMENT,
                    });
                }

                irradianceGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - irradianceGroup.beginIndex;
                if (irradianceGroup.slotCount >= irradianceGroup.groupSize) {
                    ctx.debugSpec.groups.push_back(std::move(irradianceGroup));
                }
            }

            if (preview.bHasPrefilterMap && preview.prefilterTexture && preview.prefilterMipCount > 0 &&
                preview.prefilterTexture->getImageShared() && preview.prefilterTexture->getImageView()) {
                auto prefilterImage = preview.prefilterTexture->getImageShared();
                if (prefilterImage) {
                    const uint32_t                          mipLevels = preview.prefilterMipCount;
                    EditorViewportContext::DebugSpec::Group prefilterGroup{
                        .label         = "Environment Prefilter Cubemap",
                        .type          = EditorViewportContext::DebugSpec::EGroupType::CubeMapMipFaces,
                        .categoryIndex = CATEGORY_ENVIRONMENT,
                        .beginIndex    = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
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

                            ctx.debugSpec.slots.push_back({
                                .label         = std::format("Prefilter_Mip{}_Face{}", mipIndex, faceIndex),
                                .defaultView   = faceView,
                                .ownedView     = nullptr,
                                .image         = prefilterImage,
                                .categoryIndex = CATEGORY_ENVIRONMENT,
                            });
                        }
                    }

                    prefilterGroup.slotCount = static_cast<uint32_t>(ctx.debugSpec.slots.size()) - prefilterGroup.beginIndex;
                    if (prefilterGroup.slotCount >= prefilterGroup.groupSize) {
                        ctx.debugSpec.groups.push_back(std::move(prefilterGroup));
                    }
                }
            }

            break;
        }
    }
}

} // namespace ya

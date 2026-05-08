#include "RenderRuntime.h"

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

void RenderRuntime::updateEditorViewportContext(const FrameInput& input)
{
    if (!input.editorLayer) {
        return;
    }

    EditorViewportContext ctx;
    ctx.bForwardPipeline         = (_shadingModel == EShadingModel::Forward);
    ctx.bPostprocessingEnabled   = isPostprocessingEnabled();
    ctx.postprocessOutputTexture = getPostprocessOutputTexture();
    ctx.viewportTexture          = getActiveViewportTexture();
    ctx.debugSpec.categories     = {
        {.id = "shadow", .label = "Shadow"},
        {.id = "skybox", .label = "Skybox"},
        {.id = "environment", .label = "Environment"},
        {.id = "gbuffer", .label = "GBuffer"},
        {.id = "viewport", .label = "Viewport"},
        {.id = "shared", .label = "Shared"},
    };

    if (_shadingModel == EShadingModel::Forward) {
        appendForwardDebugSlots(ctx);
    }
    else {
        appendDeferredDebugSlots(ctx);
    }

    if (_sharedResources.pbrLUT && _sharedResources.pbrLUT->getImageView()) {
        ctx.debugSpec.slots.push_back({
            .label         = "PBR_BRDF_LUT",
            .defaultView   = _sharedResources.pbrLUT->getImageView(),
            .ownedView     = nullptr,
            .image         = _sharedResources.pbrLUT->getImageShared(),
            .categoryIndex = CATEGORY_SHARED,
        });
    }

    appendEnvironmentDebugSlots(ctx);
    input.editorLayer->setViewportContext(ctx);
}

void RenderRuntime::appendForwardDebugSlots(EditorViewportContext& ctx)
{
    if (!_forwardPipeline) {
        return;
    }

    if (isShadowMappingEnabled()) {
        if (auto* directionalDepth = getShadowDirectionalDepthIV()) {
            ctx.debugSpec.slots.push_back({
                .label         = "ShadowDirectionalDepth",
                .defaultView   = directionalDepth,
                .ownedView     = nullptr,
                .image         = nullptr,
                .categoryIndex = CATEGORY_SHADOW,
            });
        }

        EditorViewportContext::DebugSpec::Group pointShadowGroup{
            .label         = "Point Shadow Cubemap",
            .type          = EditorViewportContext::DebugSpec::EGroupType::CubeMapFaces,
            .categoryIndex = CATEGORY_SHADOW,
            .beginIndex    = static_cast<uint32_t>(ctx.debugSpec.slots.size()),
            .groupSize     = 6,
            .itemLabels    = {},
        };
        for (uint32_t pointLightIndex = 0; pointLightIndex < MAX_POINT_LIGHTS; ++pointLightIndex) {
            for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
                if (auto* faceIV = getShadowPointFaceDepthIV(pointLightIndex, faceIndex)) {
                    ctx.debugSpec.slots.push_back({
                        .label         = std::format("ShadowPoint{}_Face{}", pointLightIndex, faceIndex),
                        .defaultView   = faceIV,
                        .ownedView     = nullptr,
                        .image         = nullptr,
                        .categoryIndex = CATEGORY_SHADOW,
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

    if (auto* viewportRT = _forwardPipeline->getViewportRT()) {
        if (auto* viewportFb = viewportRT->getCurFrameBuffer()) {
            if (auto* viewportDepth = viewportFb->getDepthTexture()) {
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
    }
}

void RenderRuntime::appendDeferredDebugSlots(EditorViewportContext& ctx)
{
    if (!_deferredPipeline || !_deferredPipeline->getGBufferRT() || !_deferredPipeline->getViewportRT()) {
        return;
    }

    auto* gbufferFb  = _deferredPipeline->getGBufferRT()->getCurFrameBuffer();
    auto* viewportFb = _deferredPipeline->getViewportRT()->getCurFrameBuffer();
    if (!gbufferFb || !viewportFb) {
        return;
    }

    ctx.debugSpec.slots = {
        {
            .label         = "Position",
            .defaultView   = gbufferFb->getColorTexture(0)->getImageView(),
            .ownedView     = nullptr,
            .image         = gbufferFb->getColorTexture(0)->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        },
        {
            .label         = "Normal",
            .defaultView   = gbufferFb->getColorTexture(1)->getImageView(),
            .ownedView     = nullptr,
            .image         = gbufferFb->getColorTexture(1)->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        },
        {
            .label         = "AlbedoSpec",
            .defaultView   = gbufferFb->getColorTexture(2)->getImageView(),
            .ownedView     = nullptr,
            .image         = gbufferFb->getColorTexture(2)->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
        },
        {
            .label         = "Depth",
            .defaultView   = gbufferFb->getDepthTexture()->getImageView(),
            .ownedView     = nullptr,
            .image         = gbufferFb->getDepthTexture()->getImageShared(),
            .categoryIndex = CATEGORY_GBUFFER,
            .aspectFlags   = EImageAspect::Depth,
            .tint          = {1, 0, 0, 1},
        },
    };

    ctx.debugSpec.slots.push_back({
        .label         = "ViewPortColor0",
        .defaultView   = viewportFb->getColorTexture(0) ? viewportFb->getColorTexture(0)->getImageView() : nullptr,
        .ownedView     = nullptr,
        .image         = viewportFb->getColorTexture(0) ? viewportFb->getColorTexture(0)->getImageShared() : nullptr,
        .categoryIndex = CATEGORY_VIEWPORT,
    });
    ctx.debugSpec.slots.push_back({
        .label         = "ViewportDepth",
        .defaultView   = viewportFb->getDepthTexture() ? viewportFb->getDepthTexture()->getImageView() : nullptr,
        .ownedView     = nullptr,
        .image         = viewportFb->getDepthTexture() ? viewportFb->getDepthTexture()->getImageShared() : nullptr,
        .categoryIndex = CATEGORY_VIEWPORT,
        .aspectFlags   = EImageAspect::Depth,
        .tint          = {1, 0, 0, 1},
    });

    if (_deferredPipeline->getShadowDepthRT()) {
        Texture* shadowDepthTexture = nullptr;
        if (auto* shadowFb = _deferredPipeline->getShadowDepthRT()->getCurFrameBuffer()) {
            shadowDepthTexture = shadowFb->getDepthTexture();
        }
        appendShadowDebugSlots(
            ctx,
            _deferredPipeline->getShadowDirectionalDepthIV(),
            shadowDepthTexture,
            [this](uint32_t pointLightIndex, uint32_t faceIndex)
            { return _deferredPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex); },
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

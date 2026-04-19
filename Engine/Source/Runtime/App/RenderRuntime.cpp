#include "RenderRuntime.h"

#include "App.h"
#include "Core/Async/TaskQueue.h"
#include "Core/Debug/RenderDocCapture.h"
#include "Core/UI/UIManager.h"
#include "DeferredRender/DeferredRenderPipeline.h"
#include "Editor/EditorLayer.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/2D/Render2D.h"
#include "Resource/AssetManager.h"
#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "utility.cc/ranges.h"

#include <glm/gtc/matrix_transform.hpp>

namespace ya
{

namespace
{

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

void RenderRuntime::onViewportResized(Rect2D rect)
{
    _viewportRect = rect;

    if (_forwardPipeline) {
        _forwardPipeline->onViewportResized(rect);
    }
    if (_deferredPipeline) {
        _deferredPipeline->onViewportResized(rect);
    }
}

void RenderRuntime::finalizeCompletedOffscreenJobs()
{
    ya::finalizeSubmittedOffscreenJobs(_submittedOffscreenJobs);
}

void RenderRuntime::offScreenRender()
{
    if (!_render || !_app || !_offscreenCmdBuf) {
        return;
    }

    if (_offscreenPending && _offscreenFence) {
        auto*   vkRender = static_cast<VulkanRender*>(_render);
        VkFence fence    = static_cast<VkFence>(_offscreenFence);
        vkWaitForFences(vkRender->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vkRender->getDevice(), 1, &fence);
        _offscreenPending = false;
        finalizeCompletedOffscreenJobs();
    }

    if (!_app->taskManager.hasOffscreenTasks()) {
        return;
    }

    auto cmdBuf = _offscreenCmdBuf;
    cmdBuf->reset();
    if (!cmdBuf->begin()) {
        YA_CORE_ERROR("Failed to begin offscreen command buffer");
        return;
    }

    _submittedOffscreenJobs.clear();
    _app->taskManager.updateOffscreenTasks(cmdBuf.get(), &_submittedOffscreenJobs);

    if (!cmdBuf->end()) {
        YA_CORE_ERROR("Failed to end offscreen command buffer");
        return;
    }

    _render->submitToQueue({cmdBuf->getHandle()}, {}, {}, _offscreenFence);
    _offscreenPending = true;
}

void RenderRuntime::renderFrame(const FrameInput& input)
{
    YA_PROFILE_FUNCTION()

    applyPendingShadingModelSwitch();
    offScreenRender();

    if (_viewportRect.extent.x <= 0 || _viewportRect.extent.y <= 0) {
        if (input.viewportRect.extent.x > 0 && input.viewportRect.extent.y > 0) {
            onViewportResized(input.viewportRect);
        }
        else {
            auto swapchainExtent = _render->getSwapchain()->getExtent();
            onViewportResized(Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = {static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height)},
            });
        }
    }
    _viewportFrameBufferScale = input.viewportFrameBufferScale;

    if (_renderDoc.capture) {
        _renderDoc.capture->onFrameBegin();
    }

    if (_render->getSwapchain()->getExtent().width <= 0 || _render->getSwapchain()->getExtent().height <= 0) {
        return;
    }

    _render->waitIdle();

    int32_t imageIndex = -1;
    if (!_render->begin(&imageIndex)) {
        return;
    }
    if (imageIndex < 0) {
        YA_CORE_WARN("Invalid image index ({}), skipping frame render", imageIndex);
        return;
    }

    auto cmdBuf = _commandBuffers[imageIndex];
    cmdBuf->reset();
    cmdBuf->begin();

    if (_shadingModel == EShadingModel::Forward) {
        _forwardPipeline->tick(ForwardRenderPipeline::TickDesc{
            .cmdBuf                   = cmdBuf.get(),
            .dt                       = input.dt,
            .view                     = input.view,
            .projection               = input.projection,
            .cameraPos                = input.cameraPos,
            .viewportRect             = _viewportRect,
            .viewportFrameBufferScale = _viewportFrameBufferScale,
            .appMode                  = static_cast<int>(input.appMode),
            .clicked                  = input.clicked,
            .frameData                = input.frameData,
        });
    }
    else {
        _deferredPipeline->tick(DeferredRenderPipeline::TickDesc{
            .cmdBuf                   = cmdBuf.get(),
            .sceneManager             = input.sceneManager,
            .dt                       = input.dt,
            .view                     = input.view,
            .projection               = input.projection,
            .cameraPos                = input.cameraPos,
            .viewportRect             = _viewportRect,
            .viewportFrameBufferScale = _viewportFrameBufferScale,
            .appMode                  = static_cast<int>(input.appMode),
            .clicked                  = input.clicked,
            .frameData                = input.frameData,
        });
    }

    const bool bViewportPassOpen = (_shadingModel == EShadingModel::Forward)
                                     ? (_forwardPipeline && _forwardPipeline->hasOpenViewportPass())
                                     : (_deferredPipeline && _deferredPipeline->hasOpenViewportPass());

    if (bViewportPassOpen) {
        YA_PROFILE_SCOPE("Render2D")

        const Extent2D viewportExtent = (_shadingModel == EShadingModel::Forward)
                                          ? _forwardPipeline->getViewportExtent()
                                          : _deferredPipeline->getViewportExtent();

        FRender2dContext render2dCtx{
            .cmdBuf       = cmdBuf.get(),
            .windowWidth  = viewportExtent.width,
            .windowHeight = viewportExtent.height,
            .cam          = {
                .position       = input.cameraPos,
                .view           = input.view,
                .projection     = input.projection,
                .viewProjection = input.projection * input.view,
            },
        };

        Render2D::begin(render2dCtx);

        if (input.appMode == AppMode::Drawing && input.editorLayer && input.clicked) {
            for (const auto&& [idx, p] : ut::enumerate(*input.clicked)) {
                auto tex = idx % 2 == 0
                             ? AssetManager::get()->getTextureByName("uv1")
                             : AssetManager::get()->getTextureByName("face");
                YA_CORE_ASSERT(tex, "Texture not found");
                glm::vec2 pos;
                input.editorLayer->screenToViewport(glm::vec2(p.x, p.y), pos);
                Render2D::makeSprite(glm::vec3(pos, 0.0f), {50, 50}, tex);
            }
        }

        auto scene = input.sceneManager ? input.sceneManager->getActiveScene() : nullptr;
        if (scene) {
            const glm::vec2 screenSize(30, 30);
            const float     viewPortHeight = static_cast<float>(viewportExtent.height);
            const float     scaleFactor    = screenSize.x / viewPortHeight;

            for (const auto& [entity, billboard, transfCompp] :
                 scene->getRegistry().view<BillboardComponent, TransformComponent>().each()) {
                auto        texture = billboard.image.hasPath() ? billboard.image.textureRef.getShared() : nullptr;
                const auto& pos     = transfCompp.getWorldPosition();

                glm::vec3 billboardToCamera = input.cameraPos - pos;
                float     distance          = glm::length(billboardToCamera);
                billboardToCamera           = glm::normalize(billboardToCamera);

                glm::vec3 forward = billboardToCamera;
                glm::vec3 worldUp = glm::vec3(0, 1, 0);
                glm::vec3 right   = glm::normalize(glm::cross(worldUp, forward));
                glm::vec3 up      = glm::cross(forward, right);

                glm::mat4 rot(1.0f);
                rot[0] = glm::vec4(right, 0.0f);
                rot[1] = glm::vec4(up, 0.0f);
                rot[2] = glm::vec4(forward, 0.0f);

                float     factor = scaleFactor * distance * 2.0f;
                glm::vec3 scale  = glm::vec3(factor, factor, 1.0f);

                glm::mat4 trans = glm::mat4(1.0);
                trans           = glm::translate(trans, pos);
                trans           = trans * rot;
                trans           = glm::scale(trans, scale);

                Render2D::makeWorldSprite(trans, texture);
            }
        }

        Render2D::onRender();
        UIManager::get()->render();
        Render2D::onRenderGUI();
        Render2D::end();
    }

    if (bViewportPassOpen) {
        if (_shadingModel == EShadingModel::Forward) {
            _forwardPipeline->endViewportPass(cmdBuf.get());
            YA_CORE_ASSERT(_forwardPipeline->viewportTexture, "Failed to get viewport texture for postprocessing");
        }
        else {
            _deferredPipeline->endViewportPass(cmdBuf.get());
            YA_CORE_ASSERT(_deferredPipeline->viewportTexture, "Failed to get deferred viewport texture");
        }
    }

    if (input.editorLayer) {
        EditorViewportContext ctx;
        ctx.bForwardPipeline         = (_shadingModel == EShadingModel::Forward);
        ctx.bPostprocessingEnabled   = isPostprocessingEnabled();
        ctx.postprocessOutputTexture = getPostprocessOutputTexture();
        ctx.viewportTexture          = (_shadingModel == EShadingModel::Forward)
                                         ? (_forwardPipeline ? _forwardPipeline->viewportTexture : nullptr)
                                         : (_deferredPipeline ? _deferredPipeline->viewportTexture : nullptr);
        ctx.debugSpec.categories     = {
            {.id = "shadow", .label = "Shadow"},
            {.id = "skybox", .label = "Skybox"},
            {.id = "environment", .label = "Environment"},
            {.id = "gbuffer", .label = "GBuffer"},
            {.id = "viewport", .label = "Viewport"},
            {.id = "shared", .label = "Shared"},
        };
        constexpr uint32_t CATEGORY_SHADOW      = 0;
        constexpr uint32_t CATEGORY_SKYBOX      = 1;
        constexpr uint32_t CATEGORY_ENVIRONMENT = 2;
        constexpr uint32_t CATEGORY_GBUFFER     = 3;
        constexpr uint32_t CATEGORY_VIEWPORT    = 4;
        constexpr uint32_t CATEGORY_SHARED      = 5;

        if (_shadingModel == EShadingModel::Forward) {
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
                            auto slot = EditorViewportContext::ImageSlot{
                                .label         = std::format("ShadowPoint{}_Face{}", pointLightIndex, faceIndex),
                                .defaultView   = faceIV,
                                .ownedView     = nullptr,
                                .image         = nullptr,
                                .categoryIndex = CATEGORY_SHADOW,
                                .aspectFlags   = EImageAspect::Depth,
                            };
                            ctx.debugSpec.slots.push_back(std::move(slot));
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

            if (auto* viewportDepth = _forwardPipeline->viewportRT->getCurFrameBuffer()->getDepthTexture()) {
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
        else {
            auto& fb            = *_deferredPipeline->_gBufferRT->getCurFrameBuffer();
            ctx.debugSpec.slots = {
                {
                    .label         = "Position",
                    .defaultView   = fb.getColorTexture(0)->getImageView(),
                    .ownedView     = nullptr,
                    .image         = fb.getColorTexture(0)->getImageShared(),
                    .categoryIndex = CATEGORY_GBUFFER,
                },
                {
                    .label         = "Normal",
                    .defaultView   = fb.getColorTexture(1)->getImageView(),
                    .ownedView     = nullptr,
                    .image         = fb.getColorTexture(1)->getImageShared(),
                    .categoryIndex = CATEGORY_GBUFFER,
                },
                {
                    .label         = "AlbedoSpec",
                    .defaultView   = fb.getColorTexture(2)->getImageView(),
                    .ownedView     = nullptr,
                    .image         = fb.getColorTexture(2)->getImageShared(),
                    .categoryIndex = CATEGORY_GBUFFER,
                },
                {
                    .label         = "Depth",
                    .defaultView   = fb.getDepthTexture()->getImageView(),
                    .ownedView     = nullptr,
                    .image         = fb.getDepthTexture()->getImageShared(),
                    .categoryIndex = CATEGORY_GBUFFER,
                    .aspectFlags   = EImageAspect::Depth,
                    .tint          = {1, 0, 0, 1},
                },
            };

            auto viewPortRT = _deferredPipeline->_viewportRT->getCurFrameBuffer();
            ctx.debugSpec.slots.push_back({
                .label         = "ViewPortColor0",
                .defaultView   = viewPortRT->getColorTexture(0) ? viewPortRT->getColorTexture(0)->getImageView() : nullptr,
                .ownedView     = nullptr,
                .image         = viewPortRT->getColorTexture(0) ? viewPortRT->getColorTexture(0)->getImageShared() : nullptr,
                .categoryIndex = CATEGORY_VIEWPORT,
            });
            ctx.debugSpec.slots.push_back({
                .label         = "ViewportDepth",
                .defaultView   = viewPortRT->getDepthTexture() ? viewPortRT->getDepthTexture()->getImageView() : nullptr,
                .ownedView     = nullptr,
                .image         = viewPortRT->getDepthTexture() ? viewPortRT->getDepthTexture()->getImageShared() : nullptr,
                .categoryIndex = CATEGORY_VIEWPORT,
                .aspectFlags   = EImageAspect::Depth,
                .tint          = {1, 0, 0, 1},
            });

            if (_deferredPipeline && _deferredPipeline->getShadowDepthRT()) {
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

        if (_sharedResources.pbrLUT && _sharedResources.pbrLUT->getImageView()) {
            ctx.debugSpec.slots.push_back({
                .label         = "PBR_BRDF_LUT",
                .defaultView   = _sharedResources.pbrLUT->getImageView(),
                .ownedView     = nullptr,
                .image         = _sharedResources.pbrLUT->getImageShared(),
                .categoryIndex = CATEGORY_SHARED,
            });
        }

        if (auto* scene = _app->getSceneManager()->getActiveScene()) {
            auto* resolver = _app->getResourceResolveSystem();
            if (resolver) {
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

        input.editorLayer->setViewportContext(ctx);
    }

    {
        YA_PROFILE_SCOPE("Screen pass")

        RenderingInfo ri{
            .label      = "Screen",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = _screenRT->getExtent().toVec2(),
            },
            .layerCount       = 1,
            .colorClearValues = {ClearValue::Black()},
            .renderTarget     = _screenRT.get(),
        };

        cmdBuf->beginRendering(ri);

        auto& imManager = ImGuiManager::get();
        imManager.beginFrame();
        if (_app) {
            _app->renderGUI(input.dt);
        }
        imManager.endFrame();
        imManager.render();

        if (_render->getAPI() == ERenderAPI::Vulkan) {
            imManager.submitVulkan(cmdBuf->getHandleAs<VkCommandBuffer>());
        }

        cmdBuf->endRendering(ri);
    }

    TaskQueue::get().processMainThreadCallbacks();

    cmdBuf->end();
    _render->end(imageIndex, {cmdBuf->getHandle()});

    if (_renderDoc.capture) {
        _renderDoc.capture->onFrameEnd();
    }
}

ForwardRenderPipeline* RenderRuntime::getForwardPipeline() const
{
    return _forwardPipeline.get();
}

bool RenderRuntime::isShadowMappingEnabled() const
{
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) return _forwardPipeline->isShadowMappingEnabled();
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) return _deferredPipeline->isShadowMappingEnabled();
    if (_forwardPipeline) return _forwardPipeline->isShadowMappingEnabled();
    if (_deferredPipeline) return _deferredPipeline->isShadowMappingEnabled();
    return false;
}

bool RenderRuntime::isMirrorRenderingEnabled() const
{
    return false;
}

bool RenderRuntime::hasMirrorRenderResult() const
{
    return false;
}

IRenderTarget* RenderRuntime::getShadowDepthRT() const
{
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) return _forwardPipeline->getShadowDepthRT();
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) return _deferredPipeline->getShadowDepthRT();
    if (_forwardPipeline) return _forwardPipeline->getShadowDepthRT();
    if (_deferredPipeline) return _deferredPipeline->getShadowDepthRT();
    return nullptr;
}

IImageView* RenderRuntime::getShadowDirectionalDepthIV() const
{
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) return _forwardPipeline->getShadowDirectionalDepthIV();
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) return _deferredPipeline->getShadowDirectionalDepthIV();
    if (_forwardPipeline) return _forwardPipeline->getShadowDirectionalDepthIV();
    if (_deferredPipeline) return _deferredPipeline->getShadowDirectionalDepthIV();
    return nullptr;
}

IImageView* RenderRuntime::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    if (_shadingModel == EShadingModel::Forward && _forwardPipeline) {
        return _forwardPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    if (_shadingModel == EShadingModel::Deferred && _deferredPipeline) {
        return _deferredPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    if (_forwardPipeline) {
        return _forwardPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    if (_deferredPipeline) {
        return _deferredPipeline->getShadowPointFaceDepthIV(pointLightIndex, faceIndex);
    }
    return nullptr;
}

Texture* RenderRuntime::getPostprocessOutputTexture() const
{
    if (_forwardPipeline) return _forwardPipeline->_postProcessStage.getOutputTexture();
    if (_deferredPipeline) return _deferredPipeline->_postProcessStage.getOutputTexture();
    return nullptr;
}

bool RenderRuntime::isPostprocessingEnabled() const
{
    if (_forwardPipeline) return _forwardPipeline->_postProcessStage.isEnabled();
    if (_deferredPipeline) return _deferredPipeline->_postProcessStage.isEnabled();
    return false;
}

Extent2D RenderRuntime::getViewportExtent() const
{
    if (_forwardPipeline) {
        return _forwardPipeline->getViewportExtent();
    }
    if (_deferredPipeline) {
        return _deferredPipeline->getViewportExtent();
    }
    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        return Extent2D::fromVec2(_viewportRect.extent);
    }
    return {};
}

void RenderRuntime::initActivePipeline()
{
    int winW = 0;
    int winH = 0;
    _render->getWindowSize(winW, winH);

    if (_shadingModel == EShadingModel::Forward) {
        _forwardPipeline = ya::makeShared<ForwardRenderPipeline>();
        _forwardPipeline->init(ForwardRenderPipeline::InitDesc{
            .render  = _render,
            .windowW = winW,
            .windowH = winH,
        });
    }
    else {
        _deferredPipeline = ya::makeShared<DeferredRenderPipeline>();
        _deferredPipeline->init(DeferredRenderPipeline::InitDesc{
            .render  = _render,
            .windowW = winW,
            .windowH = winH,
        });
    }

    if (_shadingModel == EShadingModel::Forward) {
        Render2D::init(_render, ForwardRenderPipeline::LINEAR_FORMAT, ForwardRenderPipeline::DEPTH_FORMAT);
    }
    else {
        Render2D::init(_render, _deferredPipeline->LINEAR_FORMAT, _deferredPipeline->DEPTH_FORMAT);
    }
}

void RenderRuntime::shutdownActivePipeline()
{
    Render2D::destroy();

    if (_forwardPipeline) {
        _forwardPipeline->shutdown();
        _forwardPipeline.reset();
    }
    if (_deferredPipeline) {
        _deferredPipeline->shutdown();
        _deferredPipeline.reset();
    }
}

void RenderRuntime::applyPendingShadingModelSwitch()
{
    if (_pendingShadingModel == _shadingModel) {
        return;
    }
    YA_PROFILE_FUNCTION_LOG();

    if ((_forwardPipeline && _forwardPipeline->hasOpenViewportPass()) ||
        (_deferredPipeline && _deferredPipeline->hasOpenViewportPass())) {
        YA_CORE_WARN("Skipping shading-model switch while a viewport pass is still open");
        return;
    }

    YA_CORE_INFO("Switching shading model: {} -> {}",
                 _shadingModel == EShadingModel::Forward ? "Forward" : "Deferred",
                 _pendingShadingModel == EShadingModel::Forward ? "Forward" : "Deferred");

    _render->waitIdle();

    shutdownActivePipeline();
    _shadingModel = _pendingShadingModel;
    initActivePipeline();

    if (_viewportRect.extent.x > 0 && _viewportRect.extent.y > 0) {
        onViewportResized(_viewportRect);
    }
}

} // namespace ya
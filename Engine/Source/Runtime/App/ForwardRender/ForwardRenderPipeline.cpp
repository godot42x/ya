#include "Runtime/App/ForwardRender/ForwardRenderPipeline.h"

#include "Core/Math/Math.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/System/3D/SkyboxSystem.h"
#include "ECS/System/Render/DebugRenderSystem.h"
#include "ECS/System/Render/PhongMaterialSystem.h"
#include "ECS/System/Render/SimpleMaterialSystem.h"
#include "ECS/System/Render/UnlitMaterialSystem.h"
#include "Platform/Render/Vulkan//VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"
#include "Render/Pipelines/ShadowMapping.h"
#include "Runtime/App/App.h"


#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ya
{

void ForwardRenderPipeline::init(const InitDesc& desc)
{
    _render = desc.render;

    // Initial viewport RT creation (subsequent changes go through dirty mechanism)
    RenderTargetCreateInfo viewportRTci{
        .label            = "Viewport RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width  = static_cast<uint32_t>(desc.windowW),
                             .height = static_cast<uint32_t>(desc.windowH)},
        .frameBufferCount = 1,
        .attachments      = {

            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = COLOR_FORMAT,
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                    .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 1,
                .format         = DEPTH_FORMAT,
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::Clear,
                .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .usage          = EImageUsage::DepthStencilAttachment,
            },
        },
    };
    viewportRT = ya::createRenderTarget(viewportRTci);
    YA_CORE_ASSERT(viewportRT, "Failed to create viewport render target");

    _postProcessStage.init(PostProcessStage::InitDesc{
        .render      = _render,
        .colorFormat = COLOR_FORMAT,
        .width       = static_cast<uint32_t>(desc.windowW),
        .height      = static_cast<uint32_t>(desc.windowH),
    });
    _deleter.push("PostProcessStage", [this](void*) {
        _postProcessStage.shutdown();
    });

    depthRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Shadow Map RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width = 512, .height = 512},
        .frameBufferCount = 1,
        .layerCount       = 1 + MAX_POINT_LIGHTS * 6,
        .attachments      = {

            .depthAttach = AttachmentDescription{
                .index            = 0,
                .format           = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT,
                .samples          = ESampleCount::Sample_1,
                .loadOp           = EAttachmentLoadOp::Clear,
                .storeOp          = EAttachmentStoreOp::Store,
                .stencilLoadOp    = EAttachmentLoadOp::DontCare,
                .stencilStoreOp   = EAttachmentStoreOp::DontCare,
                .initialLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout      = EImageLayout::ShaderReadOnlyOptimal,
                .usage            = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
                .imageCreateFlags = EImageCreateFlag::CubeCompatible,
            },
        },
    });
    _deleter.push("DepthRT", [this](void*) { depthRT.reset(); });

    // Create internal descriptor pool (owns skybox + shadow map descriptors)
    _descriptorPool = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "ForwardPipeline Descriptor Pool",
            .maxSets   = 200,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1 + (1 + MAX_POINT_LIGHTS), // skybox + depth shadow(point and directional)
                },
            },
        });
    _deleter.push("OwnedDescriptorPool", [this](void*) { _descriptorPool.reset(); });

    {
        skyBoxCubeMapDSL = IDescriptorSetLayout::create(
            _render,
            DescriptorSetLayoutDesc{
                .label    = "Skybox_CubeMap_DSL",
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            });
        skyBoxCubeMapDS = _descriptorPool->allocateDescriptorSets(skyBoxCubeMapDSL);
        _render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, skyBoxCubeMapDS.ptr, "Skybox_CubeMap_DS");
        _deleter.push("SkyboxCubeMapDSL", [this](void*) { skyBoxCubeMapDSL.reset(); });

        depthBufferDSL = IDescriptorSetLayout::create(
            _render,
            DescriptorSetLayoutDesc{
                .label    = "DepthBuffer_DSL",
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = MAX_POINT_LIGHTS,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            });
        depthBufferShadowDS = _descriptorPool->allocateDescriptorSets(depthBufferDSL);
        _render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, depthBufferShadowDS.ptr, "DepthBuffer_Shadow_DS");
        _deleter.push("DepthBufferDSL", [this](void*) { depthBufferDSL.reset(); });

        auto tf        = _render->getTextureFactory();
        auto shadowImg = depthRT->getCurFrameBuffer()->getDepthTexture()->getImageShared();

        shadowDirectionalDepthIV = tf->createImageView(
            shadowImg,
            ImageViewCreateInfo{
                .label          = "Shadow Map Directional Depth ImageView",
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Depth,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            });
        YA_CORE_ASSERT(shadowDirectionalDepthIV && shadowDirectionalDepthIV->getHandle(), "Failed to create shadow map directional depth image view");

        for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
            shadowPointCubeIVs[i] = tf->createImageView(
                shadowImg,
                ImageViewCreateInfo{
                    .label          = std::format("Shadow Point[{}] CubeIV", i),
                    .viewType       = EImageViewType::ViewCube,
                    .aspectFlags    = EImageAspect::Depth,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 1 + i * 6,
                    .layerCount     = 6,
                });
            for (uint32_t f = 0; f < 6; ++f) {
                shadowPointFaceIVs[i][f] = tf->createImageView(
                    shadowImg,
                    ImageViewCreateInfo{
                        .label          = std::format("Shadow Point[{}] Face[{}]", i, f),
                        .viewType       = EImageViewType::View2D,
                        .aspectFlags    = EImageAspect::Depth,
                        .baseMipLevel   = 0,
                        .levelCount     = 1,
                        .baseArrayLayer = 1 + i * 6 + f,
                        .layerCount     = 1,
                    });
            }
        }

        auto shadowAddressMode = ESamplerAddressMode::ClampToBorder;
        shadowSampler          = Sampler::create(
            SamplerDesc{
                         .label         = "shadow",
                         .minFilter     = EFilter::Linear,
                         .magFilter     = EFilter::Linear,
                         .mipmapMode    = ESamplerMipmapMode::Linear,
                         .addressModeU  = shadowAddressMode,
                         .addressModeV  = shadowAddressMode,
                         .addressModeW  = shadowAddressMode,
                         .mipLodBias    = 0.0f,
                         .maxAnisotropy = 1.0f,
                         .borderColor   = SamplerDesc::BorderColor{
                               .type  = SamplerDesc::EBorderColor::FloatOpaqueWhite,
                               .color = {1.0f, 1.0f, 1.0f, 1.0f},
                },
            });
        YA_CORE_ASSERT(shadowSampler, "Failed to create shadow sampler");
        _deleter.push("ShadowSampler", [this](void*) { shadowSampler.reset(); });

        _render->waitIdle();

        std::vector<DescriptorImageInfo> dsImageInfos;
        dsImageInfos.resize(MAX_POINT_LIGHTS);
        for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
            dsImageInfos[i] = DescriptorImageInfo{
                .imageView   = shadowPointCubeIVs[i]->getHandle(),
                .sampler     = shadowSampler->getHandle(),
                .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
            };
        }

        WriteDescriptorSet writeCubeMaps{
            .dstSet           = depthBufferShadowDS,
            .dstBinding       = 1,
            .dstArrayElement  = 0,
            .descriptorType   = EPipelineDescriptorType::CombinedImageSampler,
            .descriptorCount  = MAX_POINT_LIGHTS,
            .bufferInfos      = {},
            .imageInfos       = dsImageInfos,
            .texelBufferViews = {},
        };

        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneImage(depthBufferShadowDS, 0, shadowDirectionalDepthIV.get(), shadowSampler.get()),
            writeCubeMaps,
        });
        _deleter.push("Shadow ImageViews", [this](void*) {
            shadowDirectionalDepthIV.reset();
            for (auto& iv : shadowPointCubeIVs) iv.reset();
            for (auto& faces : shadowPointFaceIVs)
                for (auto& iv : faces) iv.reset();
        });

        simpleMaterialSystem = ya::makeShared<SimpleMaterialSystem>();
        simpleMaterialSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "SimpleMaterial Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        unlitMaterialSystem = ya::makeShared<UnlitMaterialSystem>();
        unlitMaterialSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "UnlitMaterial Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        phongMaterialSystem = ya::makeShared<PhongMaterialSystem>();
        phongMaterialSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "PhongMaterial Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });
        auto* phongSys       = phongMaterialSystem->as<PhongMaterialSystem>();
        _phongSharedLightUBO = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label         = "Phong_Shared_Light_UBO",
                .usage         = EBufferUsage::UniformBuffer,
                .size          = sizeof(PhongMaterialSystem::LightUBO),
                .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            });
        _phongViewportPassResources = phongSys->createScenePassResources("Phong_ViewportPass", _phongSharedLightUBO);

        debugRenderSystem = ya::makeShared<DebugRenderSystem>();
        debugRenderSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "DebugRender Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        skyboxSystem = ya::makeShared<SkyBoxSystem>();
        skyboxSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "Skybox Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {COLOR_FORMAT},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        shadowMappingSystem = ya::makeShared<ShadowMapping>();
        shadowMappingSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "ShadowMapping Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {},
                .depthAttachmentFormat   = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        _deleter.push("RenderSystems", [this](void*) {
            simpleMaterialSystem->onDestroy();
            simpleMaterialSystem.reset();
            unlitMaterialSystem->onDestroy();
            unlitMaterialSystem.reset();
            phongMaterialSystem->onDestroy();
            phongMaterialSystem.reset();
            debugRenderSystem->onDestroy();
            debugRenderSystem.reset();
            skyboxSystem->onDestroy();
            skyboxSystem.reset();
            shadowMappingSystem->onDestroy();
            shadowMappingSystem.reset();
        });

        _render->waitIdle();

        skyboxSystem->as<SkyBoxSystem>()->_cubeMapDS                    = skyBoxCubeMapDS;
        phongMaterialSystem->as<PhongMaterialSystem>()->skyBoxCubeMapDS = skyBoxCubeMapDS;
        shadowMappingSystem->as<ShadowMapping>()->setRenderTarget(depthRT);
        phongMaterialSystem->as<PhongMaterialSystem>()->depthBufferDS = depthBufferShadowDS;
        phongMaterialSystem->as<PhongMaterialSystem>()->setDirectionalShadowMappingEnabled(bShadowMapping);

        debugRenderSystem->bEnabled = false;
    }
}

void ForwardRenderPipeline::tick(const TickDesc& desc)
{
    auto sceneManager = App::get()->getSceneManager();
    YA_CORE_ASSERT(sceneManager, "ForwardRenderPipeline requires scene manager before tick");
    YA_CORE_ASSERT(desc.cmdBuf, "ForwardRenderPipeline requires command buffer");


    bool bViewPortRectValid = desc.viewportRect.extent.x > 0 && desc.viewportRect.extent.y > 0;
    if (!bViewPortRectValid)
    {
        return;
    }

    FrameContext ctx;
    ctx.view       = desc.view;
    ctx.projection = desc.projection;
    ctx.cameraPos  = desc.cameraPos;

    ctx.bHasDirectionalLight = false;
    auto scene               = sceneManager->getActiveScene();
    for (const auto& [et, dlc, tc] :
         scene->getRegistry().view<DirectionalLightComponent, TransformComponent>().each())
    {
        ctx.directionalLight.direction      = glm::normalize(tc.getForward());
        ctx.directionalLight.ambient        = dlc._ambient;
        ctx.directionalLight.diffuse        = dlc._diffuse;
        ctx.directionalLight.specular       = dlc._specular;
        ctx.directionalLight.projection     = FMath::orthographic(-dlc._orthoHalfWidth, dlc._orthoHalfWidth, -dlc._orthoHalfHeight, dlc._orthoHalfHeight, dlc._nearPlane, dlc._farPlane);
        ctx.directionalLight.view           = FMath::lookAt(-ctx.directionalLight.direction * dlc._lightDistance, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        ctx.directionalLight.viewProjection = ctx.directionalLight.projection * ctx.directionalLight.view;
        ctx.bHasDirectionalLight            = true;
        break;
    }
    if (!ctx.bHasDirectionalLight) {
        for (const auto& [et, dlc] :
             scene->getRegistry().view<DirectionalLightComponent>().each())
        {
            ctx.directionalLight.direction      = glm::normalize(dlc._direction);
            ctx.directionalLight.ambient        = dlc._ambient;
            ctx.directionalLight.diffuse        = dlc._diffuse;
            ctx.directionalLight.specular       = dlc._specular;
            ctx.directionalLight.projection     = FMath::orthographic(-dlc._orthoHalfWidth, dlc._orthoHalfWidth, -dlc._orthoHalfHeight, dlc._orthoHalfHeight, dlc._nearPlane, dlc._farPlane);
            ctx.directionalLight.view           = FMath::lookAt(-ctx.directionalLight.direction * dlc._lightDistance, glm::vec3(0.0f), glm::vec3(0, 1, 0));
            ctx.directionalLight.viewProjection = ctx.directionalLight.projection * ctx.directionalLight.view;
            ctx.bHasDirectionalLight            = true;
            break;
        }
    }

    ctx.numPointLights = 0;
    for (const auto& [et, plc, tc] :
         scene->getRegistry().view<PointLightComponent, TransformComponent>().each())
    {
        if (ctx.numPointLights >= MAX_POINT_LIGHTS) break;
        auto& pl       = ctx.pointLights[ctx.numPointLights];
        pl.type        = static_cast<float>(plc._type);
        pl.constant    = plc._constant;
        pl.linear      = plc._linear;
        pl.quadratic   = plc._quadratic;
        pl.position    = tc._position;
        pl.ambient     = plc._ambient;
        pl.diffuse     = plc._diffuse;
        pl.specular    = plc._specular;
        pl.spotDir     = tc.getForward();
        pl.innerCutOff = glm::cos(glm::radians(plc._innerConeAngle));
        pl.outerCutOff = glm::cos(glm::radians(plc._outerConeAngle));
        pl.nearPlane   = plc.nearPlane;
        pl.farPlane    = plc.farPlane;
        ++ctx.numPointLights;
    }

    auto* phongSys = phongMaterialSystem->as<PhongMaterialSystem>();
    phongSys->setDirectionalShadowMappingEnabled(bShadowMapping && ctx.bHasDirectionalLight);
    phongSys->uploadSharedLightUBO(&ctx, _phongSharedLightUBO.get());

    beginFrame();

    if (bShadowMapping)
    {
        RenderingInfo shadowMapRI{
            .label      = "Shadow Map Pass",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = depthRT->getExtent().toVec2(),
            },
            .colorClearValues = {},
            .depthClearValue  = ClearValue(1.0f, 0),
            .renderTarget     = depthRT.get(),
        };
        desc.cmdBuf->beginRendering(shadowMapRI);
        {
            ctx.extent = depthRT->getExtent();
            shadowMappingSystem->tick(desc.cmdBuf, desc.dt, &ctx);
        }
        desc.cmdBuf->endRendering(shadowMapRI);
    }


    {
        YA_PROFILE_SCOPE("ViewPort pass")

        auto extent = Extent2D::fromVec2(desc.viewportRect.extent / desc.viewportFrameBufferScale);
        viewportRT->setExtent(extent);

        RenderingInfo ri{
            .label      = "ViewPort",
            .renderArea = Rect2D{
                .pos    = {0, 0},
                .extent = viewportRT->getExtent().toVec2(),
            },
            .layerCount       = 1,
            .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
            .depthClearValue  = ClearValue(1.0f, 0),
            .renderTarget     = viewportRT.get(),
        };

        desc.cmdBuf->beginRendering(ri);

        ctx.extent = viewportRT->getExtent();
        renderScene(desc.cmdBuf, desc.dt, ctx, _phongViewportPassResources);

        // Viewport pass left open for App-level 2D rendering; App calls endViewportPass() after Render2D.
        _viewportRI   = ri;
        _lastTickCtx  = ctx;
        _lastTickDesc = desc;
    }
}

void ForwardRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    // Close viewport rendering pass (opened in tick())
    cmdBuf->endRendering(_viewportRI);

    // Determine the source color texture (MSAA → resolve, otherwise → color[0])
    auto  fb           = viewportRT->getCurFrameBuffer();
    auto* inputTexture = bMSAA ? fb->getResolveTexture() : fb->getColorTexture(0);

    // Run postprocessing via shared stage; returns input unchanged if disabled
    viewportTexture = _postProcessStage.execute(
        cmdBuf, inputTexture, _lastTickDesc.viewportRect.extent, _lastTickDesc.dt, &_lastTickCtx);

    YA_CORE_ASSERT(viewportTexture, "Failed to get viewport texture for postprocessing");
}

void ForwardRenderPipeline::shutdown()
{
    _deleter.clear();
}

void ForwardRenderPipeline::renderGUI()
{
    if (ImGui::CollapsingHeader("Render Systems", ImGuiTreeNodeFlags_DefaultOpen)) {
        simpleMaterialSystem->renderGUI();
        unlitMaterialSystem->renderGUI();
        phongMaterialSystem->renderGUI();
        debugRenderSystem->renderGUI();
        skyboxSystem->renderGUI();
        shadowMappingSystem->renderGUI();
        _postProcessStage.renderGUI();
    }

    if (ImGui::Checkbox("MSAA", &bMSAA)) {
        auto sampleCount = bMSAA ? ESampleCount::Sample_4 : ESampleCount::Sample_1;
        simpleMaterialSystem->getPipeline()->setSampleCount(sampleCount);
        unlitMaterialSystem->getPipeline()->setSampleCount(sampleCount);
        phongMaterialSystem->getPipeline()->setSampleCount(sampleCount);
        debugRenderSystem->getPipeline()->setSampleCount(sampleCount);
        skyboxSystem->getPipeline()->setSampleCount(sampleCount);

        // Mark RT dirty instead of full recreate — flushDirty() handles the rest
        viewportRT->setColorAttachmentSampleCount(0, sampleCount);
        viewportRT->setDepthAttachmentSampleCount(sampleCount);
        if (bMSAA) {
            viewportRT->setResolveAttachment(AttachmentDescription{
                .index          = 2,
                .format         = COLOR_FORMAT,
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::DontCare,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::ColorAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            });
        }
        else {
            viewportRT->clearResolveAttachment();
        }
    }

    if (ImGui::Checkbox("Shadow Mapping", &bShadowMapping)) {
        auto* phongSys = phongMaterialSystem->as<PhongMaterialSystem>();
        phongSys->setDirectionalShadowMappingEnabled(bShadowMapping);
    }
    if (viewportRT) {
        viewportRT->onRenderGUI();
    }
}


void ForwardRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    if (viewportRT) {
        viewportRT->setExtent(newExtent);
    }

    _postProcessStage.onViewportResized(newExtent);
}

Extent2D ForwardRenderPipeline::getViewportExtent() const
{
    if (viewportRT) {
        return viewportRT->getExtent();
    }
    return {};
}

void ForwardRenderPipeline::renderScene(ICommandBuffer* cmdBuf, float dt, FrameContext& ctx, const stdptr<PhongScenePassResources>& phongScenePassResources)
{
    simpleMaterialSystem->tick(cmdBuf, dt, &ctx);
    unlitMaterialSystem->tick(cmdBuf, dt, &ctx);
    phongMaterialSystem->as<PhongMaterialSystem>()->setScenePassResources(phongScenePassResources);
    phongMaterialSystem->tick(cmdBuf, dt, &ctx);
    debugRenderSystem->tick(cmdBuf, dt, &ctx);
    skyboxSystem->tick(cmdBuf, dt, &ctx);
    // transparent materials, could be a interface in phong material system?
}

void ForwardRenderPipeline::beginFrame()
{
    shadowMappingSystem->beginFrame();
    _postProcessStage.beginFrame();
    skyboxSystem->beginFrame();
    simpleMaterialSystem->beginFrame();
    unlitMaterialSystem->beginFrame();
    phongMaterialSystem->beginFrame();
    debugRenderSystem->beginFrame();
}

} // namespace ya

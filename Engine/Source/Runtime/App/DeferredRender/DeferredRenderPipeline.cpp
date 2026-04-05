#include "DeferredRenderPipeline.h"

#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Core/TextureFactory.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Material/UnlitMaterial.h"
#include "Resource/PrimitiveMeshCache.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

namespace ya
{

DeferredRenderPipeline::~DeferredRenderPipeline()
{
    shutdown();
}

void DeferredRenderPipeline::initRenderTargets(Extent2D extent)
{
    _gBufferRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "GBuffer RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = extent,
        .frameBufferCount = 1,
        .attachments      = {

            .colorAttach = {
                AttachmentDescription{
                    .index         = 0,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 1,
                    .format        = SIGNED_LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 2,
                    .format        = LINEAR_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
                AttachmentDescription{
                    .index         = 3,
                    .format        = SHADING_MODEL_FORMAT,
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                    .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 4,
                .format         = DEPTH_FORMAT,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::Clear,
                .stencilStoreOp = EAttachmentStoreOp::Store,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled | EImageUsage::TransferSrc,
            },
        },
    });

    _viewportRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Viewport RT",
        .bSwapChainTarget = false,
        .extent           = extent,
        .attachments      = {

            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = LINEAR_FORMAT,
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
                .loadOp         = EAttachmentLoadOp::Load,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled | EImageUsage::TransferDst,
            },
        },
    });
}

void DeferredRenderPipeline::initSharedResources()
{
    _frameAndLightDSL = IDescriptorSetLayout::create(
        _render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_Frame_And_Light_DSL",
            .set      = 0,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
            },
        }});
}

void DeferredRenderPipeline::initLightPassPipeline()
{
    _lightGBufferDSL = IDescriptorSetLayout::create(
        _render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_LightPass_GBuffer_DSL",
            .set      = 1,
            .bindings = {
                // GBuffer 1-4
                {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        }});

    _lightPPL = IPipelineLayout::create(
        _render,
        "Deferred_Light_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(LightPassPushConstant), .stageFlags = EShaderStage::Vertex}},
        {
            _frameAndLightDSL,
            _lightGBufferDSL,
            App::get()->getRenderRuntime()->getSkyboxDescriptorSetLayout(),
        });

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Light Pass",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _lightPPL.get(),
        .shaderDesc     = ShaderDesc{
                .shaderName        = "DeferredRender/Unified_LightPass.slang",
                .bDeriveFromShader = false,
                .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
                .vertexAttributes  = _commonVertexAttributes,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = false, .bDepthWriteEnable = false},
        .colorBlendState    = {.attachments = {ColorBlendAttachmentState{
                                   .index          = 0,
                                   .bBlendEnable   = false,
                                   .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                            }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };

    _lightPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_lightPipeline && _lightPipeline->recreate(ci), "Failed to create Light pipeline");
}

void DeferredRenderPipeline::initDescriptorsAndUBOs()
{
    _deferredDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Deferred_DSP",
            .maxSets   = 2,
            .poolSizes = {
                {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 2},
                {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 4},
            },
        });

    _frameAndLightDS = _deferredDSP->allocateDescriptorSets(_frameAndLightDSL);
    _lightTexturesDS = _deferredDSP->allocateDescriptorSets(_lightGBufferDSL);

    _frameUBO = IBuffer::create(
        _render,
        BufferCreateInfo{
            .label       = "Deferred_Frame_UBO",
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(PBRGBufferFrameData),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    _lightUBO = IBuffer::create(
        _render,
        BufferCreateInfo{
            .label       = "Deferred_Light_UBO",
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(LightPassLightData),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    _frameUBO->writeData(&_gBufferPassFrameData, sizeof(_gBufferPassFrameData), 0);
    _frameUBO->flush();
    _lightUBO->writeData(&_lightPassLightData, sizeof(_lightPassLightData), 0);
    _lightUBO->flush();

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 0, _frameUBO.get()),
        IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS, 1, _lightUBO.get()),
    });
}

// ═══════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::init(const InitDesc& desc)
{
    shutdown();

    _render            = desc.render;
    _bViewportPassOpen = false;
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    Extent2D extent{
        .width  = static_cast<uint32_t>(desc.windowW),
        .height = static_cast<uint32_t>(desc.windowH),
    };

    // Setup: RT + shared resources + light pass
    initRenderTargets(extent);
    initSharedResources();
    initLightPassPipeline();
    initDescriptorsAndUBOs();

    // Per-shading-model init
    initPBR();
    initPhong();
    initUnlit();

    // Fallback material (magenta checkerboard for meshes without material)
    {
        _fallbackMaterial = MaterialFactory::get()->createMaterial<UnlitMaterial>("__fallback_checkerboard__");
        auto& params      = _fallbackMaterial->getParamsMut();
        params.baseColor0 = glm::vec3(1.0f, 0.0f, 1.0f);
        params.baseColor1 = glm::vec3(1.0f, 0.0f, 1.0f);
        params.mixValue   = 0.0f;

        auto checkerTex = TextureLibrary::get().getCheckerboardTexture();
        if (checkerTex) {
            TextureBinding tb;
            tb.texture = checkerTex.get();
            tb.sampler = TextureLibrary::get().getNearestSampler();
            _fallbackMaterial->setTextureBinding(UnlitMaterial::BaseColor0, tb);
            params.textureParams[0].bEnable = true;
        }
        _fallbackMaterial->setParamDirty();
        _fallbackMaterial->setResourceDirty();
    }

    // Skybox
    _skyboxSystem = ya::makeShared<SkyBoxSystem>();
    _skyboxSystem->init(IRenderSystem::InitParams{
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                   = "Deferred Skybox Pipeline",
            .colorAttachmentFormats  = {LINEAR_FORMAT},
            .depthAttachmentFormat   = DEPTH_FORMAT,
            .stencilAttachmentFormat = EFormat::Undefined,
        },
    });
    _skyboxSystem->bReverseViewportY = true;

    // Simple material system (forward overlay — debug vis, direction cones)
    _simpleMaterialSystem = ya::makeShared<SimpleMaterialSystem>();
    _simpleMaterialSystem->init(IRenderSystem::InitParams{
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Deferred SimpleMaterial Overlay",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
    });
    _simpleMaterialSystem->bReverseViewportY = true;

    // PostProcess
    _postProcessStage.init(PostProcessStage::InitDesc{
        .render      = _render,
        .colorFormat = LINEAR_FORMAT,
        .width       = extent.width,
        .height      = extent.height,
    });

    _render->waitIdle();
}

void DeferredRenderPipeline::shutdown()
{
    shutdownAll();
    _render = nullptr;
}

void DeferredRenderPipeline::shutdownAll()
{
    _bViewportPassOpen = false;
    _postProcessStage.shutdown();
    viewportTexture = nullptr;

    // Release fallback material reference (owned by MaterialFactory)
    _fallbackMaterial = nullptr;

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;

    if (_skyboxSystem) {
        _skyboxSystem->onDestroy();
        _skyboxSystem.reset();
    }

    if (_simpleMaterialSystem) {
        _simpleMaterialSystem->onDestroy();
        _simpleMaterialSystem.reset();
    }

    // Render resources (reverse init order)
    _lightUBO.reset();
    _frameUBO.reset();
    _deferredDSP.reset();
    _lightPipeline.reset();
    _lightPPL.reset();
    _lightGBufferDSL.reset();

    _pbrGBufferPipeline.reset();
    _pbrGBufferPPL.reset();
    _pbrParamsDSL.reset();
    _pbrMaterialResourceDSL.reset();

    _phongGBufferPipeline.reset();
    _phongGBufferPPL.reset();
    _phongParamsDSL.reset();
    _phongMaterialResourceDSL.reset();

    _unlitGBufferPipeline.reset();
    _unlitGBufferPPL.reset();
    _unlitParamsDSL.reset();
    _unlitMaterialResourceDSL.reset();

    _frameAndLightDSL.reset();
    _viewportRT.reset();
    _gBufferRT.reset();
}

// ═══════════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::tick(const TickDesc& desc)
{
    YA_CORE_ASSERT(desc.cmdBuf, "DeferredRenderPipeline requires a command buffer");
    desc.cmdBuf->debugBeginLabel("Deferred Pipeline");

    if (desc.viewportRect.extent.x <= 0 || desc.viewportRect.extent.y <= 0) {
        desc.cmdBuf->debugEndLabel();
        return;
    }

    // Begin frame
    if (_skyboxSystem) _skyboxSystem->beginFrame();
    if (_simpleMaterialSystem) _simpleMaterialSystem->beginFrame();
    _postProcessStage.beginFrame();
    _pbrGBufferPipeline->beginFrame();
    _phongGBufferPipeline->beginFrame();
    _unlitGBufferPipeline->beginFrame();
    _lightPipeline->beginFrame();

    _viewportRT->flushDirty();
    _gBufferRT->flushDirty();

    auto* sceneManager = desc.sceneManager;
    auto* scene        = sceneManager ? sceneManager->getActiveScene() : nullptr;
    if (!scene) {
        desc.cmdBuf->debugEndLabel();
        return;
    }

    // Prepare + Execute
    preparePBR(scene);
    preparePhong(scene);
    prepareUnlit(scene);
    updateUBOs(desc, scene);
    executeGBufferPass(desc, scene);
    copyGBufferDepthToViewport(desc.cmdBuf);

    // Viewport pass (all stages share one render pass)
    beginViewportRendering(desc);
    executeLightPass(desc);
    executeSkybox(desc);
    executeForwardOverlay(desc);
    // executeTransparentPass(desc); // TODO

    desc.cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// UBO Updates
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::updateUBOs(const TickDesc& desc, Scene* scene)
{
    auto& reg = scene->getRegistry();

    _lightPassLightData.hasDirLight = false;
    for (const auto& [et, dlc, tc] :
         reg.view<DirectionalLightComponent, TransformComponent>().each()) {
        _lightPassLightData.dirLight.dir   = tc.getForward();
        _lightPassLightData.dirLight.color = dlc._color;
        _lightPassLightData.ambient        = glm::vec3(0.03);
        _lightPassLightData.hasDirLight    = true;
    }

    int pli = 0;
    for (const auto& [et, plc, tc] :
         reg.view<PointLightComponent, TransformComponent>().each()) {
        if (pli >= static_cast<int>(MAX_POINT_LIGHTS)) {
            YA_CORE_WARN("DeferredRenderPipeline: clamping point lights from scene to MAX_POINT_LIGHTS={}.", MAX_POINT_LIGHTS);
            break;
        }
        _lightPassLightData.pointLights[pli] = {
            .pos       = tc.getPosition(),
            .color     = plc.color,
            .intensity = plc.intensity,
        };
        ++pli;
    }
    _lightPassLightData.numPointLight = pli;
    // _lightPassLightData.dirLight.shininess = 32;

    _gBufferPassFrameData.viewPos    = desc.cameraPos;
    _gBufferPassFrameData.projMatrix = desc.projection;
    _gBufferPassFrameData.viewMatrix = desc.view;

    _frameUBO->writeData(&_gBufferPassFrameData, sizeof(_gBufferPassFrameData), 0);
    _frameUBO->flush();
    _lightUBO->writeData(&_lightPassLightData, sizeof(_lightPassLightData), 0);
    _lightUBO->flush();
}

// ═══════════════════════════════════════════════════════════════════════
// GBuffer Pass (orchestrates per-model draw calls)
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::executeGBufferPass(const TickDesc& desc, Scene* scene)
{
    auto           cmdBuf = desc.cmdBuf;
    const uint32_t vpW    = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH    = static_cast<uint32_t>(desc.viewportRect.extent.y);

    RenderingInfo gBufferRI{
        .label            = "GBuffer Pass",
        .renderArea       = Rect2D{.pos = {0, 0}, .extent = _gBufferRT->getExtent().toVec2()},
        .layerCount       = 1,
        .colorClearValues = {
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
            ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
        },
        .depthClearValue = ClearValue(1.0f, 0),
        .renderTarget    = _gBufferRT.get(),
    };
    cmdBuf->beginRendering(gBufferRI);

    float gbVpY = 0.0f;
    float gbVpH = static_cast<float>(vpH);
    if (_bReverseViewportY) {
        gbVpY = static_cast<float>(vpH);
        gbVpH = -gbVpH;
    }
    cmdBuf->setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
    cmdBuf->setScissor(0, 0, vpW, vpH);

    drawPBR(cmdBuf, scene);
    drawPhong(cmdBuf, scene);
    drawUnlit(cmdBuf, scene);
    drawFallback(cmdBuf, scene);

    cmdBuf->endRendering(gBufferRI);
}

void DeferredRenderPipeline::copyGBufferDepthToViewport(ICommandBuffer* cmdBuf)
{
    auto* gbufferDepth  = _gBufferRT ? _gBufferRT->getCurFrameBuffer()->getDepthTexture() : nullptr;
    auto* viewportDepth = _viewportRT ? _viewportRT->getCurFrameBuffer()->getDepthTexture() : nullptr;
    if (!cmdBuf || !gbufferDepth || !viewportDepth) {
        return;
    }

    auto* srcImage = gbufferDepth->getImage();
    auto* dstImage = viewportDepth->getImage();
    if (!srcImage || !dstImage) {
        return;
    }

    ImageSubresourceRange depthRange{
        .aspectMask     = EImageAspect::Depth,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    cmdBuf->debugBeginLabel("Deferred Depth Copy");
    cmdBuf->transitionImageLayoutAuto(srcImage,
                                      //   EImageLayout::ShaderReadOnlyOptimal,
                                      EImageLayout::TransferSrc,
                                      &depthRange);
    cmdBuf->transitionImageLayoutAuto(dstImage,
                                      //   EImageLayout::ShaderReadOnlyOptimal,
                                      EImageLayout::TransferDst,
                                      &depthRange);

    cmdBuf->copyImage(
        srcImage,
        EImageLayout::TransferSrc,
        dstImage,
        EImageLayout::TransferDst,
        {
            ImageCopy{
                .srcSubresource = ImageSubresourceLayers{
                    .aspectMask     = EImageAspect::Depth,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .dstSubresource = ImageSubresourceLayers{
                    .aspectMask     = EImageAspect::Depth,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .extentWidth  = _gBufferRT->getExtent().width,
                .extentHeight = _gBufferRT->getExtent().height,
                .extentDepth  = 1,
            },
        });

    cmdBuf->transitionImageLayoutAuto(srcImage,
                                      //   EImageLayout::TransferSrc,
                                      EImageLayout::ShaderReadOnlyOptimal,
                                      &depthRange);
    cmdBuf->transitionImageLayoutAuto(dstImage,
                                      //   EImageLayout::TransferDst,
                                      EImageLayout::ShaderReadOnlyOptimal,
                                      &depthRange);
    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// Viewport Pass — stages sharing one render pass (viewport RT + viewport depth)
// Order: Light → Skybox → ForwardOverlay → (future) Transparent
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::beginViewportRendering(const TickDesc& desc)
{
    auto cmdBuf = desc.cmdBuf;

    _viewportDepthSpec = RenderingInfo::ImageSpec{
        .texture       = _viewportRT->getCurFrameBuffer()->getDepthTexture(),
        .loadOp        = EAttachmentLoadOp::Load,
        .storeOp       = EAttachmentStoreOp::Store,
        .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
        .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
    };

    _viewportRI = RenderingInfo{
        .label            = "Viewport Pass",
        .renderArea       = {.pos = {0, 0}, .extent = _viewportRT->getExtent().toVec2()},
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 0.0f)},
        // .colorAttachments = {RenderingInfo::ImageSpec{
        //     .texture       = _viewportRT->getCurFrameBuffer()->getColorTexture(0),
        //     .initialLayout = EImageLayout::ColorAttachmentOptimal,
        //     .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
        // }},
        // .depthAttachment  = &_viewportDepthSpec,
        .renderTarget = _viewportRT.get(),
    };

    cmdBuf->beginRendering(_viewportRI);
    _bViewportPassOpen = true;

    const uint32_t vpW = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH = static_cast<uint32_t>(desc.viewportRect.extent.y);

    _lastTickCtx = {
        .view       = desc.view,
        .projection = desc.projection,
        .cameraPos  = desc.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _lastTickDesc = desc;
}

void DeferredRenderPipeline::executeLightPass(const TickDesc& desc)
{
    auto           cmdBuf = desc.cmdBuf;
    const uint32_t vpW    = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH    = static_cast<uint32_t>(desc.viewportRect.extent.y);

    auto* scene    = desc.sceneManager ? desc.sceneManager->getActiveScene() : nullptr;
    auto* runtime  = App::get()->getRenderRuntime();
    YA_CORE_ASSERT(runtime, "RenderRuntime is null");
    auto  skyboxDS = runtime->getSceneSkyboxDescriptorSet(scene);

    cmdBuf->debugBeginLabel("Light Pass");

    auto  sampler = TextureLibrary::get().getDefaultSampler();
    auto* fb      = _gBufferRT->getCurFrameBuffer();
    // TODO: each fb has it's ds cache
    std::vector updateResources = {
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 0, fb->getColorTexture(0)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 1, fb->getColorTexture(1)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 2, fb->getColorTexture(2)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 3, fb->getColorTexture(3)->getImageView(), sampler.get()),
    };
    _render->getDescriptorHelper()->updateDescriptorSets(updateResources);

    cmdBuf->bindPipeline(_lightPipeline.get());
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH));
    cmdBuf->setScissor(0, 0, vpW, vpH);
    std::vector Dss = {
        _frameAndLightDS,
        _lightTexturesDS,
        skyboxDS,
    };
    cmdBuf->bindDescriptorSets(_lightPPL.get(), 0, Dss);

    PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad)->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

void DeferredRenderPipeline::executeSkybox(const TickDesc& desc)
{
    if (!_skyboxSystem || !_skyboxSystem->bEnabled) return;

    auto           cmdBuf = desc.cmdBuf;
    const uint32_t vpW    = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH    = static_cast<uint32_t>(desc.viewportRect.extent.y);

    cmdBuf->debugBeginLabel("Deferred Skybox");
    FrameContext skyboxCtx{
        .view       = desc.view,
        .projection = desc.projection,
        .cameraPos  = desc.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _skyboxSystem->tick(cmdBuf, desc.dt, &skyboxCtx);
    cmdBuf->debugEndLabel();
}

void DeferredRenderPipeline::executeForwardOverlay(const TickDesc& desc)
{
    if (!_simpleMaterialSystem) return;

    auto           cmdBuf = desc.cmdBuf;
    const uint32_t vpW    = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH    = static_cast<uint32_t>(desc.viewportRect.extent.y);

    cmdBuf->debugBeginLabel("Forward Overlay");
    FrameContext ctx{
        .view       = desc.view,
        .projection = desc.projection,
        .cameraPos  = desc.cameraPos,
        .extent     = {.width = vpW, .height = vpH},
    };
    _simpleMaterialSystem->tick(cmdBuf, desc.dt, &ctx);
    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// GUI / Viewport / Debug
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::TreeNode("Deferred Pipeline")) return;

    ImGui::Checkbox("GBuffer Reverse Viewport Y", &_bReverseViewportY);
    ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");

    if (_pbrGBufferPipeline) _pbrGBufferPipeline->renderGUI();
    if (_phongGBufferPipeline) _phongGBufferPipeline->renderGUI();
    if (_unlitGBufferPipeline) _unlitGBufferPipeline->renderGUI();
    if (_lightPipeline) _lightPipeline->renderGUI();

    if (_skyboxSystem) _skyboxSystem->renderGUI();
    if (_simpleMaterialSystem) _simpleMaterialSystem->renderGUI();
    _postProcessStage.renderGUI();

    ImGui::TreePop();
}

void DeferredRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    if (!_bViewportPassOpen) {
        return;
    }

    cmdBuf->endRendering(_viewportRI);
    _bViewportPassOpen = false;

    auto* inputTexture = _viewportRT->getCurFrameBuffer()->getColorTexture(0);
    viewportTexture    = _postProcessStage.execute(
        cmdBuf, inputTexture, _lastTickDesc.viewportRect.extent, _lastTickDesc.dt, &_lastTickCtx);
}

void DeferredRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    if (_gBufferRT) _gBufferRT->setExtent(newExtent);
    if (_viewportRT) _viewportRT->setExtent(newExtent);

    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _postProcessStage.onViewportResized(newExtent);
}


} // namespace ya

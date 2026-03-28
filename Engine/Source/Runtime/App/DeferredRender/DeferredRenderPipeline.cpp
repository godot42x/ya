#include "DeferredRenderPipeline.h"

#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Core/TextureFactory.h"
#include "Render/Material/MaterialFactory.h"
#include "Resource/PrimitiveMeshCache.h"
#include "Resource/TextureLibrary.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

namespace ya
{

DeferredRenderPipeline::~DeferredRenderPipeline()
{
    shutdown();
}

// ═══════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::init(const InitDesc& desc)
{
    shutdown();

    _render = desc.render;
    YA_CORE_ASSERT(_render, "DeferredRenderPipeline requires a valid render backend");

    Extent2D extent{static_cast<uint32_t>(desc.windowW), static_cast<uint32_t>(desc.windowH)};

    initRenderTargets(extent);
    initPipelines();
    initDescriptorsAndUBOs();

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
    _postProcessStage.shutdown();
    viewportTexture = nullptr;

    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _cachedAlbedoSpecImageViewHandle = nullptr;

    if (_skyboxSystem) {
        _skyboxSystem->onDestroy();
        _skyboxSystem.reset();
    }

    // Render resources
    _lightUBO.reset();
    _frameUBO.reset();
    _deferredDSP.reset();
    _lightPipeline.reset();
    _lightPPL.reset();
    _lightGBufferDSL.reset();
    _pbrGBufferPipeline.reset();
    _pbrGBufferPPL.reset();
    _phongGBufferPipeline.reset();
    _phongGBufferPPL.reset();
    _pbrParamsDSL.reset();
    _pbrMaterialResourceDSL.reset();
    _phongParamsDSL.reset();
    _phongMaterialResourceDSL.reset();
    _frameAndLightDSL.reset();

    _viewportRT.reset();
    _gBufferRT.reset();
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
                AttachmentDescription{.index = 0, .format = SIGNED_LINEAR_FORMAT, .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal, .usage = EImageUsage::ColorAttachment | EImageUsage::Sampled},
                AttachmentDescription{.index = 1, .format = SIGNED_LINEAR_FORMAT, .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal, .usage = EImageUsage::ColorAttachment | EImageUsage::Sampled},
                AttachmentDescription{.index = 2, .format = LINEAR_FORMAT, .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal, .usage = EImageUsage::ColorAttachment | EImageUsage::Sampled},
                AttachmentDescription{.index = 3, .format = SHADING_MODEL_FORMAT, .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal, .usage = EImageUsage::ColorAttachment | EImageUsage::Sampled},
            },
                 .depthAttach = AttachmentDescription{.index = 4, .format = DEPTH_FORMAT, .stencilLoadOp = EAttachmentLoadOp::Clear, .stencilStoreOp = EAttachmentStoreOp::Store, .initialLayout = EImageLayout::DepthStencilAttachmentOptimal, .finalLayout = EImageLayout::DepthStencilAttachmentOptimal, .usage = EImageUsage::DepthStencilAttachment},
        },
    });

    _viewportRT = createRenderTarget(RenderTargetCreateInfo{
        .label            = "Deferred Viewport RT",
        .bSwapChainTarget = false,
        .extent           = extent,
        .attachments      = {
                 .colorAttach = {AttachmentDescription{
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
            }},
        },
    });
}

void DeferredRenderPipeline::initPipelines()
{
    auto* render = _render;

    // ── Shared Frame+Light DSL (set 0) ──
    _frameAndLightDSL = IDescriptorSetLayout::create(render, {DescriptorSetLayoutDesc{
                                                                 .label    = "Deferred_Frame_And_Light_DSL",
                                                                 .set      = 0,
                                                                 .bindings = {
                                                                     {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                     {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                 },
                                                             }});

    // ── PBR material DSLs ──
    auto pbrDSLs            = IDescriptorSetLayout::create(render, {
                                                            DescriptorSetLayoutDesc{.label = "Deferred_PBR_MatRes_DSL", .set = 1, .bindings = {
                                                                                                                                      {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                                                                      {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                                                                      {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                                                                      {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                                                                      {.binding = 4, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                                                                  }},
                                                            DescriptorSetLayoutDesc{.label = "Deferred_PBR_Params_DSL", .set = 2, .bindings = {
                                                                                                                                      {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                                                                                                                                  }},
                                                        });
    _pbrMaterialResourceDSL = pbrDSLs[0];
    _pbrParamsDSL           = pbrDSLs[1];

    // ── Phong material DSLs ──
    auto phongDSLs = IDescriptorSetLayout::create(
        render,
        {
            DescriptorSetLayoutDesc{
                .label    = "Deferred_Phong_MatRes_DSL",
                .set      = 1,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                    {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                }},
            DescriptorSetLayoutDesc{
                .label    = "Deferred_Phong_Params_DSL",
                .set      = 2,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                },
            },
        });
    _phongMaterialResourceDSL = phongDSLs[0];
    _phongParamsDSL           = phongDSLs[1];

    // ── Light pass GBuffer DSL (4 bindings) ──
    _lightGBufferDSL = IDescriptorSetLayout::create(
        render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_LightPass_GBuffer_DSL",
            .set      = 1,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        }});

    // ── Pipeline layouts ──
    _pbrGBufferPPL = IPipelineLayout::create(render, "Deferred_PBR_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(GBufferPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _pbrMaterialResourceDSL, _pbrParamsDSL});

    _phongGBufferPPL = IPipelineLayout::create(render, "Deferred_Phong_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(GBufferPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _phongMaterialResourceDSL, _phongParamsDSL});

    _lightPPL = IPipelineLayout::create(render, "Deferred_Light_PPL", {PushConstantRange{.offset = 0, .size = sizeof(LightPassPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _lightGBufferDSL});

    // ── Helpers ──
    auto makeColorBlend4RT = []() {
        ColorBlendState cbs;
        for (int i = 0; i < 4; ++i)
            cbs.attachments.push_back(ColorBlendAttachmentState{
                .index          = i,
                .bBlendEnable   = false,
                .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A});
        return cbs;
    };
    auto makeShaderDesc = [&](const char* name) {
        return ShaderDesc{.shaderName = name, .bDeriveFromShader = false, .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}}, .vertexAttributes = _commonVertexAttributes};
    };

    std::vector<EFormat::T> gBufferFormats = {SIGNED_LINEAR_FORMAT, SIGNED_LINEAR_FORMAT, LINEAR_FORMAT, SHADING_MODEL_FORMAT};

    // ── PBR GBuffer pipeline ──
    {
        GraphicsPipelineCreateInfo ci{
            .pipelineRenderingInfo = {.label = "PBR GBuffer Pass", .colorAttachmentFormats = gBufferFormats, .depthAttachmentFormat = DEPTH_FORMAT},
            .pipelineLayout        = _pbrGBufferPPL.get(),
            .shaderDesc            = makeShaderDesc("DeferredRender/Unified_GBufferPass_PBR.slang"),
            .dynamicFeatures       = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
            .primitiveType         = EPrimitiveType::TriangleList,
            .rasterizationState    = {.cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
            .depthStencilState     = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
            .colorBlendState       = makeColorBlend4RT(),
            .viewportState         = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
        };
        _pbrGBufferPipeline = IGraphicsPipeline::create(render);
        YA_CORE_ASSERT(_pbrGBufferPipeline && _pbrGBufferPipeline->recreate(ci), "Failed to create PBR GBuffer pipeline");
    }

    // ── Phong GBuffer pipeline ──
    {
        GraphicsPipelineCreateInfo ci{
            .pipelineRenderingInfo = {.label = "Phong GBuffer Pass", .colorAttachmentFormats = gBufferFormats, .depthAttachmentFormat = DEPTH_FORMAT},
            .pipelineLayout        = _phongGBufferPPL.get(),
            .shaderDesc            = makeShaderDesc("DeferredRender/Unified_GBufferPass_Phong.slang"),
            .dynamicFeatures       = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
            .primitiveType         = EPrimitiveType::TriangleList,
            .rasterizationState    = {.cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
            .depthStencilState     = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
            .colorBlendState       = makeColorBlend4RT(),
            .viewportState         = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
        };
        _phongGBufferPipeline = IGraphicsPipeline::create(render);
        YA_CORE_ASSERT(_phongGBufferPipeline && _phongGBufferPipeline->recreate(ci), "Failed to create Phong GBuffer pipeline");
    }

    // ── Light pass pipeline ──
    {
        GraphicsPipelineCreateInfo ci{
            .pipelineRenderingInfo = {.label = "Deferred Light Pass", .colorAttachmentFormats = {LINEAR_FORMAT}, .depthAttachmentFormat = DEPTH_FORMAT},
            .pipelineLayout        = _lightPPL.get(),
            .shaderDesc            = makeShaderDesc("DeferredRender/Unified_LightPass.slang"),
            .dynamicFeatures       = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
            .primitiveType         = EPrimitiveType::TriangleList,
            .rasterizationState    = {.cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
            .depthStencilState     = {.bDepthTestEnable = false, .bDepthWriteEnable = false},
            .colorBlendState       = {.attachments = {ColorBlendAttachmentState{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
            .viewportState         = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
        };
        _lightPipeline = IGraphicsPipeline::create(render);
        YA_CORE_ASSERT(_lightPipeline && _lightPipeline->recreate(ci), "Failed to create Light pipeline");
    }

    // ── Material pools ──
    {
        const uint32_t tc = static_cast<uint32_t>(_pbrMaterialResourceDSL->getLayoutInfo().bindings.size());
        _pbrMatPool.init(render, _pbrParamsDSL, _pbrMaterialResourceDSL, [tc](uint32_t n) { return std::vector<DescriptorPoolSize>{
                                                                                                {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                                                                                                {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * tc}}; }, 16);
    }
    {
        const uint32_t tc = static_cast<uint32_t>(_phongMaterialResourceDSL->getLayoutInfo().bindings.size());
        _phongMatPool.init(render, _phongParamsDSL, _phongMaterialResourceDSL, [tc](uint32_t n) { return std::vector<DescriptorPoolSize>{
                                                                                                      {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                                                                                                      {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * tc}}; }, 16);
    }
}

void DeferredRenderPipeline::initDescriptorsAndUBOs()
{
    _deferredDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                        .label     = "Deferred_DSP",
                                                        .maxSets   = 2,
                                                        .poolSizes = {
                                                            {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 2},
                                                            {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 4},
                                                        },
                                                    });

    _frameAndLightDS = _deferredDSP->allocateDescriptorSets(_frameAndLightDSL);
    _lightTexturesDS = _deferredDSP->allocateDescriptorSets(_lightGBufferDSL);

    _frameUBO = IBuffer::create(_render, BufferCreateInfo{.label = "Deferred_Frame_UBO", .usage = EBufferUsage::UniformBuffer, .size = sizeof(GBufferPassFrameData), .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent});
    _lightUBO = IBuffer::create(_render, BufferCreateInfo{.label = "Deferred_Light_UBO", .usage = EBufferUsage::UniformBuffer, .size = sizeof(LightPassLightData), .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent});

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

    if (_skyboxSystem) _skyboxSystem->beginFrame();
    _postProcessStage.beginFrame();
    _pbrGBufferPipeline->beginFrame();
    _phongGBufferPipeline->beginFrame();
    _lightPipeline->beginFrame();

    _viewportRT->flushDirty();
    _gBufferRT->flushDirty();

    auto* sceneManager = desc.sceneManager;
    auto* scene        = sceneManager ? sceneManager->getActiveScene() : nullptr;
    if (!scene) {
        desc.cmdBuf->debugEndLabel();
        return;
    }

    prepareMaterials(scene);
    updateUBOs(desc, scene);
    executeGBufferPass(desc, scene);
    executeLightPass(desc);

    desc.cmdBuf->debugEndLabel();
}

void DeferredRenderPipeline::prepareMaterials(Scene* scene)
{
    auto& reg = scene->getRegistry();

    // PBR
    {
        uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PBRMaterial>());
        bool             force    = _pbrMatPool.ensureCapacity(matCount);
        std::vector<int> prepared(matCount, 0);
        for (const auto& [e, mc, tc, pmc] : reg.view<MeshComponent, TransformComponent, PBRMaterialComponent>().each()) {
            PBRMaterial* mat = pmc.getMaterial();
            if (!mat || mat->getIndex() < 0) continue;
            uint32_t idx = static_cast<uint32_t>(mat->getIndex());
            if (prepared[idx]) continue;
            _pbrMatPool.flushDirty(
                mat,
                force,
                [](IBuffer* ubo, PBRMaterial* m) { ubo->writeData(&m->getParams(), sizeof(PBRParamUBO), 0); },
                [this](DescriptorSetHandle ds, PBRMaterial* m) {
                    _render->getDescriptorHelper()->updateDescriptorSets(
                        {
                            IDescriptorSetHelper::writeOneImage(ds, 0, m->getTextureBinding(PBRMaterial::EResource::AlbedoTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 1, m->getTextureBinding(PBRMaterial::EResource::NormalTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 2, m->getTextureBinding(PBRMaterial::EResource::MetallicTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 3, m->getTextureBinding(PBRMaterial::EResource::RoughnessTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 4, m->getTextureBinding(PBRMaterial::EResource::AOTexture)),
                        },
                        {});
                });
            prepared[idx] = 1;
        }
    }

    // Phong
    {
        uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
        bool             force    = _phongMatPool.ensureCapacity(matCount);
        std::vector<int> prepared(matCount, 0);
        for (const auto& [e, mc, tc, pmc] : reg.view<MeshComponent, TransformComponent, PhongMaterialComponent>().each()) {
            PhongMaterial* mat = pmc.getMaterial();
            if (!mat || mat->getIndex() < 0) continue;
            uint32_t idx = static_cast<uint32_t>(mat->getIndex());
            if (prepared[idx]) continue;
            _phongMatPool.flushDirty(
                mat,
                force,
                [](IBuffer* ubo, PhongMaterial* m) {
                    PhongParamUBO params{};
                    const auto&   src = m->getParams().textureParams;
                    for (int i = 0; i < PhongMaterial::EResource::Count; ++i) {
                        params.textures[i].bEnable     = src[i].bEnable;
                        params.textures[i].uvTransform = src[i].uvTransform;
                    }
                    ubo->writeData(&params, sizeof(PhongParamUBO), 0);
                },
                [this](DescriptorSetHandle ds, PhongMaterial* m) {
                    _render->getDescriptorHelper()->updateDescriptorSets(
                        {
                            IDescriptorSetHelper::writeOneImage(ds, 0, m->getTextureBinding(PhongMaterial::EResource::DiffuseTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 1, m->getTextureBinding(PhongMaterial::EResource::SpecularTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 2, m->getTextureBinding(PhongMaterial::EResource::NormalTexture)),
                        },
                        {});
                });
            prepared[idx] = 1;
        }
    }
}

void DeferredRenderPipeline::updateUBOs(const TickDesc& desc, Scene* scene)
{
    auto& reg = scene->getRegistry();

    _lightPassLightData.hasDirLight = false;
    for (const auto& [et, dlc, tc] : reg.view<DirectionalLightComponent, TransformComponent>().each()) {
        _lightPassLightData.dirLight.dir     = tc.getForward();
        _lightPassLightData.dirLight.color   = dlc._color;
        _lightPassLightData.dirLight.ambient = dlc._ambient;
        _lightPassLightData.hasDirLight      = true;
    }
    int pli = 0;
    for (const auto& [et, plc, tc] : reg.view<PointLightComponent, TransformComponent>().each()) {
        _lightPassLightData.pointLights[pli] = {.pos = tc.getPosition(), .color = plc.color, .intensity = plc.intensity};
        ++pli;
    }
    _lightPassLightData.numPointLight      = pli;
    _lightPassLightData.dirLight.shininess = 32;

    _gBufferPassFrameData.viewPos    = desc.cameraPos;
    _gBufferPassFrameData.projMatrix = desc.projection;
    _gBufferPassFrameData.viewMatrix = desc.view;

    _frameUBO->writeData(&_gBufferPassFrameData, sizeof(_gBufferPassFrameData), 0);
    _frameUBO->flush();
    _lightUBO->writeData(&_lightPassLightData, sizeof(_lightPassLightData), 0);
    _lightUBO->flush();
}

void DeferredRenderPipeline::executeGBufferPass(const TickDesc& desc, Scene* scene)
{
    auto&          reg    = scene->getRegistry();
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

    float gbVpY = 0.0f, gbVpH = static_cast<float>(vpH);
    if (_bReverseViewportY) {
        gbVpY = static_cast<float>(vpH);
        gbVpH = -gbVpH;
    }
    cmdBuf->setViewport(0.0f, gbVpY, static_cast<float>(vpW), gbVpH);
    cmdBuf->setScissor(0, 0, vpW, vpH);

    // PBR entities
    cmdBuf->bindPipeline(_pbrGBufferPipeline.get());
    for (const auto& [entity, mc, tc, pmc] : reg.view<MeshComponent, TransformComponent, PBRMaterialComponent>().each()) {
        PBRMaterial* mat = pmc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());
        cmdBuf->bindDescriptorSets(_pbrGBufferPPL.get(), 0, {_frameAndLightDS, _pbrMatPool.resourceDS(idx), _pbrMatPool.paramDS(idx)});
        GBufferPushConstant pc{.modelMat = tc.getTransform()};
        cmdBuf->pushConstants(_pbrGBufferPPL.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        mc.getMesh()->draw(cmdBuf);
    }

    // Phong entities
    cmdBuf->bindPipeline(_phongGBufferPipeline.get());
    for (const auto& [entity, mc, tc, pmc] : reg.view<MeshComponent, TransformComponent, PhongMaterialComponent>().each()) {
        PhongMaterial* mat = pmc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());
        cmdBuf->bindDescriptorSets(_phongGBufferPPL.get(), 0, {_frameAndLightDS, _phongMatPool.resourceDS(idx), _phongMatPool.paramDS(idx)});
        GBufferPushConstant pc{.modelMat = tc.getTransform()};
        cmdBuf->pushConstants(_phongGBufferPPL.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        mc.getMesh()->draw(cmdBuf);
    }

    cmdBuf->endRendering(gBufferRI);
}

void DeferredRenderPipeline::executeLightPass(const TickDesc& desc)
{
    auto           cmdBuf = desc.cmdBuf;
    const uint32_t vpW    = static_cast<uint32_t>(desc.viewportRect.extent.x);
    const uint32_t vpH    = static_cast<uint32_t>(desc.viewportRect.extent.y);

    _sharedDepthSpec = RenderingInfo::ImageSpec{
        .texture       = _gBufferRT->getCurFrameBuffer()->getDepthTexture(),
        .loadOp        = EAttachmentLoadOp::Load,
        .storeOp       = EAttachmentStoreOp::DontCare,
        .initialLayout = EImageLayout::Undefined,
        .finalLayout   = EImageLayout::Undefined,
    };

    RenderingInfo lightPassRI{
        .label            = "Light Pass",
        .renderArea       = {.pos = {0, 0}, .extent = _viewportRT->getExtent().toVec2()},
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 0.0f)},
        .colorAttachments = {RenderingInfo::ImageSpec{
            .texture       = _viewportRT->getCurFrameBuffer()->getColorTexture(0),
            .initialLayout = EImageLayout::ColorAttachmentOptimal,
            .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
        }},
        .depthAttachment  = &_sharedDepthSpec,
    };

    cmdBuf->beginRendering(lightPassRI);

    auto sampler = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 0, _gBufferRT->getCurFrameBuffer()->getColorTexture(0)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 1, _gBufferRT->getCurFrameBuffer()->getColorTexture(1)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 2, _gBufferRT->getCurFrameBuffer()->getColorTexture(2)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_lightTexturesDS, 3, _gBufferRT->getCurFrameBuffer()->getColorTexture(3)->getImageView(), sampler.get()),
    });

    cmdBuf->bindPipeline(_lightPipeline.get());
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH));
    cmdBuf->setScissor(0, 0, vpW, vpH);
    cmdBuf->bindDescriptorSets(_lightPPL.get(), 0, {_frameAndLightDS, _lightTexturesDS});

    auto quad = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad);
    quad->draw(cmdBuf);

    // Skybox
    if (_skyboxSystem && _skyboxSystem->bEnabled) {
        cmdBuf->debugBeginLabel("Deferred Skybox");
        FrameContext skyboxCtx;
        skyboxCtx.view       = desc.view;
        skyboxCtx.projection = desc.projection;
        skyboxCtx.cameraPos  = desc.cameraPos;
        skyboxCtx.extent     = Extent2D{.width = vpW, .height = vpH};
        _skyboxSystem->tick(desc.cmdBuf, desc.dt, &skyboxCtx);
        cmdBuf->debugEndLabel();
    }

    _viewportRI   = lightPassRI;
    _lastTickCtx  = {.view = desc.view, .projection = desc.projection, .cameraPos = desc.cameraPos, .extent = {.width = vpW, .height = vpH}};
    _lastTickDesc = desc;
}

// ═══════════════════════════════════════════════════════════════════════
// GUI / Viewport / Debug
// ═══════════════════════════════════════════════════════════════════════

void DeferredRenderPipeline::renderGUI()
{
    if (!ImGui::TreeNode("Deferred Pipeline")) return;

    ImGui::Checkbox("Reverse Viewport Y", &_bReverseViewportY);
    ImGui::TextUnformatted("GBuffer ID + switch/case Light Pass");

    if (_pbrGBufferPipeline) _pbrGBufferPipeline->renderGUI();
    if (_phongGBufferPipeline) _phongGBufferPipeline->renderGUI();
    if (_lightPipeline) _lightPipeline->renderGUI();

    if (_skyboxSystem) _skyboxSystem->renderGUI();
    _postProcessStage.renderGUI();

    ImGui::TreePop();
}

void DeferredRenderPipeline::endViewportPass(ICommandBuffer* cmdBuf)
{
    cmdBuf->endRendering(_viewportRI);

    auto* inputTexture = _viewportRT->getCurFrameBuffer()->getColorTexture(0);
    viewportTexture    = _postProcessStage.execute(
        cmdBuf, inputTexture, _lastTickDesc.viewportRect.extent, _lastTickDesc.dt, &_lastTickCtx);
}

void DeferredRenderPipeline::onViewportResized(Rect2D rect)
{
    Extent2D newExtent{static_cast<uint32_t>(rect.extent.x), static_cast<uint32_t>(rect.extent.y)};
    if (_gBufferRT) _gBufferRT->setExtent(newExtent);
    if (_viewportRT) _viewportRT->setExtent(newExtent);
    _cachedAlbedoSpecImageViewHandle = nullptr;
    _debugAlbedoRGBView.reset();
    _debugSpecularAlphaView.reset();
    _postProcessStage.onViewportResized(newExtent);
}

void DeferredRenderPipeline::ensureDebugSwizzledViews()
{
    auto gBuf2 = _gBufferRT ? _gBufferRT->getCurFrameBuffer()->getColorTexture(2) : nullptr;
    if (!gBuf2) return;
    auto* iv = gBuf2->getImageView();
    if (!iv) return;
    if (_cachedAlbedoSpecImageViewHandle == iv->getHandle() && _debugAlbedoRGBView && _debugSpecularAlphaView) return;

    auto* factory = ITextureFactory::get();
    if (!factory) return;

    auto image = gBuf2->getImageShared();

    _debugAlbedoRGBView = factory->createImageView(image, ImageViewCreateInfo{.label = "GBuffer AlbedoSpec RGB View", .viewType = EImageViewType::View2D, .aspectFlags = EImageAspect::Color, .components = ComponentMapping::RGBOnly()});
    if (_debugAlbedoRGBView) _debugAlbedoRGBView->setDebugName("GBuffer_AlbedoSpec_RGB");

    _debugSpecularAlphaView = factory->createImageView(image, ImageViewCreateInfo{.label = "GBuffer AlbedoSpec Alpha View", .viewType = EImageViewType::View2D, .aspectFlags = EImageAspect::Color, .components = ComponentMapping::AlphaToGrayscale()});
    if (_debugSpecularAlphaView) _debugSpecularAlphaView->setDebugName("GBuffer_AlbedoSpec_Alpha");

    _cachedAlbedoSpecImageViewHandle = iv->getHandle();
    YA_CORE_INFO("Created debug swizzled image views for GBuffer albedoSpecular");
}

} // namespace ya

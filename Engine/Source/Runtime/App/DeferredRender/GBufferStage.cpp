#include "GBufferStage.h"

#include "Render/Material/MaterialFactory.h"
#include "Render/RenderFrameData.h"
#include "Resource/TextureLibrary.h"

#include "DeferredRender.Unified_LightPass.slang.h"

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════

void GBufferStage::init(IRender* render)
{
    _render = render;
    initSharedResources();
    initPBR();
    initPhong();
    initUnlit();
    initFallbackMaterial();
}

void GBufferStage::initSharedResources()
{
    // Frame + Light DSL (set 0, shared by all GBuffer pipelines and LightStage)
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

    // Descriptor pool for per-flight frame+light DS
    _frameAndLightDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "GBufferStage_FrameLight_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {
                {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 2},
            },
        });

    // Create per-flight UBOs and descriptor sets
    using LightPassLightData = slang_types::DeferredRender::Unified_LightPass::LightData;

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _frameUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                    .label       = std::format("GBuffer_Frame_UBO_{}", i),
                                                    .usage       = EBufferUsage::UniformBuffer,
                                                    .size        = sizeof(PBRFrameData),
                                                    .memoryUsage = EMemoryUsage::CpuToGpu,
                                                });

        _lightUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                    .label       = std::format("GBuffer_Light_UBO_{}", i),
                                                    .usage       = EBufferUsage::UniformBuffer,
                                                    .size        = sizeof(LightPassLightData),
                                                    .memoryUsage = EMemoryUsage::CpuToGpu,
                                                });

        _frameAndLightDS[i] = _frameAndLightDSP->allocateDescriptorSets(_frameAndLightDSL);

        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS[i], 0, _frameUBO[i].get()),
            IDescriptorSetHelper::writeOneUniformBuffer(_frameAndLightDS[i], 1, _lightUBO[i].get()),
        });
    }
}

void GBufferStage::initPBR()
{
    auto dsls                = IDescriptorSetLayout::create(_render, {
                                                                         DescriptorSetLayoutDesc{
                                                                             .label    = "Deferred_PBR_MatRes_DSL",
                                                                             .set      = 1,
                                                                             .bindings = {
                                                                                 {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                 {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                 {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                 {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                 {.binding = 4, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                             },
                                                                         },
                                                                         DescriptorSetLayoutDesc{
                                                                             .label    = "Deferred_PBR_Params_DSL",
                                                                             .set      = 2,
                                                                             .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
                                                                         },
                                                                     });
    _pbr.materialResourceDSL = dsls[0];
    _pbr.materialParamsDSL   = dsls[1];

    _pbr.pipelineLayout = IPipelineLayout::create(
        _render, "Deferred_PBR_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PBRPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _pbr.materialResourceDSL, _pbr.materialParamsDSL});

    std::vector<EFormat::T> gBufferFormats = {SIGNED_LINEAR_FORMAT, SIGNED_LINEAR_FORMAT, LINEAR_FORMAT, SHADING_MODEL_FORMAT};

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {.label = "PBR GBuffer Pass", .colorAttachmentFormats = gBufferFormats, .depthAttachmentFormat = DEPTH_FORMAT},
        .pipelineLayout        = _pbr.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "DeferredRender/Unified_GBufferPass_PBR.slang",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = _commonVertexAttributes,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = ColorBlendState{.attachments = {
                                                  {.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 1, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 2, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 3, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                              }},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _pbr.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pbr.pipeline && _pbr.pipeline->recreate(ci), "Failed to create PBR GBuffer pipeline");

    const uint32_t texCount = static_cast<uint32_t>(_pbr.materialResourceDSL->getLayoutInfo().bindings.size());
    _pbrMatPool.init(_render, _pbr.materialParamsDSL, _pbr.materialResourceDSL, [texCount](uint32_t n)
                     { return std::vector<DescriptorPoolSize>{
                           {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                           {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
                       }; },
                     16);
}

void GBufferStage::initPhong()
{
    auto dsls                  = IDescriptorSetLayout::create(_render, {
                                                                           DescriptorSetLayoutDesc{
                                                                               .label    = "Deferred_Phong_MatRes_DSL",
                                                                               .set      = 1,
                                                                               .bindings = {
                                                                                   {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                   {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                   {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                   {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                               },
                                                                           },
                                                                           DescriptorSetLayoutDesc{
                                                                               .label    = "Deferred_Phong_Params_DSL",
                                                                               .set      = 2,
                                                                               .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
                                                                           },
                                                                       });
    _phong.materialResourceDSL = dsls[0];
    _phong.materialParamsDSL   = dsls[1];

    _phong.pipelineLayout = IPipelineLayout::create(
        _render, "Deferred_Phong_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PhongPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _phong.materialResourceDSL, _phong.materialParamsDSL});

    std::vector<EFormat::T>    gBufferFormats = {SIGNED_LINEAR_FORMAT, SIGNED_LINEAR_FORMAT, LINEAR_FORMAT, SHADING_MODEL_FORMAT};
    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {.label = "Phong GBuffer Pass", .colorAttachmentFormats = gBufferFormats, .depthAttachmentFormat = DEPTH_FORMAT},
        .pipelineLayout        = _phong.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "DeferredRender/Unified_GBufferPass_Phong.slang",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = _commonVertexAttributes,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = ColorBlendState{.attachments = {
                                                  {.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 1, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 2, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 3, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                              }},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _phong.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_phong.pipeline && _phong.pipeline->recreate(ci), "Failed to create Phong GBuffer pipeline");

    const uint32_t texCount = static_cast<uint32_t>(_phong.materialResourceDSL->getLayoutInfo().bindings.size());
    _phongMatPool.init(_render, _phong.materialParamsDSL, _phong.materialResourceDSL, [texCount](uint32_t n)
                       { return std::vector<DescriptorPoolSize>{
                             {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                             {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
                         }; },
                       16);
}

void GBufferStage::initUnlit()
{
    auto dsls                  = IDescriptorSetLayout::create(_render, {
                                                                           DescriptorSetLayoutDesc{
                                                                               .label    = "Deferred_Unlit_MatRes_DSL",
                                                                               .set      = 1,
                                                                               .bindings = {
                                                                                   {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                                   {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                                                                               },
                                                                           },
                                                                           DescriptorSetLayoutDesc{
                                                                               .label    = "Deferred_Unlit_Params_DSL",
                                                                               .set      = 2,
                                                                               .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
                                                                           },
                                                                       });
    _unlit.materialResourceDSL = dsls[0];
    _unlit.materialParamsDSL   = dsls[1];

    _unlit.pipelineLayout = IPipelineLayout::create(
        _render, "Deferred_Unlit_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(UnlitPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _unlit.materialResourceDSL, _unlit.materialParamsDSL});

    std::vector<EFormat::T>    gBufferFormats = {SIGNED_LINEAR_FORMAT, SIGNED_LINEAR_FORMAT, LINEAR_FORMAT, SHADING_MODEL_FORMAT};
    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {.label = "Unlit GBuffer Pass", .colorAttachmentFormats = gBufferFormats, .depthAttachmentFormat = DEPTH_FORMAT},
        .pipelineLayout        = _unlit.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "DeferredRender/Unified_GBufferPass_Unlit.slang",
            .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
            .vertexAttributes  = _commonVertexAttributes,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = ColorBlendState{.attachments = {
                                                  {.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 1, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 2, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                                  {.index = 3, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                                              }},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _unlit.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_unlit.pipeline && _unlit.pipeline->recreate(ci), "Failed to create Unlit GBuffer pipeline");

    const uint32_t texCount = static_cast<uint32_t>(_unlit.materialResourceDSL->getLayoutInfo().bindings.size());
    _unlitMatPool.init(_render, _unlit.materialParamsDSL, _unlit.materialResourceDSL, [texCount](uint32_t n)
                       { return std::vector<DescriptorPoolSize>{
                             {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                             {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
                         }; },
                       16);
}

void GBufferStage::initFallbackMaterial()
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

void GBufferStage::destroy()
{
    _pbrMatPool   = {};
    _phongMatPool = {};
    _unlitMatPool = {};

    _pbr   = {};
    _phong = {};
    _unlit = {};

    for (auto& ubo : _frameUBO) ubo.reset();
    for (auto& ubo : _lightUBO) ubo.reset();
    _frameAndLightDSP.reset();
    _frameAndLightDSL.reset();
    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Prepare (per-frame)
// ═══════════════════════════════════════════════════════════════════════

void GBufferStage::prepare(const RenderStageContext& ctx)
{
    if (_pbr.pipeline) {
        _pbr.pipeline->beginFrame();
    }
    if (_phong.pipeline) {
        _phong.pipeline->beginFrame();
    }
    if (_unlit.pipeline) {
        _unlit.pipeline->beginFrame();
    }

    if (!ctx.frameData) return;
    updateFrameUBOs(ctx);
    preparePBR(*ctx.frameData);
    preparePhong(*ctx.frameData);
    prepareUnlit(*ctx.frameData);
}

void GBufferStage::updateFrameUBOs(const RenderStageContext& ctx)
{
    using LightPassLightData = slang_types::DeferredRender::Unified_LightPass::LightData;

    const auto& fd = *ctx.frameData;
    uint32_t    fi = ctx.flightIndex;

    // Frame UBO
    _frameData.viewPos    = fd.cameraPos;
    _frameData.projMatrix = fd.projection;
    _frameData.viewMatrix = fd.view;
    _frameUBO[fi]->writeData(&_frameData, sizeof(_frameData), 0);
    _frameUBO[fi]->flush();

    // Light UBO
    LightPassLightData lightData{};
    lightData.hasDirLight = false;
    if (fd.bHasDirectionalLight) {
        lightData.dirLight.dir   = fd.directionalLight.direction;
        lightData.dirLight.color = fd.directionalLight.diffuse;
        lightData.ambient        = glm::vec3(0.03);
        lightData.hasDirLight    = true;
    }
    int pli = 0;
    for (uint32_t i = 0; i < fd.numPointLights && pli < static_cast<int>(MAX_POINT_LIGHTS); ++i) {
        const auto& src            = fd.pointLights[i];
        lightData.pointLights[pli] = {
            .pos       = src.position,
            .color     = src.diffuse,
            .intensity = 1.0f,
        };
        ++pli;
    }
    lightData.numPointLight = pli;
    _lightUBO[fi]->writeData(&lightData, sizeof(lightData), 0);
    _lightUBO[fi]->flush();
}

void GBufferStage::preparePBR(const RenderFrameData& frameData)
{
    uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PBRMaterial>());
    bool             force    = _pbrMatPool.ensureCapacity(matCount);
    std::vector<int> prepared(matCount, 0);

    for (const auto& item : frameData.pbrDrawItems) {
        auto* mat = static_cast<PBRMaterial*>(item.material);
        if (!mat || mat->getIndex() < 0) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());
        if (prepared[idx]) continue;

        _pbrMatPool.flushDirty(mat, force, [](IBuffer* ubo, PBRMaterial* m)
                               { ubo->writeData(&m->getParams(), sizeof(PBRParamUBO), 0); },
                               [this](DescriptorSetHandle ds, PBRMaterial* m)
                               { _render->getDescriptorHelper()->updateDescriptorSets({
                                                                                          IDescriptorSetHelper::writeOneImage(ds, 0, m->getTextureBinding(PBRMaterial::EResource::AlbedoTexture)),
                                                                                          IDescriptorSetHelper::writeOneImage(ds, 1, m->getTextureBinding(PBRMaterial::EResource::NormalTexture)),
                                                                                          IDescriptorSetHelper::writeOneImage(ds, 2, m->getTextureBinding(PBRMaterial::EResource::MetallicTexture)),
                                                                                          IDescriptorSetHelper::writeOneImage(ds, 3, m->getTextureBinding(PBRMaterial::EResource::RoughnessTexture)),
                                                                                          IDescriptorSetHelper::writeOneImage(ds, 4, m->getTextureBinding(PBRMaterial::EResource::AOTexture)),
                                                                                      },
                                                                                      {}); });
        prepared[idx] = 1;
    }
}

void GBufferStage::preparePhong(const RenderFrameData& frameData)
{
    uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool             force    = _phongMatPool.ensureCapacity(matCount);
    std::vector<int> prepared(matCount, 0);

    for (const auto& item : frameData.phongDrawItems) {
        auto* mat = static_cast<PhongMaterial*>(item.material);
        if (!mat || mat->getIndex() < 0) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());
        if (prepared[idx]) continue;

        _phongMatPool.flushDirty(mat, force, [](IBuffer* ubo, PhongMaterial* m)
                                 {
                PhongParamUBO params{};
                const auto& srcParams = m->getParams();
                params.ambient   = srcParams.ambient;
                params.diffuse   = srcParams.diffuse;
                params.specular  = srcParams.specular;
                params.shininess = srcParams.shininess;
                const auto& src  = m->getParams().textureParams;
                for (int i = 0; i < PhongMaterial::EResource::Count; ++i) {
                    params.textures[i].bEnable     = src[i].bEnable;
                    params.textures[i].uvTransform = src[i].uvTransform;
                }
                ubo->writeData(&params, sizeof(PhongParamUBO), 0); },
                                 [this](DescriptorSetHandle ds, PhongMaterial* m)
                                 { _render->getDescriptorHelper()->updateDescriptorSets({
                                                                                            IDescriptorSetHelper::writeOneImage(ds, 0, m->getTextureBinding(PhongMaterial::EResource::DiffuseTexture)),
                                                                                            IDescriptorSetHelper::writeOneImage(ds, 1, m->getTextureBinding(PhongMaterial::EResource::SpecularTexture)),
                                                                                            IDescriptorSetHelper::writeOneImage(ds, 2, m->getTextureBinding(PhongMaterial::EResource::ReflectionTexture)),
                                                                                            IDescriptorSetHelper::writeOneImage(ds, 3, m->getTextureBinding(PhongMaterial::EResource::NormalTexture)),
                                                                                        },
                                                                                        {}); });
        prepared[idx] = 1;
    }
}

void GBufferStage::prepareUnlit(const RenderFrameData& frameData)
{
    uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<UnlitMaterial>());
    bool             force    = _unlitMatPool.ensureCapacity(matCount);
    std::vector<int> prepared(matCount, 0);

    auto flushOne = [&](UnlitMaterial* mat)
    {
        if (!mat || mat->getIndex() < 0) return;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());
        if (prepared[idx]) return;

        _unlitMatPool.flushDirty(mat, force, [](IBuffer* ubo, UnlitMaterial* m)
                                 { ubo->writeData(&m->getParams(), sizeof(UnlitParamUBO), 0); },
                                 [this](DescriptorSetHandle ds, UnlitMaterial* m)
                                 { _render->getDescriptorHelper()->updateDescriptorSets({
                                                                                            IDescriptorSetHelper::writeOneImage(ds, 0, m->getTextureBinding(UnlitMaterial::EResource::BaseColor0)),
                                                                                            IDescriptorSetHelper::writeOneImage(ds, 1, m->getTextureBinding(UnlitMaterial::EResource::BaseColor1)),
                                                                                        },
                                                                                        {}); });
        prepared[idx] = 1;
    };

    for (const auto& item : frameData.unlitDrawItems)
        flushOne(static_cast<UnlitMaterial*>(item.material));
    flushOne(_fallbackMaterial);
}

// ═══════════════════════════════════════════════════════════════════════
// Execute (draw)
// ═══════════════════════════════════════════════════════════════════════

void GBufferStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.frameData || !ctx.cmdBuf) return;

    ctx.cmdBuf->debugBeginLabel("GBufferStage");
    drawPBR(ctx);
    drawPhong(ctx);
    drawUnlit(ctx);
    drawFallback(ctx);
    ctx.cmdBuf->debugEndLabel();
}

void GBufferStage::drawPBR(const RenderStageContext& ctx)
{
    const auto& items = ctx.frameData->pbrDrawItems;
    if (items.empty()) return;

    auto* cmdBuf = ctx.cmdBuf;
    auto  ds0    = _frameAndLightDS[ctx.flightIndex];

    cmdBuf->bindPipeline(_pbr.pipeline.get());
    for (const auto& item : items) {
        if (!item.mesh || !item.material) continue;
        cmdBuf->bindDescriptorSets(_pbr.pipelineLayout.get(), 0, {ds0, _pbrMatPool.resourceDS(item.materialIndex), _pbrMatPool.paramDS(item.materialIndex)});
        PBRPushConstant pc{.modelMat = item.worldMatrix};
        cmdBuf->pushConstants(_pbr.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        item.mesh->draw(cmdBuf);
    }
}

void GBufferStage::drawPhong(const RenderStageContext& ctx)
{
    const auto& items = ctx.frameData->phongDrawItems;
    if (items.empty()) return;

    auto* cmdBuf = ctx.cmdBuf;
    auto  ds0    = _frameAndLightDS[ctx.flightIndex];

    cmdBuf->bindPipeline(_phong.pipeline.get());
    for (const auto& item : items) {
        if (!item.mesh || !item.material) continue;
        cmdBuf->bindDescriptorSets(_phong.pipelineLayout.get(), 0, {ds0, _phongMatPool.resourceDS(item.materialIndex), _phongMatPool.paramDS(item.materialIndex)});
        PhongPushConstant pc{.modelMat = item.worldMatrix};
        cmdBuf->pushConstants(_phong.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        item.mesh->draw(cmdBuf);
    }
}

void GBufferStage::drawUnlit(const RenderStageContext& ctx)
{
    const auto& items = ctx.frameData->unlitDrawItems;
    if (items.empty()) return;

    auto* cmdBuf = ctx.cmdBuf;
    auto  ds0    = _frameAndLightDS[ctx.flightIndex];

    cmdBuf->bindPipeline(_unlit.pipeline.get());
    for (const auto& item : items) {
        if (!item.mesh || !item.material) continue;
        cmdBuf->bindDescriptorSets(_unlit.pipelineLayout.get(), 0, {ds0, _unlitMatPool.resourceDS(item.materialIndex), _unlitMatPool.paramDS(item.materialIndex)});
        UnlitPushConstant pc{.modelMat = item.worldMatrix};
        cmdBuf->pushConstants(_unlit.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        item.mesh->draw(cmdBuf);
    }
}

void GBufferStage::drawFallback(const RenderStageContext& ctx)
{
    const auto& items = ctx.frameData->fallbackDrawItems;
    if (!_fallbackMaterial || _fallbackMaterial->getIndex() < 0 || items.empty()) return;

    auto*    cmdBuf = ctx.cmdBuf;
    auto     ds0    = _frameAndLightDS[ctx.flightIndex];
    uint32_t fbIdx  = static_cast<uint32_t>(_fallbackMaterial->getIndex());
    bool     bound  = false;

    for (const auto& item : items) {
        if (!item.mesh) continue;
        if (!bound) {
            cmdBuf->bindPipeline(_unlit.pipeline.get());
            cmdBuf->bindDescriptorSets(_unlit.pipelineLayout.get(), 0, {ds0, _unlitMatPool.resourceDS(fbIdx), _unlitMatPool.paramDS(fbIdx)});
            bound = true;
        }
        UnlitPushConstant pc{.modelMat = item.worldMatrix};
        cmdBuf->pushConstants(_unlit.pipelineLayout.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        item.mesh->draw(cmdBuf);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Debug GUI
// ═══════════════════════════════════════════════════════════════════════

void GBufferStage::renderGUI()
{
    _pbr.pipeline->renderGUI();
    _phong.pipeline->renderGUI();
    _unlit.pipeline->renderGUI();

    // Future: material pool stats, per-pipeline toggle, etc.
}

} // namespace ya

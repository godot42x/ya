#include "GBufferStage.h"

#include "Render/Core/IRenderTarget.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/RenderFrameData.h"
#include "Resource/Texture/TextureLibrary.h"

#include "Core/Math/Geometry.h"


#include "imgui.h"

#include "DeferredRender.Unified_LightPass.slang.h"

#include <algorithm>
#include <vector>

namespace ya
{

namespace
{

void refreshShadingPipelineFormats(IGraphicsPipeline* pipeline, const IRenderTarget* gBufferRT)
{
    if (!pipeline || !gBufferRT) {
        return;
    }

    const auto& colorDescs = gBufferRT->getColorAttachmentDescs();
    const auto& depthDesc  = gBufferRT->getDepthAttachmentDesc();
    if (colorDescs.empty() || !depthDesc.has_value()) {
        return;
    }

    std::vector<EFormat::T> colorFormats;
    colorFormats.reserve(colorDescs.size());
    for (const auto& desc : colorDescs) {
        colorFormats.push_back(desc.format);
    }

    auto ci                                         = pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = std::move(colorFormats);
    ci.pipelineRenderingInfo.depthAttachmentFormat  = depthDesc->format;
    pipeline->updateDesc(std::move(ci));
}

} // namespace

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

void GBufferStage::refreshPipelineFormats(const IRenderTarget* gBufferRT)
{
    refreshShadingPipelineFormats(_pbr.pipeline.get(), gBufferRT);
    refreshShadingPipelineFormats(_pbrSkinned.pipeline.get(), gBufferRT);
    refreshShadingPipelineFormats(_phong.pipeline.get(), gBufferRT);
    refreshShadingPipelineFormats(_phongSkinned.pipeline.get(), gBufferRT);
    refreshShadingPipelineFormats(_unlit.pipeline.get(), gBufferRT);
    refreshShadingPipelineFormats(_unlitSkinned.pipeline.get(), gBufferRT);
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

    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_Skinning_DSL",
            .set      = 3,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
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
    auto dsls = IDescriptorSetLayout::create(
        _render,
        {
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
        .colorBlendState    = ColorBlendState{
            .attachments = {
                {.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                {.index = 1, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                {.index = 2, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                {.index = 3, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
            },
        },
        .viewportState = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _pbr.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pbr.pipeline && _pbr.pipeline->recreate(ci), "Failed to create PBR GBuffer pipeline");

    auto skinnedCI                         = ci;
    skinnedCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    skinnedCI.shaderDesc.vertexAttributes = _commonVertexAttributes;
    skinnedCI.shaderDesc.vertexAttributes.push_back({.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)});
    skinnedCI.shaderDesc.vertexAttributes.push_back({.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)});
    skinnedCI.shaderDesc.defines = {"ENABLE_SKINNING 1"};

    _pbrSkinned.materialResourceDSL = _pbr.materialResourceDSL;
    _pbrSkinned.materialParamsDSL   = _pbr.materialParamsDSL;
    _pbrSkinned.pipelineLayout      = IPipelineLayout::create(
        _render,
        "Deferred_PBR_GBuffer_Skinned_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PBRPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameAndLightDSL, _pbr.materialResourceDSL, _pbr.materialParamsDSL, _skinningDSL});
    skinnedCI.pipelineLayout = _pbrSkinned.pipelineLayout.get();
    _pbrSkinned.pipeline     = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pbrSkinned.pipeline && _pbrSkinned.pipeline->recreate(skinnedCI), "Failed to create skinned PBR GBuffer pipeline");

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
    auto dsls = IDescriptorSetLayout::create(
        _render,
        {
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
        .colorBlendState    = ColorBlendState{
            .attachments = {
                {.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                {.index = 1, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                {.index = 2, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
                {.index = 3, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A},
            },
        },
        .viewportState = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _phong.pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_phong.pipeline && _phong.pipeline->recreate(ci), "Failed to create Phong GBuffer pipeline");

    auto skinnedCI                         = ci;
    skinnedCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    skinnedCI.shaderDesc.vertexAttributes = _commonVertexAttributes;
    skinnedCI.shaderDesc.vertexAttributes.push_back({.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)});
    skinnedCI.shaderDesc.vertexAttributes.push_back({.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)});
    skinnedCI.shaderDesc.defines = {"ENABLE_SKINNING 1"};

    _phongSkinned.materialResourceDSL = _phong.materialResourceDSL;
    _phongSkinned.materialParamsDSL   = _phong.materialParamsDSL;
    _phongSkinned.pipelineLayout      = IPipelineLayout::create(
        _render,
        "Deferred_Phong_GBuffer_Skinned_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PhongPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameAndLightDSL, _phong.materialResourceDSL, _phong.materialParamsDSL, _skinningDSL});
    skinnedCI.pipelineLayout = _phongSkinned.pipelineLayout.get();
    _phongSkinned.pipeline   = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_phongSkinned.pipeline && _phongSkinned.pipeline->recreate(skinnedCI), "Failed to create skinned Phong GBuffer pipeline");

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
    auto dsls = IDescriptorSetLayout::create(
        _render,
        {
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

    auto skinnedCI                         = ci;
    skinnedCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    skinnedCI.shaderDesc.vertexAttributes = _commonVertexAttributes;
    skinnedCI.shaderDesc.vertexAttributes.push_back({.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)});
    skinnedCI.shaderDesc.vertexAttributes.push_back({.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)});
    skinnedCI.shaderDesc.defines = {"ENABLE_SKINNING 1"};

    _unlitSkinned.materialResourceDSL = _unlit.materialResourceDSL;
    _unlitSkinned.materialParamsDSL   = _unlit.materialParamsDSL;
    _unlitSkinned.pipelineLayout      = IPipelineLayout::create(
        _render,
        "Deferred_Unlit_GBuffer_Skinned_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(UnlitPushConstant), .stageFlags = EShaderStage::Vertex}},
        {_frameAndLightDSL, _unlit.materialResourceDSL, _unlit.materialParamsDSL, _skinningDSL});
    skinnedCI.pipelineLayout = _unlitSkinned.pipelineLayout.get();
    _unlitSkinned.pipeline   = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_unlitSkinned.pipeline && _unlitSkinned.pipeline->recreate(skinnedCI), "Failed to create skinned Unlit GBuffer pipeline");

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

void GBufferStage::ensureSkinningCapacity(uint32_t paletteCount)
{
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (_skinningDSP && requiredCount <= _skinningCapacity) {
        return;
    }

    _skinningDSP.reset();
    for (auto& ds : _skinningDS) {
        ds = {};
    }

    _skinningCapacity = std::max(requiredCount, _skinningCapacity == 0 ? 16u : _skinningCapacity);
    while (_skinningCapacity < requiredCount) {
        _skinningCapacity *= 2;
    }

    _skinningDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "GBufferStage_Skinning_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });

    const uint32_t bufferSize = static_cast<uint32_t>(static_cast<uint64_t>(_skinningCapacity) * sizeof(RenderSkinningPalette));
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skinningSSBO[i] = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label       = std::format("GBuffer_Skinning_SSBO_{}", i),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        _skinningDS[i] = _skinningDSP->allocateDescriptorSets(_skinningDSL);
        _render->getDescriptorHelper()->updateDescriptorSets(
            {IDescriptorSetHelper::genSingleBufferWrite(_skinningDS[i], 0, EPipelineDescriptorType::StorageBuffer, _skinningSSBO[i].get())},
            {});
    }
}

void GBufferStage::updateSkinningBuffer(const RenderStageContext& ctx)
{
    if (!ctx.frameData) {
        return;
    }

    const auto& palettes = ctx.frameData->skinningPalettes;
    ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()));
    if (palettes.empty()) {
        return;
    }

    auto& buffer = _skinningSSBO[ctx.flightIndex];
    buffer->writeData(palettes.data(), palettes.size() * sizeof(RenderSkinningPalette), 0);
    buffer->flush();
}

void GBufferStage::destroy()
{
    _pbrMatPool   = {};
    _phongMatPool = {};
    _unlitMatPool = {};

    _pbr          = {};
    _pbrSkinned   = {};
    _phong        = {};
    _phongSkinned = {};
    _unlit        = {};
    _unlitSkinned = {};

    for (auto& ubo : _frameUBO) ubo.reset();
    for (auto& ubo : _lightUBO) ubo.reset();
    for (auto& ssbo : _skinningSSBO) ssbo.reset();
    _skinningDSP.reset();
    _skinningDSL.reset();
    _skinningCapacity = 0;
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
    if (_pbrSkinned.pipeline) {
        _pbrSkinned.pipeline->beginFrame();
    }
    if (_phong.pipeline) {
        _phong.pipeline->beginFrame();
    }
    if (_phongSkinned.pipeline) {
        _phongSkinned.pipeline->beginFrame();
    }
    if (_unlit.pipeline) {
        _unlit.pipeline->beginFrame();
    }
    if (_unlitSkinned.pipeline) {
        _unlitSkinned.pipeline->beginFrame();
    }

    if (!ctx.frameData) return;
    updateFrameUBOs(ctx);
    updateSkinningBuffer(ctx);
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
    lightData.hasDirLight              = false;
    lightData.dirLight.bias            = _shadowBias;
    lightData.dirLight.normalBias      = _shadowNormalBias;
    lightData.dirLight.shadowFilter    = _shadowFilter;
    lightData.dirLight.shadowTexelSize = _shadowTexelSize;
    if (fd.bHasDirectionalLight) {
        lightData.dirLight.dir          = fd.directionalLight.direction;
        lightData.dirLight.color        = fd.directionalLight.color;
        lightData.dirLight.intensity    = fd.directionalLight.intensity;
        lightData.dirLight.shadowMatrix = fd.directionalLight.viewProjection;
        lightData.hasDirLight           = true;
    }
    int            pli                      = 0;
    const uint32_t shadowedPointLightBudget = std::min(_maxShadowedPointLights, fd.numPointLights);
    for (uint32_t i = 0; i < fd.numPointLights && pli < static_cast<int>(MAX_POINT_LIGHTS); ++i) {
        const auto& src            = fd.pointLights[i];
        lightData.pointLights[pli] = {
            .pos       = src.position,
            .color     = src.color,
            .intensity = src.intensity,
            .farPlane  = static_cast<uint32_t>(pli) < shadowedPointLightBudget ? src.farPlane : 0.0f,
        };
        ++pli;
    }
    _lastShadowedPointLights = std::min(shadowedPointLightBudget, static_cast<uint32_t>(pli));
    lightData.numPointLight  = pli;
    _lightUBO[fi]->writeData(&lightData, sizeof(lightData), 0);
    _lightUBO[fi]->flush();
}

void GBufferStage::preparePBR(const RenderFrameData& frameData)
{
    uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PBRMaterial>());
    bool             force    = _pbrMatPool.ensureCapacity(matCount);
    std::vector<int> prepared(matCount, 0);

    auto prepareBucket = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            auto* mat = static_cast<PBRMaterial*>(item.material);
            if (!mat || mat->getIndex() < 0) continue;
            uint32_t idx = static_cast<uint32_t>(mat->getIndex());
            if (prepared[idx]) continue;

            _pbrMatPool.flushDirty(
                mat,
                force,
                [](IBuffer* ubo, PBRMaterial* m)
                {
                    ubo->writeData(&m->getParams(), sizeof(PBRParamUBO), 0);
                },
                [this](DescriptorSetHandle ds, PBRMaterial* m)
                {
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
    };

    prepareBucket(frameData.drawBuckets.staticMeshes.pbrDrawItems);
    prepareBucket(frameData.drawBuckets.skinnedMeshes.pbrDrawItems);
}

void GBufferStage::preparePhong(const RenderFrameData& frameData)
{
    uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool             force    = _phongMatPool.ensureCapacity(matCount);
    std::vector<int> prepared(matCount, 0);

    auto prepareBucket = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
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
    };

    prepareBucket(frameData.drawBuckets.staticMeshes.phongDrawItems);
    prepareBucket(frameData.drawBuckets.skinnedMeshes.phongDrawItems);
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

        _unlitMatPool.flushDirty(
            mat,
            force,
            [](IBuffer* ubo, UnlitMaterial* m)
            {
                ubo->writeData(&m->getParams(), sizeof(UnlitParamUBO), 0);
            },
            [this](DescriptorSetHandle ds, UnlitMaterial* m)
            {
                _render
                    ->getDescriptorHelper()
                    ->updateDescriptorSets(
                        {
                            IDescriptorSetHelper::writeOneImage(ds, 0, m->getTextureBinding(UnlitMaterial::EResource::BaseColor0)),
                            IDescriptorSetHelper::writeOneImage(ds, 1, m->getTextureBinding(UnlitMaterial::EResource::BaseColor1)),
                        },
                        {});
            });
        prepared[idx] = 1;
    };

    auto flushBucket = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            flushOne(static_cast<UnlitMaterial*>(item.material));
        }
    };

    flushBucket(frameData.drawBuckets.staticMeshes.unlitDrawItems);
    flushBucket(frameData.drawBuckets.skinnedMeshes.unlitDrawItems);
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
    auto* cmdBuf = ctx.cmdBuf;
    auto  ds0    = _frameAndLightDS[ctx.flightIndex];

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        if (items.empty()) return;

        auto* pipeline = bSkinned ? _pbrSkinned.pipeline.get() : _pbr.pipeline.get();
        auto* layout   = bSkinned ? _pbrSkinned.pipelineLayout.get() : _pbr.pipelineLayout.get();
        cmdBuf->bindPipeline(pipeline);
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {ds0, _pbrMatPool.resourceDS(item.materialIndex), _pbrMatPool.paramDS(item.materialIndex), _skinningDS[ctx.flightIndex]});
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {ds0, _pbrMatPool.resourceDS(item.materialIndex), _pbrMatPool.paramDS(item.materialIndex)});
            }

            PBRPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout, EShaderStage::Vertex, 0, sizeof(pc), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(ctx.frameData->drawBuckets.staticMeshes.pbrDrawItems, false);
    drawBucket(ctx.frameData->drawBuckets.skinnedMeshes.pbrDrawItems, true);
}

void GBufferStage::drawPhong(const RenderStageContext& ctx)
{
    auto* cmdBuf     = ctx.cmdBuf;
    auto  ds0        = _frameAndLightDS[ctx.flightIndex];
    auto  drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        if (items.empty()) return;

        auto* pipeline = bSkinned ? _phongSkinned.pipeline.get() : _phong.pipeline.get();
        auto* layout   = bSkinned ? _phongSkinned.pipelineLayout.get() : _phong.pipelineLayout.get();
        cmdBuf->bindPipeline(pipeline);
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;

            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {ds0, _phongMatPool.resourceDS(item.materialIndex), _phongMatPool.paramDS(item.materialIndex), _skinningDS[ctx.flightIndex]});
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {ds0, _phongMatPool.resourceDS(item.materialIndex), _phongMatPool.paramDS(item.materialIndex)});
            }

            PhongPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout, EShaderStage::Vertex, 0, sizeof(pc), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(ctx.frameData->drawBuckets.staticMeshes.phongDrawItems, false);
    drawBucket(ctx.frameData->drawBuckets.skinnedMeshes.phongDrawItems, true);
}

void GBufferStage::drawUnlit(const RenderStageContext& ctx)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  ds0    = _frameAndLightDS[ctx.flightIndex];

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        if (items.empty()) return;

        auto* pipeline = bSkinned ? _unlitSkinned.pipeline.get() : _unlit.pipeline.get();
        auto* layout   = bSkinned ? _unlitSkinned.pipelineLayout.get() : _unlit.pipelineLayout.get();
        cmdBuf->bindPipeline(pipeline);
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {ds0, _unlitMatPool.resourceDS(item.materialIndex), _unlitMatPool.paramDS(item.materialIndex), _skinningDS[ctx.flightIndex]});
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {ds0, _unlitMatPool.resourceDS(item.materialIndex), _unlitMatPool.paramDS(item.materialIndex)});
            }

            UnlitPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout, EShaderStage::Vertex, 0, sizeof(pc), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(ctx.frameData->drawBuckets.staticMeshes.unlitDrawItems, false);
    drawBucket(ctx.frameData->drawBuckets.skinnedMeshes.unlitDrawItems, true);
}

void GBufferStage::drawFallback(const RenderStageContext& ctx)
{
    if (!_fallbackMaterial || _fallbackMaterial->getIndex() < 0) return;

    auto*    cmdBuf = ctx.cmdBuf;
    auto     ds0    = _frameAndLightDS[ctx.flightIndex];
    uint32_t fbIdx  = static_cast<uint32_t>(_fallbackMaterial->getIndex());

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        if (items.empty()) return;

        auto* pipeline = bSkinned ? _unlitSkinned.pipeline.get() : _unlit.pipeline.get();
        auto* layout   = bSkinned ? _unlitSkinned.pipelineLayout.get() : _unlit.pipelineLayout.get();
        cmdBuf->bindPipeline(pipeline);
        if (bSkinned) {
            cmdBuf->bindDescriptorSets(layout, 0, {ds0, _unlitMatPool.resourceDS(fbIdx), _unlitMatPool.paramDS(fbIdx), _skinningDS[ctx.flightIndex]});
        }
        else {
            cmdBuf->bindDescriptorSets(layout, 0, {ds0, _unlitMatPool.resourceDS(fbIdx), _unlitMatPool.paramDS(fbIdx)});
        }
        for (const auto& item : items) {
            if (!item.mesh) continue;
            UnlitPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout, EShaderStage::Vertex, 0, sizeof(pc), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(ctx.frameData->drawBuckets.staticMeshes.fallbackDrawItems, false);
    drawBucket(ctx.frameData->drawBuckets.skinnedMeshes.fallbackDrawItems, true);
}

// ═══════════════════════════════════════════════════════════════════════
// Debug GUI
// ═══════════════════════════════════════════════════════════════════════

void GBufferStage::renderGUI()
{
    if (!ImGui::TreeNode("GBufferState")) {
        return;
    }

    if (ImGui::TreeNode("Stats")) {
        ImGui::Text("Point shadow budget: %u", _maxShadowedPointLights);
        ImGui::Text("Shadowed point lights: %u", _lastShadowedPointLights);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Pipelines")) {
        if (ImGui::TreeNode("PBR")) {
            _pbr.pipeline->renderGUI();
            _pbrSkinned.pipeline->renderGUI();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Phong")) {
            _phong.pipeline->renderGUI();
            _phongSkinned.pipeline->renderGUI();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Unlit")) {
            _unlit.pipeline->renderGUI();
            _unlitSkinned.pipeline->renderGUI();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    // Future: material pool stats, per-pipeline toggle, etc.

    ImGui::TreePop();
}

} // namespace ya

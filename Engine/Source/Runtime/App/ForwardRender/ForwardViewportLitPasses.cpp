#include "ForwardViewportLitPasses.h"

#include "Config/ConfigManager.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Render.h"

namespace ya
{

namespace
{

static const std::vector<VertexAttribute> kLitVertexAttributes4 = {
    {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
    {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
    {.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
};

static const std::vector<VertexAttribute> kLitSkinningVertexAttributes = {
    {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
    {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
};

static const VertexBufferDescription kLitVBDesc{.slot = 0, .pitch = sizeof(ya::Vertex)};

constexpr const char* FORWARD_PBR_CONFIG_DOC_NAME         = "editor";
constexpr const char* FORWARD_PBR_CONFIG_KEY_IBL_DIFFUSE  = "render.deferred.light.enablePBRDiffuseIBL";
constexpr const char* FORWARD_PBR_CONFIG_KEY_IBL_SPECULAR = "render.deferred.light.enablePBRSpecularIBL";

std::vector<std::string> buildPhongShaderDefines(bool bEnableDirectionalShadow)
{
    std::vector<std::string> defines;
    if (bEnableDirectionalShadow) {
        defines.push_back("ENABLE_DIRECTIONAL_SHADOW 1");
    }
    return defines;
}

std::vector<std::string> buildPBRShaderDefines(bool bEnablePBRDiffuseIBL,
                                               bool bEnablePBRSpecularIBL,
                                               bool bEnableShadowMapping,
                                               bool bEnablePointLightShadow)
{
    std::vector<std::string> defines = {
        std::string("YA_DEFERRED_PBR_ENABLE_IBL_DIFFUSE=") + (bEnablePBRDiffuseIBL ? "1" : "0"),
        std::string("YA_DEFERRED_PBR_ENABLE_IBL_SPECULAR=") + (bEnablePBRSpecularIBL ? "1" : "0"),
    };

    if (bEnableShadowMapping) {
        defines.push_back("YA_DEFERRED_ENABLE_SHADOW_MAPPING=1");
    }
    if (bEnablePointLightShadow) {
        defines.push_back("YA_DEFERRED_ENABLE_POINT_LIGHT_SHADOW=1");
    }

    return defines;
}

} // namespace

void ForwardViewportLitPasses::init(const InitDesc& desc)
{
    _render                = desc.render;
    _shadowState           = desc.shadowState;
    _skinningDSL           = desc.skinningDSL;
    _getFrameIndex         = desc.getFrameIndex;
    _getElapsedTimeSeconds = desc.getElapsedTimeSeconds;

    initPBR(desc);
    initPhong(desc);
}

void ForwardViewportLitPasses::destroy()
{
    _pbrMatPool = {};
    _pbrStatic = {};
    _pbrSkinned = {};
    _pbrFrameDSL.reset();
    _pbrResourceDSL.reset();
    _pbrParamDSL.reset();
    _pbrFrameDSP.reset();
    for (auto& u : _pbrFrameUBO) u.reset();
    for (auto& u : _pbrLightUBO) u.reset();

    _phongMatPool = {};
    _phongStatic = {};
    _phongSkinned = {};
    _phongFrameDSL.reset();
    _phongResourceDSL.reset();
    _phongParamDSL.reset();
    _phongFrameDSP.reset();
    for (auto& u : _phongFrameUBO) u.reset();
    for (auto& u : _phongLightUBO) u.reset();
    for (auto& u : _phongDebugUBO) u.reset();

    _getFrameIndex = {};
    _getElapsedTimeSeconds = {};
    _skinningDSL.reset();
    _render = nullptr;
}

void ForwardViewportLitPasses::beginFrame()
{
    if (_pbrStatic.pipeline) {
        _pbrStatic.pipeline->beginFrame();
    }
    if (_pbrSkinned.pipeline) {
        _pbrSkinned.pipeline->beginFrame();
    }
    if (_phongStatic.pipeline) {
        _phongStatic.pipeline->beginFrame();
    }
    if (_phongSkinned.pipeline) {
        _phongSkinned.pipeline->beginFrame();
    }
}

void ForwardViewportLitPasses::refreshPipelineFormats(const RenderAttachmentFormats& formats)
{
    if (!formats.hasColor()) {
        return;
    }

    auto refreshVariant = [&](ShadingPipelineVariant& variant)
    {
        if (!variant.pipeline) {
            return;
        }
        variant.pipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
        variant.pipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
        variant.pipeline->updateDesc(variant.pipelineCI);
    };

    refreshVariant(_pbrStatic);
    refreshVariant(_pbrSkinned);
    refreshVariant(_phongStatic);
    refreshVariant(_phongSkinned);
}

void ForwardViewportLitPasses::prepare(const RenderStageContext& ctx)
{
    if (!ctx.frameData) {
        return;
    }

    preparePBR(ctx);
    preparePhong(ctx);
}

void ForwardViewportLitPasses::initPBR(const InitDesc& desc)
{
    auto& configManager    = ConfigManager::get();
    _bEnablePBRDiffuseIBL  = configManager.getOr<bool>(FORWARD_PBR_CONFIG_DOC_NAME, FORWARD_PBR_CONFIG_KEY_IBL_DIFFUSE, _bEnablePBRDiffuseIBL);
    _bEnablePBRSpecularIBL = configManager.getOr<bool>(FORWARD_PBR_CONFIG_DOC_NAME, FORWARD_PBR_CONFIG_KEY_IBL_SPECULAR, _bEnablePBRSpecularIBL);

    auto dsls = IDescriptorSetLayout::create(
        _render,
        {
            DescriptorSetLayoutDesc{
                .label    = "FwdPBR_Frame_DSL",
                .set      = 0,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPBR_Resource_DSL",
                .set      = 1,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 4, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPBR_Param_DSL",
                .set      = 2,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPBR_Environment_DSL",
                .set      = 3,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPBR_Shadow_DSL",
                .set      = 4,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = MAX_POINT_LIGHTS, .stageFlags = EShaderStage::Fragment},
                },
            },
        });
    _pbrFrameDSL    = dsls[0];
    _pbrResourceDSL = dsls[1];
    _pbrParamDSL    = dsls[2];

    _pbrStatic.pipelineLayout = IPipelineLayout::create(
        _render,
        "FwdPBR_Static_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PBRPushConstant), .stageFlags = EShaderStage::Vertex}},
        dsls);

    _pbrStatic.pipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _pbrStatic.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "PBRForward.slang",
            .vertexBufferDescs = {kLitVBDesc},
            .vertexAttributes  = kLitVertexAttributes4,
            .defines           = buildPBRShaderDefines(_bEnablePBRDiffuseIBL, _bEnablePBRSpecularIBL, _shadowState.bEnableShadowMapping, _shadowState.bEnablePointLightShadow),
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .multisampleState   = {.sampleCount = ESampleCount::Sample_1},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = true,
                                   .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _pbrStatic.pipeline = IGraphicsPipeline::create(_render);
    _pbrStatic.pipeline->recreate(_pbrStatic.pipelineCI);

    auto skinnedDsls = dsls;
    skinnedDsls.push_back(_skinningDSL);
    _pbrSkinned.pipelineLayout = IPipelineLayout::create(
        _render,
        "FwdPBR_Skinned_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PBRPushConstant), .stageFlags = EShaderStage::Vertex}},
        skinnedDsls);

    _pbrSkinned.pipelineCI                         = _pbrStatic.pipelineCI;
    _pbrSkinned.pipelineCI.pipelineLayout          = _pbrSkinned.pipelineLayout.get();
    _pbrSkinned.pipelineCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    _pbrSkinned.pipelineCI.shaderDesc.vertexAttributes = kLitVertexAttributes4;
    _pbrSkinned.pipelineCI.shaderDesc.vertexAttributes.insert(_pbrSkinned.pipelineCI.shaderDesc.vertexAttributes.end(), kLitSkinningVertexAttributes.begin(), kLitSkinningVertexAttributes.end());
    _pbrSkinned.pipelineCI.shaderDesc.defines.push_back("ENABLE_SKINNING 1");
    _pbrSkinned.pipelineCI.shaderDesc.defines.push_back("SKINNING_SET_INDEX 5");
    _pbrSkinned.pipeline = IGraphicsPipeline::create(_render);
    _pbrSkinned.pipeline->recreate(_pbrSkinned.pipelineCI);

    _pbrFrameDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                        .label     = "FwdPBR_Frame_DSP",
                                                        .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                        .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 2}},
                                                    });

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _pbrFrameUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
                                                       .label       = std::format("FwdPBR_Frame_UBO_{}", i),
                                                       .usage       = EBufferUsage::UniformBuffer,
                                                       .size        = sizeof(PBRFrameUBO),
                                                       .memoryUsage = EMemoryUsage::CpuToGpu,
                                                   });
        _pbrLightUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
                                                       .label       = std::format("FwdPBR_Light_UBO_{}", i),
                                                       .usage       = EBufferUsage::UniformBuffer,
                                                       .size        = sizeof(PBRLightUBO),
                                                       .memoryUsage = EMemoryUsage::CpuToGpu,
                                                   });

        _pbrFrameDS[i] = _pbrFrameDSP->allocateDescriptorSets(_pbrFrameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_pbrFrameDS[i], 0, _pbrFrameUBO[i].get()),
            IDescriptorSetHelper::writeOneUniformBuffer(_pbrFrameDS[i], 1, _pbrLightUBO[i].get()),
        });
    }

    constexpr uint32_t pbrTextureCount = 5;
    _pbrMatPool.init(
        _render, _pbrParamDSL, _pbrResourceDSL, [pbrTextureCount](uint32_t n) -> std::vector<DescriptorPoolSize>
        { return {
              {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
              {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * pbrTextureCount},
          }; },
        16);
    _pbrPoolRecreated = true;
}

void ForwardViewportLitPasses::initPhong(const InitDesc& desc)
{
    auto dsls = IDescriptorSetLayout::create(
        _render,
        {
            DescriptorSetLayoutDesc{
                .label    = "FwdPhong_Frame_DSL",
                .set      = 0,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                    {.binding = 2, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPhong_Resource_DSL",
                .set      = 1,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPhong_Param_DSL",
                .set      = 2,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPhong_Skybox_DSL",
                .set      = 3,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                },
            },
            DescriptorSetLayoutDesc{
                .label    = "FwdPhong_Shadow_DSL",
                .set      = 4,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = MAX_POINT_LIGHTS, .stageFlags = EShaderStage::Fragment},
                },
            },
        });
    _phongFrameDSL    = dsls[0];
    _phongResourceDSL = dsls[1];
    _phongParamDSL    = dsls[2];

    _phongStatic.pipelineLayout = IPipelineLayout::create(
        _render, "FwdPhong_Static_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PhongModelPC), .stageFlags = EShaderStage::Vertex}}, dsls);

    _phongStatic.pipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _phongStatic.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "PhongLit.slang",
            .vertexBufferDescs = {kLitVBDesc},
            .vertexAttributes  = kLitVertexAttributes4,
            .defines           = buildPhongShaderDefines(_shadowState.bEnableShadowMapping),
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .multisampleState   = {.sampleCount = ESampleCount::Sample_1},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = true,
                                   .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstAlphaBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _phongStatic.pipeline = IGraphicsPipeline::create(_render);
    _phongStatic.pipeline->recreate(_phongStatic.pipelineCI);

    auto skinnedDsls = dsls;
    skinnedDsls.push_back(_skinningDSL);
    _phongSkinned.pipelineLayout = IPipelineLayout::create(
        _render, "FwdPhong_Skinned_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PhongModelPC), .stageFlags = EShaderStage::Vertex}}, skinnedDsls);

    _phongSkinned.pipelineCI                         = _phongStatic.pipelineCI;
    _phongSkinned.pipelineCI.pipelineLayout          = _phongSkinned.pipelineLayout.get();
    _phongSkinned.pipelineCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    _phongSkinned.pipelineCI.shaderDesc.vertexAttributes = kLitVertexAttributes4;
    _phongSkinned.pipelineCI.shaderDesc.vertexAttributes.insert(_phongSkinned.pipelineCI.shaderDesc.vertexAttributes.end(), kLitSkinningVertexAttributes.begin(), kLitSkinningVertexAttributes.end());
    _phongSkinned.pipelineCI.shaderDesc.defines.push_back("ENABLE_SKINNING 1");
    _phongSkinned.pipelineCI.shaderDesc.defines.push_back("SKINNING_SET_INDEX 5");
    _phongSkinned.pipeline = IGraphicsPipeline::create(_render);
    _phongSkinned.pipeline->recreate(_phongSkinned.pipelineCI);

    _phongFrameDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                          .label     = "FwdPhong_Frame_DSP",
                                                          .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                          .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 3}},
                                                      });

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _phongFrameUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
                                                         .label       = std::format("FwdPhong_Frame_UBO_{}", i),
                                                         .usage       = EBufferUsage::UniformBuffer,
                                                         .size        = sizeof(PhongFrameUBO),
                                                         .memoryUsage = EMemoryUsage::CpuToGpu,
                                                     });
        _phongLightUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
                                                         .label       = std::format("FwdPhong_Light_UBO_{}", i),
                                                         .usage       = EBufferUsage::UniformBuffer,
                                                         .size        = sizeof(PhongLightUBO),
                                                         .memoryUsage = EMemoryUsage::CpuToGpu,
                                                     });
        _phongDebugUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
                                                         .label       = std::format("FwdPhong_Debug_UBO_{}", i),
                                                         .usage       = EBufferUsage::UniformBuffer,
                                                         .size        = sizeof(PhongDebugUBO),
                                                         .memoryUsage = EMemoryUsage::CpuToGpu,
                                                     });

        _phongFrameDS[i] = _phongFrameDSP->allocateDescriptorSets(_phongFrameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_phongFrameDS[i], 0, _phongFrameUBO[i].get()),
            IDescriptorSetHelper::writeOneUniformBuffer(_phongFrameDS[i], 1, _phongLightUBO[i].get()),
            IDescriptorSetHelper::writeOneUniformBuffer(_phongFrameDS[i], 2, _phongDebugUBO[i].get()),
        });
    }

    constexpr uint32_t phongTextureCount = 4;
    _phongMatPool.init(
        _render, _phongParamDSL, _phongResourceDSL, [phongTextureCount](uint32_t n) -> std::vector<DescriptorPoolSize>
        { return {
              {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
              {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * phongTextureCount},
          }; },
        16);
    _phongPoolRecreated = true;
    _phongDebug = {};
}

void ForwardViewportLitPasses::preparePBR(const RenderStageContext& ctx)
{
    const auto& fd = *ctx.frameData;
    uint32_t    fi = ctx.flightIndex;

    uint32_t materialCount = MaterialFactory::get()->getMaterialSize<PBRMaterial>();
    if (_pbrMatPool.ensureCapacity(materialCount)) {
        _pbrPoolRecreated = true;
    }

    PBRFrameUBO frameUBO{};
    frameUBO.projMat   = fd.projection;
    frameUBO.viewMat   = fd.view;
    frameUBO.cameraPos = fd.cameraPos;
    _pbrFrameUBO[fi]->writeData(&frameUBO, sizeof(PBRFrameUBO), 0);

    fillPBRLightFromFrameData(fd);
    _pbrLightUBO[fi]->writeData(&_pbrLight, sizeof(PBRLightUBO), 0);

    preparePBRMaterials(fd);
    _pbrPoolRecreated = false;
}

void ForwardViewportLitPasses::preparePhong(const RenderStageContext& ctx)
{
    const auto& fd = *ctx.frameData;
    uint32_t    fi = ctx.flightIndex;

    uint32_t materialCount = MaterialFactory::get()->getMaterialSize<PhongMaterial>();
    if (_phongMatPool.ensureCapacity(materialCount)) {
        _phongPoolRecreated = true;
    }

    PhongFrameUBO frameUBO{};
    frameUBO.projMat    = fd.projection;
    frameUBO.viewMat    = fd.view;
    frameUBO.resolution = glm::ivec2(ctx.viewportExtent.width, ctx.viewportExtent.height);
    frameUBO.frameIdx   = _getFrameIndex ? static_cast<int32_t>(_getFrameIndex()) : 0;
    frameUBO.time       = _getElapsedTimeSeconds ? static_cast<float>(_getElapsedTimeSeconds()) : 0.0f;
    frameUBO.cameraPos  = fd.cameraPos;
    _phongFrameUBO[fi]->writeData(&frameUBO, sizeof(PhongFrameUBO), 0);

    fillPhongLightFromFrameData(fd);
    _phongLightUBO[fi]->writeData(&_phongLight, sizeof(PhongLightUBO), 0);
    _phongDebugUBO[fi]->writeData(&_phongDebug, sizeof(PhongDebugUBO), 0);

    preparePhongMaterials(fd);
    _phongPoolRecreated = false;
}

void ForwardViewportLitPasses::preparePBRMaterials(const RenderFrameData& fd)
{
    uint32_t         materialCount   = MaterialFactory::get()->getMaterialSize<PBRMaterial>();
    std::vector<int> preparedMaterial(materialCount, 0);

    auto prepareBucket = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            if (!item.material) continue;
            auto* material = static_cast<PBRMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t matIdx = material->getIndex();
            if (preparedMaterial[matIdx]) continue;

            _pbrMatPool.flushDirty(
                material, _pbrPoolRecreated, [](IBuffer* ubo, PBRMaterial* mat)
                {
                    const auto& src = mat->getParams();
                    PBRParamUBO  dst{};
                    dst.albedo    = src.albedo;
                    dst.metallic  = src.metallic;
                    dst.roughness = src.roughness;
                    dst.ao        = src.ao;
                    for (int i = 0; i < PBRMaterial::EResource::Count; ++i) {
                        dst.textures[i].bEnable        = src.textures[i].bEnable;
                        dst.textures[i].rotationRadius = src.textures[i].rotationRadius;
                        dst.textures[i].translation    = src.textures[i].translation;
                        dst.textures[i].scale          = src.textures[i].scale;
                    }
                    ubo->writeData(&dst, sizeof(PBRParamUBO), 0);
                },
                [&](DescriptorSetHandle ds, PBRMaterial* mat)
                {
                    _render->getDescriptorHelper()->updateDescriptorSets(
                        {
                            IDescriptorSetHelper::writeOneImage(ds, 0, mat->getTextureBinding(PBRMaterial::EResource::AlbedoTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 1, mat->getTextureBinding(PBRMaterial::EResource::NormalTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 2, mat->getTextureBinding(PBRMaterial::EResource::MetallicTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 3, mat->getTextureBinding(PBRMaterial::EResource::RoughnessTexture)),
                            IDescriptorSetHelper::writeOneImage(ds, 4, mat->getTextureBinding(PBRMaterial::EResource::AOTexture)),
                        },
                        {});
                });
            preparedMaterial[matIdx] = 1;
        }
    };

    prepareBucket(fd.drawBuckets.staticMeshes.pbrDrawItems);
    prepareBucket(fd.drawBuckets.skinnedMeshes.pbrDrawItems);
}

void ForwardViewportLitPasses::preparePhongMaterials(const RenderFrameData& fd)
{
    uint32_t         materialCount   = MaterialFactory::get()->getMaterialSize<PhongMaterial>();
    std::vector<int> preparedMaterial(materialCount, 0);

    auto prepareBucket = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            if (!item.material) continue;
            auto* material = static_cast<PhongMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t matIdx = material->getIndex();
            if (preparedMaterial[matIdx]) continue;

            _phongMatPool.flushDirty(
                material, _phongPoolRecreated, [&](IBuffer* ubo, PhongMaterial* mat)
                {
                    const auto& params = mat->getParams();
                    ubo->writeData(&params, sizeof(PhongMaterial::ParamUBO), 0);
                },
                [&](DescriptorSetHandle ds, PhongMaterial* mat)
                {
                    auto diffuse    = getDescriptorImageInfo(mat->getTextureBinding(PhongMaterial::EResource::DiffuseTexture));
                    auto specular   = getDescriptorImageInfo(mat->getTextureBinding(PhongMaterial::EResource::SpecularTexture));
                    auto reflection = getDescriptorImageInfo(mat->getTextureBinding(PhongMaterial::EResource::ReflectionTexture));
                    auto normal     = getDescriptorImageInfo(mat->getTextureBinding(PhongMaterial::EResource::NormalTexture));

                    _render->getDescriptorHelper()->updateDescriptorSets(
                        {
                            IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {diffuse}),
                            IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {specular}),
                            IDescriptorSetHelper::genImageWrite(ds, 2, 0, EPipelineDescriptorType::CombinedImageSampler, {reflection}),
                            IDescriptorSetHelper::genImageWrite(ds, 3, 0, EPipelineDescriptorType::CombinedImageSampler, {normal}),
                        },
                        {});
                });
            preparedMaterial[matIdx] = 1;
        }
    };

    prepareBucket(fd.drawBuckets.staticMeshes.phongDrawItems);
    prepareBucket(fd.drawBuckets.skinnedMeshes.phongDrawItems);
}

void ForwardViewportLitPasses::drawPBR(const DrawContext& drawCtx)
{
    const auto& ctx          = drawCtx.stageCtx;
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.pbrDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.pbrDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;
    uint32_t    fi           = ctx.flightIndex;

    if (staticItems.empty() && skinnedItems.empty()) {
        return;
    }

    cmdBuf->debugBeginLabel("ForwardPBR");
    drawCtx.setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* material = static_cast<PBRMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t            matIdx     = material->getIndex();
            DescriptorSetHandle resourceDS = _pbrMatPool.resourceDS(matIdx);
            DescriptorSetHandle paramDS    = _pbrMatPool.paramDS(matIdx);

            auto& pipelineVariant = bSkinned ? _pbrSkinned : _pbrStatic;
            auto* layout = pipelineVariant.pipelineLayout.get();
            cmdBuf->bindPipeline(pipelineVariant.pipeline.get());
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {
                    _pbrFrameDS[fi],
                    resourceDS,
                    paramDS,
                    drawCtx.environmentLightingDescriptorSet,
                    drawCtx.depthBufferShadowDS,
                    drawCtx.skinningDS,
                });
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {
                    _pbrFrameDS[fi],
                    resourceDS,
                    paramDS,
                    drawCtx.environmentLightingDescriptorSet,
                    drawCtx.depthBufferShadowDS,
                });
            }

            PBRPushConstant pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout, EShaderStage::Vertex, 0, sizeof(PBRPushConstant), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(staticItems, false);
    drawBucket(skinnedItems, true);
    cmdBuf->debugEndLabel();
}

void ForwardViewportLitPasses::drawPhong(const DrawContext& drawCtx)
{
    const auto& ctx          = drawCtx.stageCtx;
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.phongDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.phongDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;
    uint32_t    fi           = ctx.flightIndex;

    if (staticItems.empty() && skinnedItems.empty()) {
        return;
    }

    cmdBuf->debugBeginLabel("ForwardPhong");
    drawCtx.setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* material = static_cast<PhongMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t            matIdx     = material->getIndex();
            DescriptorSetHandle resourceDS = _phongMatPool.resourceDS(matIdx);
            DescriptorSetHandle paramDS    = _phongMatPool.paramDS(matIdx);

            auto& pipelineVariant = bSkinned ? _phongSkinned : _phongStatic;
            auto* layout = pipelineVariant.pipelineLayout.get();
            cmdBuf->bindPipeline(pipelineVariant.pipeline.get());
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {
                    _phongFrameDS[fi],
                    resourceDS,
                    paramDS,
                    drawCtx.skyboxDescriptorSet,
                    drawCtx.depthBufferShadowDS,
                    drawCtx.skinningDS,
                });
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {
                    _phongFrameDS[fi],
                    resourceDS,
                    paramDS,
                    drawCtx.skyboxDescriptorSet,
                    drawCtx.depthBufferShadowDS,
                });
            }

            PhongModelPC pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout, EShaderStage::Vertex, 0, sizeof(PhongModelPC), &pc);

            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };

    drawBucket(staticItems, false);
    drawBucket(skinnedItems, true);
    cmdBuf->debugEndLabel();
}

void ForwardViewportLitPasses::applyShadowState(const ShadowRuntimeState& shadowState)
{
    const bool bShadowDefinesChanged = _shadowState.bEnableShadowMapping != shadowState.bEnableShadowMapping;
    const bool bPbrDefinesChanged = bShadowDefinesChanged ||
                                    _shadowState.bEnablePointLightShadow != shadowState.bEnablePointLightShadow;

    _shadowState = shadowState;

    if (bShadowDefinesChanged) {
        _phongStatic.pipelineCI.shaderDesc.defines = buildPhongShaderDefines(_shadowState.bEnableShadowMapping);
        _phongStatic.pipeline->updateDesc(_phongStatic.pipelineCI);

        _phongSkinned.pipelineCI.shaderDesc.defines = buildPhongShaderDefines(_shadowState.bEnableShadowMapping);
        _phongSkinned.pipelineCI.shaderDesc.defines.push_back("ENABLE_SKINNING 1");
        _phongSkinned.pipeline->updateDesc(_phongSkinned.pipelineCI);
    }

    if (bPbrDefinesChanged) {
        _pbrStatic.pipelineCI.shaderDesc.defines = buildPBRShaderDefines(
            _bEnablePBRDiffuseIBL,
            _bEnablePBRSpecularIBL,
            _shadowState.bEnableShadowMapping,
            _shadowState.bEnablePointLightShadow);
        _pbrStatic.pipeline->updateDesc(_pbrStatic.pipelineCI);

        _pbrSkinned.pipelineCI.shaderDesc.defines = buildPBRShaderDefines(
            _bEnablePBRDiffuseIBL,
            _bEnablePBRSpecularIBL,
            _shadowState.bEnableShadowMapping,
            _shadowState.bEnablePointLightShadow);
        _pbrSkinned.pipelineCI.shaderDesc.defines.push_back("ENABLE_SKINNING 1");
        _pbrSkinned.pipeline->updateDesc(_pbrSkinned.pipelineCI);
    }
}

void ForwardViewportLitPasses::fillPBRLightFromFrameData(const RenderFrameData& fd)
{
    _pbrLight             = {};
    _pbrLight.hasDirLight = false;
    if (fd.bHasDirectionalLight) {
        _pbrLight.dirLight.dir          = fd.directionalLight.direction;
        _pbrLight.dirLight.color        = fd.directionalLight.color;
        _pbrLight.dirLight.intensity    = fd.directionalLight.intensity;
        _pbrLight.dirLight.shadowMatrix = fd.directionalLight.viewProjection;
        _pbrLight.hasDirLight           = true;
    }

    _pbrLight.numPointLight                 = fd.numPointLights;
    const uint32_t shadowedPointLightBudget = _shadowState.bEnablePointLightShadow
        ? std::min(_shadowState.maxShadowedPointLights, fd.numPointLights)
        : 0u;
    for (uint32_t i = 0; i < fd.numPointLights; ++i) {
        const auto& src = fd.pointLights[i];
        auto&       dst = _pbrLight.pointLights[i];
        dst             = {};
        dst.pos         = src.position;
        dst.color       = src.color;
        dst.intensity   = src.intensity;
        dst.farPlane    = i < shadowedPointLightBudget ? src.farPlane : 0.0f;
    }
}

void ForwardViewportLitPasses::fillPhongLightFromFrameData(const RenderFrameData& fd)
{
    _phongLight.hasDirectionalLight = fd.bHasDirectionalLight;
    if (fd.bHasDirectionalLight) {
        _phongLight.dirLight.direction              = fd.directionalLight.direction;
        _phongLight.dirLight.color                  = fd.directionalLight.color;
        _phongLight.dirLight.intensity              = fd.directionalLight.intensity;
        _phongLight.dirLight.directionalLightMatrix = fd.directionalLight.viewProjection;
    }

    _phongLight.numPointLights = fd.numPointLights;
    for (uint32_t i = 0; i < fd.numPointLights; ++i) {
        const auto& pl  = fd.pointLights[i];
        auto&       dst = _phongLight.pointLights[i];
        dst             = {};
        dst.type        = pl.type;
        dst.constant    = pl.constant;
        dst.linear      = pl.linear;
        dst.quadratic   = pl.quadratic;
        dst.position    = pl.position;
        dst.color       = pl.color;
        dst.intensity   = pl.intensity;
        dst.nearPlane   = pl.nearPlane;
        dst.farPlane    = pl.farPlane;
        dst.spotDir     = pl.spotDir;
        dst.innerCutOff = pl.innerCutOff;
        dst.outerCutOff = pl.outerCutOff;
    }
}

DescriptorImageInfo ForwardViewportLitPasses::getDescriptorImageInfo(const TextureBinding& tb) const
{
    return DescriptorImageInfo(tb.getImageViewHandle(), tb.getSamplerHandle(), EImageLayout::ShaderReadOnlyOptimal);
}

} // namespace ya

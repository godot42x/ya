#include "ForwardViewportStage.h"

#include "Config/ConfigManager.h"
#include "Core/Math/Math.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/DirectionComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Render.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"
#include "Scene/SceneManager.h"

#include "glm/gtc/type_ptr.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════
// Common vertex attributes
// ═══════════════════════════════════════════════════════════════════════

static const std::vector<VertexAttribute> kVertexAttributes3 = {
    {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
    {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
};

static const std::vector<VertexAttribute> kVertexAttributes4 = {
    {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
    {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
    {.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
};

static const std::vector<VertexAttribute> kSkinningVertexAttributes = {
    {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
    {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
};

static const VertexBufferDescription kVBDesc{.slot = 0, .pitch = sizeof(ya::Vertex)};

static std::vector<std::string> buildPhongShaderDefines(bool bEnableDirectionalShadow)
{
    std::vector<std::string> defines;
    if (bEnableDirectionalShadow) {
        defines.push_back("ENABLE_DIRECTIONAL_SHADOW 1");
    }
    return defines;
}

namespace
{

constexpr const char* FORWARD_PBR_CONFIG_DOC_NAME         = "editor";
constexpr const char* FORWARD_PBR_CONFIG_KEY_IBL_DIFFUSE  = "render.deferred.light.enablePBRDiffuseIBL";
constexpr const char* FORWARD_PBR_CONFIG_KEY_IBL_SPECULAR = "render.deferred.light.enablePBRSpecularIBL";

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

void ForwardViewportStage::initSkinningResources()
{
    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Forward_Skinning_DSL",
            .set      = 5,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
}

void ForwardViewportStage::ensureSkinningCapacity(uint32_t paletteCount)
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
            .label     = "Forward_Skinning_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });

    const uint32_t bufferSize = static_cast<uint32_t>(static_cast<uint64_t>(_skinningCapacity) * sizeof(RenderSkinningPalette));
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skinningSSBO[i] = IBuffer::create(
            _render,
            BufferCreateInfo{
                .label       = std::format("Forward_Skinning_SSBO_{}", i),
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

void ForwardViewportStage::updateSkinningBuffer(const RenderStageContext& ctx)
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

// ═══════════════════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::init(IRender* render)
{
    // Stub — use initWithDesc() instead.
    _render = render;
}

void ForwardViewportStage::initWithDesc(const InitDesc& desc)
{
    _render              = desc.render;
    _depthBufferShadowDS = desc.depthBufferShadowDS;
    _bShadowMapping      = desc.bShadowMapping;

    initSkinningResources();
    initPBR(desc);
    initPhong(desc);
    initUnlit(desc);
    initSimple(desc);
    initSkybox(desc);
    initDebug(desc);
}

void ForwardViewportStage::refreshPipelineFormats(const IRenderTarget* viewportRT)
{
    if (!viewportRT) {
        return;
    }

    const auto& colorDescs = viewportRT->getColorAttachmentDescs();
    const auto& depthDesc  = viewportRT->getDepthAttachmentDesc();
    if (colorDescs.empty()) {
        return;
    }

    const auto colorFormat = colorDescs.front().format;
    const auto depthFormat = depthDesc.has_value() ? depthDesc->format : EFormat::Undefined;

    auto refreshVariant = [&](ShadingPipelineVariant& variant)
    {
        if (!variant.pipeline) {
            return;
        }
        variant.pipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        variant.pipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        variant.pipeline->updateDesc(variant.pipelineCI);
    };

    refreshVariant(_pbrStatic);
    refreshVariant(_pbrSkinned);
    refreshVariant(_phongStatic);
    refreshVariant(_phongSkinned);

    if (_unlitStatic.pipeline) {
        _unlitStatic.pipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        _unlitStatic.pipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _unlitStatic.pipeline->updateDesc(_unlitStatic.pipelineCI);
    }

    if (_unlitSkinned.pipeline) {
        _unlitSkinned.pipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        _unlitSkinned.pipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _unlitSkinned.pipeline->updateDesc(_unlitSkinned.pipelineCI);
    }

    if (_simplePipeline) {
        auto ci                                         = _simplePipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _simplePipeline->updateDesc(std::move(ci));
    }

    if (_skyboxPipeline) {
        auto ci                                         = _skyboxPipeline->getDesc();
        ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        ci.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _skyboxPipeline->updateDesc(std::move(ci));
    }

    if (_debugPipeline) {
        _debugPipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
        _debugPipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = depthFormat;
        _debugPipeline->updateDesc(_debugPipelineCI);
    }
}

// ── PBR ─────────────────────────────────────────────────────────────
void ForwardViewportStage::initPBR(const InitDesc& desc)
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
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes4,
            .defines           = buildPBRShaderDefines(_bEnablePBRDiffuseIBL, _bEnablePBRSpecularIBL, _bShadowMapping, _bEnablePointLightShadow),
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
    _pbrSkinned.pipelineCI.shaderDesc.vertexAttributes = kVertexAttributes4;
    _pbrSkinned.pipelineCI.shaderDesc.vertexAttributes.insert(_pbrSkinned.pipelineCI.shaderDesc.vertexAttributes.end(), kSkinningVertexAttributes.begin(), kSkinningVertexAttributes.end());
    _pbrSkinned.pipelineCI.shaderDesc.defines.push_back("ENABLE_SKINNING 1");
    _pbrSkinned.pipeline = IGraphicsPipeline::create(_render);
    _pbrSkinned.pipeline->recreate(_pbrSkinned.pipelineCI);

    _pbrFrameDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                        .label     = "FwdPBR_Frame_DSP",
                                                        .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                        .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 2}},
                                                    });

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _pbrFrameUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                       .label       = std::format("FwdPBR_Frame_UBO_{}", i),
                                                       .usage       = EBufferUsage::UniformBuffer,
                                                       .size        = sizeof(PBRFrameUBO),
                                                       .memoryUsage = EMemoryUsage::CpuToGpu,
                                                   });
        _pbrLightUBO[i] = IBuffer::create(_render, BufferCreateInfo{
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

// ── Phong ───────────────────────────────────────────────────────────
void ForwardViewportStage::initPhong(const InitDesc& desc)
{
    // DSLs — 5 sets:
    //   set 0: frame (UBO) + light (UBO) + debug (UBO)
    //   set 1: material resources (4 textures)
    //   set 2: material params (UBO)
    //   set 3: skybox cubemap (1 combined image sampler) — shared with _skyboxResourceDSL
    //   set 4: shadow map (dir + point cubemaps)
    auto dsls = IDescriptorSetLayout::create(
        _render,
        {
            DescriptorSetLayoutDesc{
                .label    = "FwdPhong_Frame_DSL",
                .set      = 0,
                .bindings = {
                    {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment},
                    {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                    {.binding = 2, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment},
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
    // dsls[3] and dsls[4] are used only for pipeline layout creation

    _phongStatic.pipelineLayout = IPipelineLayout::create(
        _render, "FwdPhong_Static_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PhongModelPC), .stageFlags = EShaderStage::Vertex | EShaderStage::Geometry}}, dsls);

    _phongStatic.pipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _phongStatic.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "PhongLit.slang",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes4,
            .defines           = buildPhongShaderDefines(_bShadowMapping),
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
        _render, "FwdPhong_Skinned_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PhongModelPC), .stageFlags = EShaderStage::Vertex | EShaderStage::Geometry}}, skinnedDsls);

    _phongSkinned.pipelineCI                         = _phongStatic.pipelineCI;
    _phongSkinned.pipelineCI.pipelineLayout          = _phongSkinned.pipelineLayout.get();
    _phongSkinned.pipelineCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    _phongSkinned.pipelineCI.shaderDesc.vertexAttributes = kVertexAttributes4;
    _phongSkinned.pipelineCI.shaderDesc.vertexAttributes.insert(_phongSkinned.pipelineCI.shaderDesc.vertexAttributes.end(), kSkinningVertexAttributes.begin(), kSkinningVertexAttributes.end());
    _phongSkinned.pipelineCI.shaderDesc.defines.push_back("ENABLE_SKINNING 1");
    _phongSkinned.pipeline = IGraphicsPipeline::create(_render);
    _phongSkinned.pipeline->recreate(_phongSkinned.pipelineCI);

    // Per-flight frame DS (frame + light + debug UBOs)
    _phongFrameDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                          .label     = "FwdPhong_Frame_DSP",
                                                          .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                          .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 3}},
                                                      });

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _phongFrameUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                         .label       = std::format("FwdPhong_Frame_UBO_{}", i),
                                                         .usage       = EBufferUsage::UniformBuffer,
                                                         .size        = sizeof(PhongFrameUBO),
                                                         .memoryUsage = EMemoryUsage::CpuToGpu,
                                                     });
        _phongLightUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                         .label       = std::format("FwdPhong_Light_UBO_{}", i),
                                                         .usage       = EBufferUsage::UniformBuffer,
                                                         .size        = sizeof(PhongLightUBO),
                                                         .memoryUsage = EMemoryUsage::CpuToGpu,
                                                     });
        _phongDebugUBO[i] = IBuffer::create(_render, BufferCreateInfo{
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

    // Material pool
    constexpr uint32_t phongTextureCount = 4; // diffuse, specular, reflection, normal
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

// ── Unlit ───────────────────────────────────────────────────────────
void ForwardViewportStage::initUnlit(const InitDesc& desc)
{
    auto dsls         = IDescriptorSetLayout::create(_render, {
                                                                  DescriptorSetLayoutDesc{
                                                                      .label    = "FwdUnlit_Frame_DSL",
                                                                      .set      = 0,
                                                                      .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
                                                                  },
                                                                  DescriptorSetLayoutDesc{
                                                                      .label    = "FwdUnlit_Param_DSL",
                                                                      .set      = 1,
                                                                      .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
                                                                  },
                                                                  DescriptorSetLayoutDesc{
                                                                      .label    = "FwdUnlit_Resource_DSL",
                                                                      .set      = 2,
                                                                      .bindings = {
                                                                          {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                                                                          {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                                                                      },
                                                                  },
                                                              });
    _unlitFrameDSL    = dsls[0];
    _unlitParamDSL    = dsls[1];
    _unlitResourceDSL = dsls[2];

    _unlitStatic.pipelineLayout = IPipelineLayout::create(
        _render, "FwdUnlit_Static_PPL", {PushConstantRange{.offset = 0, .size = sizeof(UnlitPC), .stageFlags = EShaderStage::Vertex}}, dsls);

    _unlitStatic.pipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _unlitStatic.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/Unlit.glsl",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .frontFace = EFrontFaceType::CounterClockWise},
        .multisampleState   = {.sampleCount = ESampleCount::Sample_1},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = false,
                                   .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::One,
                                   .dstAlphaBlendFactor = EBlendFactor::Zero,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _unlitStatic.pipeline = IGraphicsPipeline::create(_render);
    _unlitStatic.pipeline->recreate(_unlitStatic.pipelineCI);

    auto skinnedDsls = dsls;
    skinnedDsls.push_back(_skinningDSL);
    _unlitSkinned.pipelineLayout = IPipelineLayout::create(
        _render, "FwdUnlit_Skinned_PPL", {PushConstantRange{.offset = 0, .size = sizeof(UnlitPC), .stageFlags = EShaderStage::Vertex}}, skinnedDsls);

    _unlitSkinned.pipelineCI                         = _unlitStatic.pipelineCI;
    _unlitSkinned.pipelineCI.pipelineLayout          = _unlitSkinned.pipelineLayout.get();
    _unlitSkinned.pipelineCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    _unlitSkinned.pipelineCI.shaderDesc.vertexAttributes = kVertexAttributes3;
    _unlitSkinned.pipelineCI.shaderDesc.vertexAttributes.insert(_unlitSkinned.pipelineCI.shaderDesc.vertexAttributes.end(), kSkinningVertexAttributes.begin(), kSkinningVertexAttributes.end());
    _unlitSkinned.pipelineCI.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 5"};
    _unlitSkinned.pipeline = IGraphicsPipeline::create(_render);
    _unlitSkinned.pipeline->recreate(_unlitSkinned.pipelineCI);

    // Frame DS ring buffer
    _unlitFrameDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                          .maxSets   = UNLIT_FRAME_SLOTS,
                                                          .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = UNLIT_FRAME_SLOTS}},
                                                      });
    std::vector<DescriptorSetHandle> sets;
    _unlitFrameDSP->allocateDescriptorSets(_unlitFrameDSL, UNLIT_FRAME_SLOTS, sets);
    for (uint32_t i = 0; i < UNLIT_FRAME_SLOTS; ++i) {
        _unlitFrameDSs[i]  = sets[i];
        _unlitFrameUBOs[i] = IBuffer::create(_render, BufferCreateInfo{
                                                          .label       = std::format("FwdUnlit_Frame_UBO_{}", i),
                                                          .usage       = EBufferUsage::UniformBuffer,
                                                          .size        = sizeof(UnlitFrameUBO),
                                                          .memoryUsage = EMemoryUsage::CpuToGpu,
                                                      });
    }

    // Material pool
    constexpr uint32_t unlitTextureCount = 2;
    _unlitMatPool.init(
        _render, _unlitParamDSL, _unlitResourceDSL, [unlitTextureCount](uint32_t n) -> std::vector<DescriptorPoolSize>
        { return {
              {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
              {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * unlitTextureCount},
          }; },
        16);
    _unlitPoolRecreated = true;
}

// ── Simple ──────────────────────────────────────────────────────────
void ForwardViewportStage::initSimple(const InitDesc& desc)
{
    _simplePPL = IPipelineLayout::create(
        _render, "FwdSimple_PPL", {PushConstantRange{.offset = 0, .size = sizeof(SimplePC), .stageFlags = EShaderStage::Vertex}}, {});

    GraphicsPipelineCreateInfo ci{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _simplePPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/SimpleMaterial.glsl",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .frontFace = EFrontFaceType::CounterClockWise},
        .multisampleState   = {.sampleCount = ESampleCount::Sample_1},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::Less},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = false,
                                   .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                                   .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::One,
                                   .dstAlphaBlendFactor = EBlendFactor::Zero,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _simplePipeline = IGraphicsPipeline::create(_render);
    _simplePipeline->recreate(ci);
}

// ── Skybox ──────────────────────────────────────────────────────────
void ForwardViewportStage::initSkybox(const InitDesc& desc)
{
    auto dsls          = IDescriptorSetLayout::create(_render, {
                                                                   DescriptorSetLayoutDesc{
                                                                       .label    = "FwdSkybox_PerFrame_DSL",
                                                                       .set      = 0,
                                                                       .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
                                                                   },
                                                                   DescriptorSetLayoutDesc{
                                                                       .label    = "FwdSkybox_Resource_DSL",
                                                                       .set      = 1,
                                                                       .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
                                                                   },
                                                               });
    _skyboxFrameDSL    = dsls[0];
    _skyboxResourceDSL = dsls[1];

    _skyboxPPL = IPipelineLayout::create(_render, "FwdSkybox_PPL", {}, dsls);

    GraphicsPipelineCreateInfo ci{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _skyboxPPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Skybox.glsl",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Front, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _skyboxPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_skyboxPipeline && _skyboxPipeline->recreate(ci), "Failed to create Forward Skybox pipeline");

    // Per-flight UBO + DS
    _skyboxDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                      .label     = "FwdSkybox_DSP",
                                                      .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
                                                      .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
                                                  });

    SkyboxFrameUBO initialData{};
    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _skyboxFrameUBO[i] = IBuffer::create(_render, BufferCreateInfo{
                                                          .label       = std::format("FwdSkybox_Frame_UBO_{}", i),
                                                          .usage       = EBufferUsage::UniformBuffer,
                                                          .size        = sizeof(SkyboxFrameUBO),
                                                          .memoryUsage = EMemoryUsage::CpuToGpu,
                                                      });
        _skyboxFrameUBO[i]->writeData(&initialData, sizeof(SkyboxFrameUBO), 0);

        _skyboxFrameDS[i] = _skyboxDSP->allocateDescriptorSets(_skyboxFrameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_skyboxFrameDS[i], 0, _skyboxFrameUBO[i].get()),
        });
    }
}

// ── Debug ───────────────────────────────────────────────────────────
void ForwardViewportStage::initDebug(const InitDesc& desc)
{
    _debugDSL = IDescriptorSetLayout::create(_render,
                                             DescriptorSetLayoutDesc{
                                                 .label    = "FwdDebug_DSL",
                                                 .set      = 0,
                                                 .bindings = {
                                                     {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Geometry | EShaderStage::Fragment},
                                                 },
                                             });

    _debugPPL = IPipelineLayout::create(
        _render, "FwdDebug_PPL", {PushConstantRange{.offset = 0, .size = sizeof(DebugModelPC), .stageFlags = EShaderStage::Vertex}}, {_debugDSL});

    _debugPipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _debugPPL.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/DebugRender.glsl",
            .vertexBufferDescs = {kVBDesc},
            .vertexAttributes  = kVertexAttributes3,
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Scissor, EPipelineDynamicFeature::Viewport},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::Back, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = true, .bDepthWriteEnable = true, .depthCompareOp = ECompareOp::LessOrEqual},
        .colorBlendState    = {.attachments = {{
                                   .index               = 0,
                                   .bBlendEnable        = false,
                                   .srcColorBlendFactor = EBlendFactor::One,
                                   .dstColorBlendFactor = EBlendFactor::Zero,
                                   .colorBlendOp        = EBlendOp::Add,
                                   .srcAlphaBlendFactor = EBlendFactor::One,
                                   .dstAlphaBlendFactor = EBlendFactor::Zero,
                                   .alphaBlendOp        = EBlendOp::Add,
                                   .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                               }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _debugPipeline = IGraphicsPipeline::create(_render);
    _debugPipeline->recreate(_debugPipelineCI);

    _debugDSP       = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                           .maxSets   = 1,
                                                           .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1}},
                                                       });
    _debugUboDS     = _debugDSP->allocateDescriptorSets(_debugDSL);
    _debugUboBuffer = IBuffer::create(_render, BufferCreateInfo{
                                                   .label       = "FwdDebug_UBO",
                                                   .usage       = EBufferUsage::UniformBuffer,
                                                   .size        = sizeof(DebugUBO),
                                                   .memoryUsage = EMemoryUsage::CpuToGpu,
                                               });
    _render->getDescriptorHelper()->updateDescriptorSets({
                                                             IDescriptorSetHelper::genSingleBufferWrite(_debugUboDS, 0, EPipelineDescriptorType::UniformBuffer, _debugUboBuffer.get()),
                                                         },
                                                         {});
}

// ═══════════════════════════════════════════════════════════════════════
// Destroy
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::destroy()
{
    // PBR
    _pbrMatPool = {};
    _pbrStatic = {};
    _pbrSkinned = {};
    _pbrFrameDSL.reset();
    _pbrResourceDSL.reset();
    _pbrParamDSL.reset();
    _pbrFrameDSP.reset();
    for (auto& u : _pbrFrameUBO) u.reset();
    for (auto& u : _pbrLightUBO) u.reset();

    // Phong
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

    // Unlit
    _unlitMatPool = {};
    _unlitStatic = {};
    _unlitSkinned = {};
    _unlitFrameDSL.reset();
    _unlitParamDSL.reset();
    _unlitResourceDSL.reset();
    _unlitFrameDSP.reset();
    for (auto& u : _unlitFrameUBOs) u.reset();
    for (auto& u : _skinningSSBO) u.reset();
    _skinningDSP.reset();
    _skinningDSL.reset();
    _skinningCapacity = 0;

    // Simple
    _simplePipeline.reset();
    _simplePPL.reset();

    // Skybox
    _skyboxPipeline.reset();
    _skyboxPPL.reset();
    _skyboxFrameDSL.reset();
    _skyboxResourceDSL.reset();
    _skyboxDSP.reset();
    for (auto& u : _skyboxFrameUBO) u.reset();

    // Debug
    _debugPipeline.reset();
    _debugPPL.reset();
    _debugDSL.reset();
    _debugDSP.reset();
    _debugUboBuffer.reset();

    _render = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Prepare
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::prepare(const RenderStageContext& ctx)
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
    if (_unlitStatic.pipeline) {
        _unlitStatic.pipeline->beginFrame();
    }
    if (_unlitSkinned.pipeline) {
        _unlitSkinned.pipeline->beginFrame();
    }
    if (_simplePipeline) {
        _simplePipeline->beginFrame();
    }
    if (_skyboxPipeline) {
        _skyboxPipeline->beginFrame();
    }
    if (_debugPipeline) {
        _debugPipeline->beginFrame();
    }

    if (!ctx.frameData) return;

    updateSkinningBuffer(ctx);
    preparePBR(ctx);
    preparePhong(ctx);
    prepareUnlit(ctx);

    // Skybox: update per-flight UBO
    SkyboxFrameUBO skyboxUBO{
        .projection = ctx.frameData->projection,
        .view       = FMath::dropTranslation(ctx.frameData->view),
    };
    _skyboxFrameUBO[ctx.flightIndex]->writeData(&skyboxUBO, sizeof(SkyboxFrameUBO), 0);
}

void ForwardViewportStage::preparePBR(const RenderStageContext& ctx)
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
}

void ForwardViewportStage::preparePhong(const RenderStageContext& ctx)
{
    const auto& fd = *ctx.frameData;
    uint32_t    fi = ctx.flightIndex;

    // Ensure material pool capacity
    uint32_t materialCount = MaterialFactory::get()->getMaterialSize<PhongMaterial>();
    if (_phongMatPool.ensureCapacity(materialCount)) {
        _phongPoolRecreated = true;
    }

    // Update frame UBO
    auto*         app = App::get();
    PhongFrameUBO frameUBO{};
    frameUBO.projMat    = fd.projection;
    frameUBO.viewMat    = fd.view;
    frameUBO.resolution = glm::ivec2(ctx.viewportExtent.width, ctx.viewportExtent.height);
    frameUBO.frameIdx   = app->getFrameIndex();
    frameUBO.time       = static_cast<float>(app->getElapsedTimeMS()) / 1000.0f;
    frameUBO.cameraPos  = fd.cameraPos;
    _phongFrameUBO[fi]->writeData(&frameUBO, sizeof(PhongFrameUBO), 0);

    // Update light UBO
    fillPhongLightFromFrameData(fd);
    _phongLightUBO[fi]->writeData(&_phongLight, sizeof(PhongLightUBO), 0);

    // Update debug UBO
    _phongDebugUBO[fi]->writeData(&_phongDebug, sizeof(PhongDebugUBO), 0);
}

void ForwardViewportStage::prepareUnlit(const RenderStageContext& ctx)
{
    uint32_t materialCount = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    if (_unlitMatPool.ensureCapacity(materialCount)) {
        _unlitPoolRecreated = true;
    }

    // Update current slot frame UBO
    auto*         app = App::get();
    UnlitFrameUBO ubo{};
    ubo.projMat    = ctx.frameData->projection;
    ubo.viewMat    = ctx.frameData->view;
    ubo.resolution = glm::ivec2(ctx.viewportExtent.width, ctx.viewportExtent.height);
    ubo.frameIdx   = app->getFrameIndex();
    ubo.time       = static_cast<float>(app->getElapsedTimeMS()) / 1000.0f;

    uint32_t slot = _unlitFrameSlot;
    _unlitFrameUBOs[slot]->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(_unlitFrameUBOs[slot]->getHandle()), 0, sizeof(UnlitFrameUBO));
    _render->getDescriptorHelper()->updateDescriptorSets({
                                                             IDescriptorSetHelper::genBufferWrite(_unlitFrameDSs[slot], 0, 0, EPipelineDescriptorType::UniformBuffer, {bufferInfo}),
                                                         },
                                                         {});
}

// ═══════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData) return;

    drawSkybox(ctx);
    drawPBR(ctx);
    drawPhong(ctx);
    drawUnlit(ctx);
    drawSimple(ctx);
    drawDebug(ctx);
}

// ── Skybox draw ─────────────────────────────────────────────────────

void ForwardViewportStage::drawSkybox(const RenderStageContext& ctx)
{
    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    // Check if skybox is available
    auto* resolver = App::get()->getResourceResolveSystem();
    auto* scene    = App::get()->getSceneManager()->getActiveScene();
    if (!resolver || !scene) return;

    const auto* skyboxState = resolver->findFirstSceneSkyboxState(scene);
    if (!skyboxState || !skyboxState->hasRenderableCubemap()) return;

    auto* runtime = App::get()->getRenderRuntime();
    if (!runtime) return;
    auto skyboxDS = runtime->getSceneSkyboxDescriptorSet(scene);

    // Get cube mesh from scene's skybox entity, or use primitive cache
    Mesh* cubeMesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
    for (const auto& [entity, sc, mc] : scene->getRegistry().view<SkyboxComponent, StaticMeshComponent>().each()) {
        if (mc.isResolved() && mc.getMesh()) {
            cubeMesh = mc.getMesh();
        }
        break;
    }
    if (!cubeMesh) return;

    cmdBuf->debugBeginLabel("ForwardSkybox");

    cmdBuf->bindPipeline(_skyboxPipeline.get());
    setViewportAndScissor(cmdBuf, vpW, vpH);

    cmdBuf->bindDescriptorSets(_skyboxPPL.get(), 0, {_skyboxFrameDS[ctx.flightIndex], skyboxDS});
    cubeMesh->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

// ── PBR draw ────────────────────────────────────────────────────────

void ForwardViewportStage::drawPBR(const RenderStageContext& ctx)
{
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.pbrDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.pbrDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;
    uint32_t    fi           = ctx.flightIndex;

    if (staticItems.empty() && skinnedItems.empty()) return;

    auto*               runtime       = App::get()->getRenderRuntime();
    auto*               scene         = App::get()->getSceneManager()->getActiveScene();
    DescriptorSetHandle environmentDS = (runtime && scene) ? runtime->getSceneEnvironmentLightingDescriptorSet(scene) : DescriptorSetHandle{};

    cmdBuf->debugBeginLabel("ForwardPBR");
    setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    uint32_t         materialCount = MaterialFactory::get()->getMaterialSize<PBRMaterial>();
    std::vector<int> updatedMaterial(materialCount, 0);

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* material = static_cast<PBRMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t            matIdx     = material->getIndex();
            DescriptorSetHandle resourceDS = _pbrMatPool.resourceDS(matIdx);
            DescriptorSetHandle paramDS    = _pbrMatPool.paramDS(matIdx);

            if (!updatedMaterial[matIdx]) {
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
                        ubo->writeData(&dst, sizeof(PBRParamUBO), 0); },
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
                updatedMaterial[matIdx] = 1;
            }

            auto& pipelineVariant = bSkinned ? _pbrSkinned : _pbrStatic;
            auto* layout = pipelineVariant.pipelineLayout.get();
            cmdBuf->bindPipeline(pipelineVariant.pipeline.get());
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {
                                                                                _pbrFrameDS[fi],
                                                                                resourceDS,
                                                                                paramDS,
                                                                                environmentDS,
                                                                                _depthBufferShadowDS,
                                                                                _skinningDS[fi],
                                                                            });
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {
                                                                                _pbrFrameDS[fi],
                                                                                resourceDS,
                                                                                paramDS,
                                                                                environmentDS,
                                                                                _depthBufferShadowDS,
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

    _pbrPoolRecreated = false;
    cmdBuf->debugEndLabel();
}

// ── Phong draw ──────────────────────────────────────────────────────

void ForwardViewportStage::drawPhong(const RenderStageContext& ctx)
{
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.phongDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.phongDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;
    uint32_t    fi           = ctx.flightIndex;

    if (staticItems.empty() && skinnedItems.empty()) return;

    auto*               runtime  = App::get()->getRenderRuntime();
    auto*               scene    = App::get()->getSceneManager()->getActiveScene();
    DescriptorSetHandle skyboxDS = (runtime && scene) ? runtime->getSceneSkyboxDescriptorSet(scene) : DescriptorSetHandle{};

    cmdBuf->debugBeginLabel("ForwardPhong");
    setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    // Material tracking
    uint32_t         materialCount = MaterialFactory::get()->getMaterialSize<PhongMaterial>();
    std::vector<int> updatedMaterial(materialCount, 0);

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* material = static_cast<PhongMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t            matIdx     = material->getIndex();
            DescriptorSetHandle resourceDS = _phongMatPool.resourceDS(matIdx);
            DescriptorSetHandle paramDS    = _phongMatPool.paramDS(matIdx);

            if (!updatedMaterial[matIdx]) {
                _phongMatPool.flushDirty(
                    material, _phongPoolRecreated, [&](IBuffer* ubo, PhongMaterial* mat)
                    {
                        const auto& params = mat->getParams();
                        ubo->writeData(&params, sizeof(PhongMaterial::ParamUBO), 0); },
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
                updatedMaterial[matIdx] = 1;
            }

            auto& pipelineVariant = bSkinned ? _phongSkinned : _phongStatic;
            auto* layout = pipelineVariant.pipelineLayout.get();
            cmdBuf->bindPipeline(pipelineVariant.pipeline.get());
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {
                                                                                   _phongFrameDS[fi],
                                                                                   resourceDS,
                                                                                   paramDS,
                                                                                   skyboxDS,
                                                                                   _depthBufferShadowDS,
                                                                                   _skinningDS[fi],
                                                                               });
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {
                                                                                   _phongFrameDS[fi],
                                                                                   resourceDS,
                                                                                   paramDS,
                                                                                   skyboxDS,
                                                                                   _depthBufferShadowDS,
                                                                               });
            }

            PhongModelPC pc{.modelMat = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout,
                                  EShaderStage::Vertex | EShaderStage::Geometry,
                                  0,
                                  sizeof(PhongModelPC),
                                  &pc);

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

    _phongPoolRecreated = false;
    cmdBuf->debugEndLabel();
}

// ── Unlit draw ──────────────────────────────────────────────────────

void ForwardViewportStage::drawUnlit(const RenderStageContext& ctx)
{
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.unlitDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.unlitDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;
    uint32_t    fi           = ctx.flightIndex;

    if (staticItems.empty() && skinnedItems.empty()) return;

    cmdBuf->debugBeginLabel("ForwardUnlit");
    setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    uint32_t          materialCount = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    std::vector<bool> updatedMaterial(materialCount);

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* material = static_cast<UnlitMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t            matIdx     = material->getIndex();
            DescriptorSetHandle paramDS    = _unlitMatPool.paramDS(matIdx);
            DescriptorSetHandle resourceDS = _unlitMatPool.resourceDS(matIdx);

            if (!updatedMaterial[matIdx]) {
                _unlitMatPool.flushDirty(
                    material, _unlitPoolRecreated, [&](IBuffer* ubo, UnlitMaterial* mat)
                    {
                        const auto& params = mat->getParams();
                        ubo->writeData(&params, sizeof(UnlitMaterial::ParamUBO), 0); },
                    [&](DescriptorSetHandle ds, UnlitMaterial* mat)
                    {
                        DescriptorImageInfo img0(mat->getImageViewHandle(UnlitMaterial::BaseColor0),
                                                 mat->getSamplerHandle(UnlitMaterial::BaseColor0),
                                                 EImageLayout::ShaderReadOnlyOptimal);
                        DescriptorImageInfo img1(mat->getImageViewHandle(UnlitMaterial::BaseColor1),
                                                 mat->getSamplerHandle(UnlitMaterial::BaseColor1),
                                                 EImageLayout::ShaderReadOnlyOptimal);
                        _render->getDescriptorHelper()->updateDescriptorSets({
                                                                                 IDescriptorSetHelper::genImageWrite(ds, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {img0}),
                                                                                 IDescriptorSetHelper::genImageWrite(ds, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {img1}),
                                                                             },
                                                                             {});
                    });
                updatedMaterial[matIdx] = true;
            }

            auto& pipelineVariant = bSkinned ? _unlitSkinned : _unlitStatic;
            auto* layout = pipelineVariant.pipelineLayout.get();
            cmdBuf->bindPipeline(pipelineVariant.pipeline.get());
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {_unlitFrameDSs[_unlitFrameSlot], paramDS, resourceDS, _skinningDS[fi]});
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {_unlitFrameDSs[_unlitFrameSlot], paramDS, resourceDS});
            }

            UnlitPC pc{.modelMatrix = item.worldMatrix, .skinningPaletteIndex = item.skinningPaletteIndex};
            cmdBuf->pushConstants(layout, EShaderStage::Vertex, 0, sizeof(UnlitPC), &pc);

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

    _unlitPoolRecreated = false;
    _unlitFrameSlot     = (_unlitFrameSlot + 1) % UNLIT_FRAME_SLOTS;

    cmdBuf->debugEndLabel();
}

// ── Simple draw ─────────────────────────────────────────────────────

void ForwardViewportStage::drawSimple(const RenderStageContext& ctx)
{
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.simpleDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.simpleDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;

    auto* scene = App::get()->getSceneManager()->getActiveScene();
    if (!scene) return;

    bool hasSimple = !staticItems.empty() || !skinnedItems.empty();

    // Direction components (editor visualization — still from registry, TODO: migrate to snapshot)
    const auto& dirView      = scene->getRegistry().view<TransformComponent, DirectionComponent>();
    bool        hasDirection = dirView.begin() != dirView.end();

    if (!hasSimple && !hasDirection) return;

    cmdBuf->debugBeginLabel("ForwardSimple");
    cmdBuf->bindPipeline(_simplePipeline.get());
    setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    SimplePC pc{};
    pc.view       = fd.view;
    pc.projection = fd.projection;

    // Simple material draw items (from snapshot)
    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* mat    = static_cast<SimpleMaterial*>(item.material);
            pc.model     = item.worldMatrix;
            pc.colorType = mat->colorType;
            cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
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

    // Direction component visualization (from registry)
    auto* cone     = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cone);
    auto* cylinder = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cylinder);

    glm::mat4 coneLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.3f, 1.0f, 0.3f));
    glm::mat4 cylinderLocalTransf =
        glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(glm::mat4(1.0), glm::vec3(0.1f, 1.0f, 0.1f));

    pc.colorType = _simpleDefaultColorType;
    for (auto entity : dirView) {
        const auto& [tc, dc] = dirView.get(entity);

        glm::mat4 worldTransform = glm::translate(glm::mat4(1.0), tc.getWorldPosition()) *
                                   glm::mat4_cast(glm::quat(glm::radians(tc.getRotation())));

        pc.model = glm::translate(glm::mat4(1.0), -tc.getForward()) * coneLocalTransf * worldTransform;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cone->draw(cmdBuf);

        pc.model = worldTransform * cylinderLocalTransf;
        cmdBuf->pushConstants(_simplePPL.get(), EShaderStage::Vertex, 0, sizeof(SimplePC), &pc);
        cylinder->draw(cmdBuf);
    }

    cmdBuf->debugEndLabel();
}

// ── Debug draw ──────────────────────────────────────────────────────

void ForwardViewportStage::drawDebug(const RenderStageContext& ctx)
{
    if (_debugMode == DebugNone) return;

    auto*       cmdBuf = ctx.cmdBuf;
    const auto& fd     = *ctx.frameData;

    // Collect all meshes (from all draw item buckets)
    auto collectMeshes = [&]() -> bool
    {
        return fd.drawBuckets.totalDrawCount() > 0;
    };
    if (!collectMeshes()) return;

    auto vpW = ctx.viewportExtent.width;
    auto vpH = ctx.viewportExtent.height;
    if (vpW == 0 || vpH == 0) return;

    // Update debug UBO
    auto* app            = App::get();
    _debugUBO.projection = fd.projection;
    _debugUBO.view       = fd.view;
    _debugUBO.resolution = glm::ivec2(static_cast<int>(vpW), static_cast<int>(vpH));
    _debugUBO.time       = static_cast<float>(app->getElapsedTimeMS()) / 1000.0f;
    _debugUboBuffer->writeData(&_debugUBO, sizeof(DebugUBO), 0);

    cmdBuf->debugBeginLabel("ForwardDebug");
    cmdBuf->bindPipeline(_debugPipeline.get());
    setViewportAndScissor(cmdBuf, vpW, vpH);

    // Draw all items from all buckets
    auto drawItems = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh) continue;
            DebugModelPC pc{.modelMat = item.worldMatrix};
            cmdBuf->bindDescriptorSets(_debugPPL.get(), 0, {_debugUboDS});
            cmdBuf->pushConstants(_debugPPL.get(), EShaderStage::Vertex, 0, sizeof(DebugModelPC), &pc);
            if (bSkinned) {
                item.mesh->drawSkinned(cmdBuf);
            }
            else {
                item.mesh->drawStatic(cmdBuf);
            }
        }
    };
    auto drawBucketSet = [&](const RenderShadingDrawBuckets& buckets, bool bSkinned)
    {
        drawItems(buckets.pbrDrawItems, bSkinned);
        drawItems(buckets.phongDrawItems, bSkinned);
        drawItems(buckets.unlitDrawItems, bSkinned);
        drawItems(buckets.simpleDrawItems, bSkinned);
        drawItems(buckets.fallbackDrawItems, bSkinned);
    };
    drawBucketSet(fd.drawBuckets.staticMeshes, false);
    drawBucketSet(fd.drawBuckets.skinnedMeshes, true);

    cmdBuf->debugEndLabel();
}

// ═══════════════════════════════════════════════════════════════════════
// GUI
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::renderGUI()
{
    if (!ImGui::TreeNode("ForwardViewportStage")) return;

    if (ImGui::TreeNode("Phong Debug")) {
        bool bDebugNormal = (_phongDebug.bDebugNormal != 0);
        bool bDebugDepth  = (_phongDebug.bDebugDepth != 0);
        bool bDebugUV     = (_phongDebug.bDebugUV != 0);

        if (ImGui::Checkbox("Debug Normal", &bDebugNormal)) _phongDebug.bDebugNormal = bDebugNormal ? 1u : 0u;
        if (ImGui::Checkbox("Debug Depth", &bDebugDepth)) _phongDebug.bDebugDepth = bDebugDepth ? 1u : 0u;
        if (ImGui::Checkbox("Debug UV", &bDebugUV)) _phongDebug.bDebugUV = bDebugUV ? 1u : 0u;
        ImGui::DragFloat4("Float Param", glm::value_ptr(_phongDebug.floatParam), 0.1f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Debug Render")) {
        const char* modeNames[] = {"None", "NormalColor", "NormalDir", "Depth", "UV"};
        int         mode        = static_cast<int>(_debugMode);
        if (ImGui::Combo("Mode", &mode, modeNames, IM_ARRAYSIZE(modeNames))) {
            EDebugMode newMode = static_cast<EDebugMode>(mode);
            if (newMode != _debugMode) {
                if (newMode == DebugNormalDir) {
                    _debugPipelineCI.shaderDesc.defines = {"DEBUG_NORMAL_DIR"};
                    _debugPipeline->updateDesc(_debugPipelineCI);
                }
                else if (_debugMode == DebugNormalDir) {
                    _debugPipelineCI.shaderDesc.defines = {};
                    _debugPipeline->updateDesc(_debugPipelineCI);
                }
                _debugMode     = newMode;
                _debugUBO.mode = static_cast<int>(_debugMode);
            }
        }
        ImGui::DragFloat4("Float Param", glm::value_ptr(_debugUBO.floatParam), 0.1f);
        ImGui::TreePop();
    }
    _simplePipeline->renderGUI();
    _unlitStatic.pipeline->renderGUI();
    _unlitSkinned.pipeline->renderGUI();
    _phongStatic.pipeline->renderGUI();
    _phongSkinned.pipeline->renderGUI();
    _debugPipeline->renderGUI();
    _skyboxPipeline->renderGUI();

    ImGui::Combo("Simple Color Type", &_simpleDefaultColorType, "Normal\0UV\0Fixed");

    _pbrStatic.pipeline->renderGUI();
    _pbrSkinned.pipeline->renderGUI();
    ImGui::TreePop();
}

// ═══════════════════════════════════════════════════════════════════════
// Shadow mapping toggle
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::setShadowMappingEnabled(bool enabled)
{
    if (_bShadowMapping == enabled) return;
    _bShadowMapping = enabled;

    _phongStatic.pipelineCI.shaderDesc.defines = buildPhongShaderDefines(_bShadowMapping);
    _phongStatic.pipeline->updateDesc(_phongStatic.pipelineCI);

    _phongSkinned.pipelineCI.shaderDesc.defines = buildPhongShaderDefines(_bShadowMapping);
    _phongSkinned.pipelineCI.shaderDesc.defines.push_back("ENABLE_SKINNING 1");
    _phongSkinned.pipeline->updateDesc(_phongSkinned.pipelineCI);
}

// ═══════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════

void ForwardViewportStage::setViewportAndScissor(ICommandBuffer* cmdBuf, uint32_t w, uint32_t h)
{
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(h);
    if (bReverseViewportY) {
        viewportY      = static_cast<float>(h);
        viewportHeight = -static_cast<float>(h);
    }
    cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(w), viewportHeight, 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, w, h);
}

void ForwardViewportStage::fillPBRLightFromFrameData(const RenderFrameData& fd)
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
    const uint32_t shadowedPointLightBudget = _bEnablePointLightShadow
                                                ? std::min(_maxShadowedPointLights, fd.numPointLights)
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

void ForwardViewportStage::fillPhongLightFromFrameData(const RenderFrameData& fd)
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

DescriptorImageInfo ForwardViewportStage::getDescriptorImageInfo(const TextureBinding& tb)
{
    return DescriptorImageInfo(tb.getImageViewHandle(), tb.getSamplerHandle(), EImageLayout::ShaderReadOnlyOptimal);
}

} // namespace ya

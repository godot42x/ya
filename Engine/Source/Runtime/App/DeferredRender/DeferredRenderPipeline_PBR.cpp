#include "DeferredRenderPipeline.h"

#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Material/MaterialFactory.h"

namespace ya
{

void DeferredRenderPipeline::initPBR()
{
    auto* render = _render;

    // DSLs: set 1 = 5 textures, set 2 = param UBO
    auto dsls = IDescriptorSetLayout::create(
        render,
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
    _pbrMaterialResourceDSL = dsls[0];
    _pbrParamsDSL           = dsls[1];

    // Pipeline layout
    _pbrGBufferPPL = IPipelineLayout::create(
        render, "Deferred_PBR_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PBRGBufferPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _pbrMaterialResourceDSL, _pbrParamsDSL});

    // Pipeline
    std::vector<EFormat::T> gBufferFormats = {SIGNED_LINEAR_FORMAT, SIGNED_LINEAR_FORMAT, LINEAR_FORMAT, SHADING_MODEL_FORMAT};

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {
            .label                  = "PBR GBuffer Pass",
            .colorAttachmentFormats = gBufferFormats,
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _pbrGBufferPPL.get(),
        .shaderDesc     = ShaderDesc{

            .shaderName        = "DeferredRender/Unified_GBufferPass_PBR.slang",
            .bDeriveFromShader = false,
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
    _pbrGBufferPipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_pbrGBufferPipeline && _pbrGBufferPipeline->recreate(ci), "Failed to create PBR GBuffer pipeline");

    // Material pool
    const uint32_t texCount = static_cast<uint32_t>(_pbrMaterialResourceDSL->getLayoutInfo().bindings.size());
    _pbrMatPool.init(
        render,
        _pbrParamsDSL,
        _pbrMaterialResourceDSL,
        [texCount](uint32_t n) {
            return std::vector<DescriptorPoolSize>{
                {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
            };
        },
        16);
}

void DeferredRenderPipeline::preparePBR(Scene* scene)
{
    auto& reg = scene->getRegistry();

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
            [](IBuffer* ubo, PBRMaterial* m) {
                ubo->writeData(&m->getParams(), sizeof(PBRParamUBO), 0);
            },
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

void DeferredRenderPipeline::drawPBR(ICommandBuffer* cmdBuf, Scene* scene)
{
    auto& reg = scene->getRegistry();

    cmdBuf->bindPipeline(_pbrGBufferPipeline.get());
    for (const auto& [entity, mc, tc, pmc] :
         reg.view<MeshComponent, TransformComponent, PBRMaterialComponent>().each()) {
        PBRMaterial* mat = pmc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());

        cmdBuf->bindDescriptorSets(_pbrGBufferPPL.get(), 0, {_frameAndLightDS, _pbrMatPool.resourceDS(idx), _pbrMatPool.paramDS(idx)});
        PBRGBufferPushConstant pc{.modelMat = tc.getTransform()};
        cmdBuf->pushConstants(_pbrGBufferPPL.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        mc.getMesh()->draw(cmdBuf);
    }
}

} // namespace ya

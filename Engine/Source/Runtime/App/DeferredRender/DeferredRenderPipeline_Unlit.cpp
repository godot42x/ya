#include "DeferredRenderPipeline.h"

#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Material/MaterialFactory.h"

namespace ya
{

void DeferredRenderPipeline::initUnlit()
{
    auto* render = _render;

    // DSLs: set 1 = 2 textures, set 2 = param UBO
    auto dsls = IDescriptorSetLayout::create(
        render,
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
    _unlitMaterialResourceDSL = dsls[0];
    _unlitParamsDSL           = dsls[1];

    // Pipeline layout
    _unlitGBufferPPL = IPipelineLayout::create(
        render, "Deferred_Unlit_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(UnlitGBufferPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _unlitMaterialResourceDSL, _unlitParamsDSL});

    // Pipeline
    std::vector<EFormat::T> gBufferFormats = {SIGNED_LINEAR_FORMAT, SIGNED_LINEAR_FORMAT, LINEAR_FORMAT, SHADING_MODEL_FORMAT};

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {.label = "Unlit GBuffer Pass", .colorAttachmentFormats = gBufferFormats, .depthAttachmentFormat = DEPTH_FORMAT},
        .pipelineLayout        = _unlitGBufferPPL.get(),
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
    _unlitGBufferPipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_unlitGBufferPipeline && _unlitGBufferPipeline->recreate(ci), "Failed to create Unlit GBuffer pipeline");

    // Material pool
    const uint32_t texCount = static_cast<uint32_t>(_unlitMaterialResourceDSL->getLayoutInfo().bindings.size());
    _unlitMatPool.init(render, _unlitParamsDSL, _unlitMaterialResourceDSL, [texCount](uint32_t n) { return std::vector<DescriptorPoolSize>{
                                                                                                        {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                                                                                                        {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
                                                                                                    }; }, 16);
}

void DeferredRenderPipeline::prepareUnlit(Scene* scene)
{
    auto& reg = scene->getRegistry();

    uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<UnlitMaterial>());
    bool             force    = _unlitMatPool.ensureCapacity(matCount);
    std::vector<int> prepared(matCount, 0);

    auto flushOne = [&](UnlitMaterial* mat) {
        if (!mat || mat->getIndex() < 0) return;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());
        if (prepared[idx]) return;

        _unlitMatPool.flushDirty(
            mat,
            force,
            [](IBuffer* ubo, UnlitMaterial* m) {
                ubo->writeData(&m->getParams(), sizeof(UnlitParamUBO), 0);
            },
            [this](DescriptorSetHandle ds, UnlitMaterial* m) {
                _render->getDescriptorHelper()->updateDescriptorSets(
                    {
                        IDescriptorSetHelper::writeOneImage(ds, 0, m->getTextureBinding(UnlitMaterial::EResource::BaseColor0)),
                        IDescriptorSetHelper::writeOneImage(ds, 1, m->getTextureBinding(UnlitMaterial::EResource::BaseColor1)),
                    },
                    {});
            });
        prepared[idx] = 1;
    };

    for (const auto& [e, mc, tc, umc] : reg.view<MeshComponent, TransformComponent, UnlitMaterialComponent>().each()) {
        flushOne(umc.getMaterial());
    }

    // Fallback material
    flushOne(_fallbackMaterial);
}

void DeferredRenderPipeline::drawUnlit(ICommandBuffer* cmdBuf, Scene* scene)
{
    auto& reg = scene->getRegistry();

    cmdBuf->bindPipeline(_unlitGBufferPipeline.get());
    for (const auto& [entity, mc, tc, umc] :
         reg.view<MeshComponent, TransformComponent, UnlitMaterialComponent>().each()) {
        UnlitMaterial* mat = umc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());

        cmdBuf->bindDescriptorSets(
            _unlitGBufferPPL.get(),
            0,
            {
                _frameAndLightDS,
                _unlitMatPool.resourceDS(idx),
                _unlitMatPool.paramDS(idx),
            });
        UnlitGBufferPushConstant pc{.modelMat = tc.getTransform()};
        cmdBuf->pushConstants(_unlitGBufferPPL.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        mc.getMesh()->draw(cmdBuf);
    }
}

void DeferredRenderPipeline::drawFallback(ICommandBuffer* cmdBuf, Scene* scene)
{
    if (!_fallbackMaterial || _fallbackMaterial->getIndex() < 0) return;

    auto&    reg   = scene->getRegistry();
    uint32_t fbIdx = static_cast<uint32_t>(_fallbackMaterial->getIndex());
    bool     bound = false;

    for (const auto& [entity, mc, tc] : reg.view<MeshComponent, TransformComponent>(entt::exclude<RenderComponent>).each()) {

        if (!bound) {
            cmdBuf->bindPipeline(_unlitGBufferPipeline.get());
            cmdBuf->bindDescriptorSets(
                _unlitGBufferPPL.get(),
                0,
                {
                    _frameAndLightDS,
                    _unlitMatPool.resourceDS(fbIdx),
                    _unlitMatPool.paramDS(fbIdx),
                });
            bound = true;
        }

        UnlitGBufferPushConstant pc{.modelMat = tc.getTransform()};
        cmdBuf->pushConstants(_unlitGBufferPPL.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        mc.getMesh()->draw(cmdBuf);
    }
}

} // namespace ya

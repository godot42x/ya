#include "DeferredRenderPipeline.h"

#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Render/Material/MaterialFactory.h"

namespace ya
{

void DeferredRenderPipeline::initPhong()
{
    auto* render = _render;

    // DSLs: set 1 = 3 textures, set 2 = param UBO
    auto dsls = IDescriptorSetLayout::create(
        render,
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
    _phongMaterialResourceDSL = dsls[0];
    _phongParamsDSL           = dsls[1];

    // Pipeline layout
    _phongGBufferPPL = IPipelineLayout::create(
        render, "Deferred_Phong_GBuffer_PPL", {PushConstantRange{.offset = 0, .size = sizeof(PhongGBufferPushConstant), .stageFlags = EShaderStage::Vertex}}, {_frameAndLightDSL, _phongMaterialResourceDSL, _phongParamsDSL});

    // Pipeline
    std::vector<EFormat::T> gBufferFormats = {SIGNED_LINEAR_FORMAT, SIGNED_LINEAR_FORMAT, LINEAR_FORMAT, SHADING_MODEL_FORMAT};

    GraphicsPipelineCreateInfo ci{
        .pipelineRenderingInfo = {.label = "Phong GBuffer Pass", .colorAttachmentFormats = gBufferFormats, .depthAttachmentFormat = DEPTH_FORMAT},
        .pipelineLayout        = _phongGBufferPPL.get(),
        .shaderDesc            = ShaderDesc{
                       .shaderName        = "DeferredRender/Unified_GBufferPass_Phong.slang",
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
    _phongGBufferPipeline = IGraphicsPipeline::create(render);
    YA_CORE_ASSERT(_phongGBufferPipeline && _phongGBufferPipeline->recreate(ci), "Failed to create Phong GBuffer pipeline");

    // Material pool
    const uint32_t texCount = static_cast<uint32_t>(_phongMaterialResourceDSL->getLayoutInfo().bindings.size());
    _phongMatPool.init(
        render,
        _phongParamsDSL,
        _phongMaterialResourceDSL,
        [texCount](uint32_t n) {
            return std::vector<DescriptorPoolSize>{
                {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = n},
                {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = n * texCount},
            };
        },
        16);
}

void DeferredRenderPipeline::preparePhong(Scene* scene)
{
    auto& reg = scene->getRegistry();

    uint32_t         matCount = static_cast<uint32_t>(MaterialFactory::get()->getMaterialSize<PhongMaterial>());
    bool             force    = _phongMatPool.ensureCapacity(matCount);
    std::vector<int> prepared(matCount, 0);

    for (const auto& [e, mc, tc, pmc] :
         reg.view<MeshComponent, TransformComponent, PhongMaterialComponent>().each()) {
        PhongMaterial* mat = pmc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());
        if (prepared[idx]) continue;

        _phongMatPool.flushDirty(
            mat,
            force,
            [](IBuffer* ubo, PhongMaterial* m) {
                PhongParamUBO params{};
                const auto&   srcParams = m->getParams();
                params.ambient          = srcParams.ambient;
                params.diffuse          = srcParams.diffuse;
                params.specular         = srcParams.specular;
                params.shininess        = srcParams.shininess;
                const auto& src         = m->getParams().textureParams;
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
                        IDescriptorSetHelper::writeOneImage(ds, 2, m->getTextureBinding(PhongMaterial::EResource::ReflectionTexture)),
                        IDescriptorSetHelper::writeOneImage(ds, 3, m->getTextureBinding(PhongMaterial::EResource::NormalTexture)),
                    },
                    {});
            });
        prepared[idx] = 1;
    }
}

void DeferredRenderPipeline::drawPhong(ICommandBuffer* cmdBuf, Scene* scene)
{
    auto& reg = scene->getRegistry();

    cmdBuf->bindPipeline(_phongGBufferPipeline.get());
    for (const auto& [entity, mc, tc, pmc] :
         reg.view<MeshComponent, TransformComponent, PhongMaterialComponent>().each())
    {
        PhongMaterial* mat = pmc.getMaterial();
        if (!mat || mat->getIndex() < 0) continue;
        if (!mc.isResolved() || !mc.getMesh()) continue;
        uint32_t idx = static_cast<uint32_t>(mat->getIndex());

        cmdBuf->bindDescriptorSets(_phongGBufferPPL.get(), 0, {_frameAndLightDS, _phongMatPool.resourceDS(idx), _phongMatPool.paramDS(idx)});
        PhongGBufferPushConstant pc{.modelMat = tc.getTransform()};
        cmdBuf->pushConstants(_phongGBufferPPL.get(), EShaderStage::Vertex, 0, sizeof(pc), &pc);
        mc.getMesh()->draw(cmdBuf);
    }
}

} // namespace ya

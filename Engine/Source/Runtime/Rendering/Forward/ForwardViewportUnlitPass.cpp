#include "ForwardViewportUnlitPass.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Render.h"
#include "Runtime/Rendering/Forward/ForwardFrameResourceSet.h"

namespace ya
{

namespace
{

static const std::vector<VertexAttribute> kUnlitVertexAttributes3 = {
    {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
    {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
    {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
};

static const std::vector<VertexAttribute> kUnlitSkinningVertexAttributes = {
    {.bufferSlot = 1, .location = 4, .format = EVertexAttributeFormat::Int32x4, .offset = offsetof(ya::SkeletonMeshVertex, boneIDs)},
    {.bufferSlot = 1, .location = 5, .format = EVertexAttributeFormat::Float4, .offset = offsetof(ya::SkeletonMeshVertex, weights)},
};

static const VertexBufferDescription kUnlitVBDesc{.slot = 0, .pitch = sizeof(ya::Vertex)};

} // namespace

void ForwardViewportUnlitPass::init(const InitDesc& desc)
{
    _render                = desc.render;
    _skinningDSL           = desc.skinningDSL;
    _getFrameIndex         = desc.getFrameIndex;
    _getElapsedTimeSeconds = desc.getElapsedTimeSeconds;
    _unlitFrameDSL         = desc.unlitFrameDSL;
    initUnlit(desc);
}

void ForwardViewportUnlitPass::destroy()
{
    _unlitMatPool = {};
    _unlitStatic = {};
    _unlitSkinned = {};
    _unlitFrameDSL.reset();
    _unlitParamDSL.reset();
    _unlitResourceDSL.reset();

    _getFrameIndex = {};
    _getElapsedTimeSeconds = {};
    _skinningDSL.reset();
    _render = nullptr;
}

void ForwardViewportUnlitPass::beginFrame()
{
    if (_unlitStatic.pipeline) {
        _unlitStatic.pipeline->beginFrame();
    }
    if (_unlitSkinned.pipeline) {
        _unlitSkinned.pipeline->beginFrame();
    }
}

void ForwardViewportUnlitPass::refreshPipelineFormats(const RenderAttachmentFormats& formats)
{
    if (!formats.hasColor()) {
        return;
    }

    if (_unlitStatic.pipeline) {
        _unlitStatic.pipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
        _unlitStatic.pipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
        _unlitStatic.pipeline->updateDesc(_unlitStatic.pipelineCI);
    }

    if (_unlitSkinned.pipeline) {
        _unlitSkinned.pipelineCI.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
        _unlitSkinned.pipelineCI.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
        _unlitSkinned.pipeline->updateDesc(_unlitSkinned.pipelineCI);
    }
}

void ForwardViewportUnlitPass::prepare(const RenderStageContext& ctx,
                                       UnlitFrameUBO& outFrame)
{
    if (!ctx.frameData) {
        return;
    }
    prepareUnlit(ctx, outFrame);
}

void ForwardViewportUnlitPass::initUnlit(const InitDesc& desc)
{
    auto dsls = IDescriptorSetLayout::create(_render, {
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
    _unlitParamDSL    = dsls[0];
    _unlitResourceDSL = dsls[1];

    auto pipelineDsls = dsls;
    pipelineDsls.insert(pipelineDsls.begin(), _unlitFrameDSL);

    _unlitStatic.pipelineLayout = IPipelineLayout::create(
        _render, "FwdUnlit_Static_PPL", {PushConstantRange{.offset = 0, .size = sizeof(UnlitPC), .stageFlags = EShaderStage::Vertex}}, pipelineDsls);

    _unlitStatic.pipelineCI = GraphicsPipelineCreateInfo{
        .renderPass            = desc.renderPass,
        .pipelineRenderingInfo = desc.pipelineRenderingInfo,
        .pipelineLayout        = _unlitStatic.pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName        = "Test/Unlit.glsl",
            .vertexBufferDescs = {kUnlitVBDesc},
            .vertexAttributes  = kUnlitVertexAttributes3,
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

    auto skinnedDsls = pipelineDsls;
    skinnedDsls.push_back(_skinningDSL);
    _unlitSkinned.pipelineLayout = IPipelineLayout::create(
        _render, "FwdUnlit_Skinned_PPL", {PushConstantRange{.offset = 0, .size = sizeof(UnlitPC), .stageFlags = EShaderStage::Vertex}}, skinnedDsls);

    _unlitSkinned.pipelineCI                         = _unlitStatic.pipelineCI;
    _unlitSkinned.pipelineCI.pipelineLayout          = _unlitSkinned.pipelineLayout.get();
    _unlitSkinned.pipelineCI.shaderDesc.vertexBufferDescs = {
        VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)},
        VertexBufferDescription{.slot = 1, .pitch = sizeof(ya::SkeletonMeshVertex)},
    };
    _unlitSkinned.pipelineCI.shaderDesc.vertexAttributes = kUnlitVertexAttributes3;
    _unlitSkinned.pipelineCI.shaderDesc.vertexAttributes.insert(_unlitSkinned.pipelineCI.shaderDesc.vertexAttributes.end(), kUnlitSkinningVertexAttributes.begin(), kUnlitSkinningVertexAttributes.end());
    _unlitSkinned.pipelineCI.shaderDesc.defines = {"ENABLE_SKINNING 1", "SKINNING_SET_INDEX 3"};
    _unlitSkinned.pipeline = IGraphicsPipeline::create(_render);
    _unlitSkinned.pipeline->recreate(_unlitSkinned.pipelineCI);

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

void ForwardViewportUnlitPass::prepareUnlit(const RenderStageContext& ctx,
                                            UnlitFrameUBO& outFrame)
{
    const auto& fd = *ctx.frameData;
    uint32_t materialCount = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    if (_unlitMatPool.ensureCapacity(materialCount)) {
        _unlitPoolRecreated = true;
    }

    outFrame.projMat    = ctx.frameData->projection;
    outFrame.viewMat    = ctx.frameData->view;
    outFrame.resolution = glm::ivec2(ctx.viewportExtent.width, ctx.viewportExtent.height);
    outFrame.frameIdx   = _getFrameIndex ? static_cast<int32_t>(_getFrameIndex()) : 0;
    outFrame.time       = _getElapsedTimeSeconds ? static_cast<float>(_getElapsedTimeSeconds()) : 0.0f;

    prepareUnlitMaterials(fd);
    _unlitPoolRecreated = false;
}

void ForwardViewportUnlitPass::prepareUnlitMaterials(const RenderFrameData& fd)
{
    uint32_t          materialCount   = MaterialFactory::get()->getMaterialSize<UnlitMaterial>();
    std::vector<bool> preparedMaterial(materialCount);

    auto prepareBucket = [&](const std::vector<RenderDrawItem>& items)
    {
        for (const auto& item : items) {
            if (!item.material) continue;
            auto* material = static_cast<UnlitMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t matIdx = material->getIndex();
            if (preparedMaterial[matIdx]) continue;

            _unlitMatPool.flushDirty(
                material, _unlitPoolRecreated, [&](IBuffer* ubo, UnlitMaterial* mat)
                {
                    const auto& params = mat->getParams();
                    ubo->writeData(&params, sizeof(UnlitMaterial::ParamUBO), 0);
                },
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
                    }, {});
                });
            preparedMaterial[matIdx] = true;
        }
    };

    prepareBucket(fd.drawBuckets.staticMeshes.unlitDrawItems);
    prepareBucket(fd.drawBuckets.skinnedMeshes.unlitDrawItems);
}

void ForwardViewportUnlitPass::draw(const DrawContext& drawCtx)
{
    const auto& ctx          = drawCtx.stageCtx;
    const auto& fd           = *ctx.frameData;
    const auto& staticItems  = fd.drawBuckets.staticMeshes.unlitDrawItems;
    const auto& skinnedItems = fd.drawBuckets.skinnedMeshes.unlitDrawItems;
    auto*       cmdBuf       = ctx.cmdBuf;

    if (staticItems.empty() && skinnedItems.empty()) {
        return;
    }

    cmdBuf->debugBeginLabel("ForwardUnlit");
    drawCtx.setViewportAndScissor(cmdBuf, ctx.viewportExtent.width, ctx.viewportExtent.height);

    auto drawBucket = [&](const std::vector<RenderDrawItem>& items, bool bSkinned)
    {
        for (const auto& item : items) {
            if (!item.mesh || !item.material) continue;
            auto* material = static_cast<UnlitMaterial*>(item.material);
            if (material->getIndex() < 0) continue;

            uint32_t            matIdx     = material->getIndex();
            DescriptorSetHandle paramDS    = _unlitMatPool.paramDS(matIdx);
            DescriptorSetHandle resourceDS = _unlitMatPool.resourceDS(matIdx);

            auto& pipelineVariant = bSkinned ? _unlitSkinned : _unlitStatic;
            auto* layout = pipelineVariant.pipelineLayout.get();
            cmdBuf->bindPipeline(pipelineVariant.pipeline.get());
            if (bSkinned) {
                cmdBuf->bindDescriptorSets(layout, 0, {drawCtx.unlitFrameDescriptorSet, paramDS, resourceDS, drawCtx.skinningDS});
            }
            else {
                cmdBuf->bindDescriptorSets(layout, 0, {drawCtx.unlitFrameDescriptorSet, paramDS, resourceDS});
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
    cmdBuf->debugEndLabel();
}

} // namespace ya

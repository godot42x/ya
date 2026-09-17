#include "BasicPostprocessing.h"

#include "Core/Log.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Sampler.h"
#include "RHI/Render.h"
#include "RHI/Backend/TextureLibrary.h"

#include <algorithm>

namespace ya
{

namespace
{

constexpr uint32_t POSTPROCESS_FLAG_INVERSION    = 1u << 0;
constexpr uint32_t POSTPROCESS_FLAG_TONEMAPPING  = 1u << 1;
constexpr uint32_t POSTPROCESS_FLAG_GAMMA        = 1u << 2;
constexpr uint32_t POSTPROCESS_FLAG_RANDOM_GRAIN = 1u << 3;

} // namespace

void BasicPostprocessing::init(const InitDesc& initDesc)
{
    _render   = initDesc.render;
    _initDesc = initDesc;

    auto dsls        = IDescriptorSetLayout::create(_render, _pipelineLayoutDesc.descriptorSetLayouts);
    _dslInputTexture = dsls[0];

    _pipelineLayout = IPipelineLayout::create(
        _render,
        _pipelineLayoutDesc.label,
        _pipelineLayoutDesc.pushConstants,
        dsls);

    auto pipelineDesc = GraphicsPipelineCreateInfo{
        .renderPass            = initDesc.renderPass,
        .pipelineRenderingInfo = initDesc.pipelineRenderingInfo,
        .pipelineLayout        = _pipelineLayout.get(),
        .shaderDesc            = ShaderDesc{
            .shaderName = "Misc/BasicPostprocessing.slang",
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::None,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = false,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index        = 0,
                    .bBlendEnable = false,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {Viewport::defaults()},
            .scissors  = {Scissor::defaults()},
        },
    };
    _pipeline = IGraphicsPipeline::create(_render);
    _pipeline->recreate(pipelineDesc);
}

void BasicPostprocessing::shutdown()
{
    _dslInputTexture.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _render = nullptr;
}

void BasicPostprocessing::beginFrame()
{
    if (_pipeline) {
        _pipeline->beginFrame();
    }
}

void BasicPostprocessing::rebuildPushConstants(const PostProcessingState& state, bool bOutputIsSRGB)
{
    _pushConstants.flags = 0;
    if (state.bEnableInversion) {
        _pushConstants.flags |= POSTPROCESS_FLAG_INVERSION;
    }
    if (state.bEnableToneMapping) {
        _pushConstants.flags |= POSTPROCESS_FLAG_TONEMAPPING;
    }
    if (state.bEnableGammaCorrection && !bOutputIsSRGB) {
        _pushConstants.flags |= POSTPROCESS_FLAG_GAMMA;
    }
    if (state.bEnableRandomGrain) {
        _pushConstants.flags |= POSTPROCESS_FLAG_RANDOM_GRAIN;
    }

    _pushConstants.grayscaleMode    = static_cast<uint32_t>(state.grayscaleMode);
    _pushConstants.kernelMode       = static_cast<uint32_t>(state.kernelMode);
    _pushConstants.toneMappingCurve = static_cast<uint32_t>(state.toneMappingCurve);
    _pushConstants.params0          = glm::vec4(
        std::max(state.gamma, 0.001f),
        std::max(state.kernelTexelOffset, 0.000001f),
        std::max(state.randomGrainStrength, 0.0f),
        std::max(state.exposure, 0.0f));
}

void BasicPostprocessing::writeInput(DescriptorSetHandle set, IImageView* inputImageView)
{
    if (!_render || !set || !inputImageView) {
        return;
    }
    static auto sampler = TextureLibrary::get().getDefaultSampler();
    DescriptorImageInfo imageInfo(
        inputImageView->getHandle(),
        sampler->getHandle(),
        EImageLayout::ShaderReadOnlyOptimal);
    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(
                set,
                0,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                {imageInfo}),
        },
        {});
}

void BasicPostprocessing::render(const RenderDesc& desc)
{
    if (!desc.cmdBuf || !desc.inputImageView || !desc.state) {
        return;
    }
    if (desc.renderExtent.width == 0 || desc.renderExtent.height == 0) {
        return;
    }

    const DescriptorSetHandle viewSet = desc.toneMap.input.set;
    if (!viewSet) {
        YA_CORE_ERROR("BasicPostprocessing refuses to bind a null input descriptor set");
        return;
    }
    writeInput(viewSet, desc.inputImageView);
    rebuildPushConstants(*desc.state, desc.bOutputIsSRGB);

    desc.cmdBuf->bindPipeline(_pipeline.get());
    desc.cmdBuf->setViewport(0, 0, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
    desc.cmdBuf->setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
    desc.cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {viewSet}, {});
    desc.cmdBuf->pushConstants(
        _pipelineLayout.get(),
        _pipelineLayoutDesc.pushConstants[0].stageFlags,
        _pipelineLayoutDesc.pushConstants[0].offset,
        _pipelineLayoutDesc.pushConstants[0].size,
        &_pushConstants);
    desc.cmdBuf->draw(3, 1, 0, 0);
}

} // namespace ya

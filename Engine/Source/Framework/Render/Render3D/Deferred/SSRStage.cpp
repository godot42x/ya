#include "Render3D/Deferred/SSRStage.h"

#include "Core/Log.h"
#include "Core/Profiling/Instrumentor.h"
#include "RHI/Backend/TextureLibrary.h"
#include "RHI/Render.h"

namespace ya
{

void SSRStage::init(IRender* render)
{
    _render = render;

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSR_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });
    YA_CORE_ASSERT(_frameDSL != nullptr, "SSRStage requires a frame descriptor layout");

    _inputDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSR_Input_DSL",
            .set      = 1,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        });

    _pipelineLayout = IPipelineLayout::create(
        _render,
        "Deferred_SSR_PPL",
        {},
        {_frameDSL, _inputDSL});

    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                  = "Deferred SSR Pass",
            .colorAttachmentFormats = {OUTPUT_FORMAT},
            .depthAttachmentFormat  = EFormat::Undefined,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc     = ShaderDesc{.shaderName = "DeferredRender/SSR.slang"},
        .dynamicFeatures = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType   = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = false, .bDepthWriteEnable = false},
        .colorBlendState    = {.attachments = {ColorBlendAttachmentState{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    }), "Failed to create SSR pipeline");
}

void SSRStage::destroy()
{
    _inputDSL.reset();
    _frameDSL.reset();
    _pipeline.reset();
    _pipelineLayout.reset();

    _render = nullptr;
}

void SSRStage::refreshPipelineFormat(EFormat::T colorFormat)
{
    if (!_pipeline) {
        return;
    }

    auto ci                                         = _pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = {colorFormat};
    ci.pipelineRenderingInfo.depthAttachmentFormat  = EFormat::Undefined;
    _pipeline->updateDesc(std::move(ci));
}

SSRStage::FrameData SSRStage::buildFrameData(const RenderFrameData& frameData,
                                             Extent2D               viewExtent,
                                             float                  maxDistance,
                                             float                  thickness,
                                             float                  intensity,
                                             uint32_t               stepCount,
                                             bool                   bReverseY)
{
    FrameData data{};
    data.screenResolution = {static_cast<int32_t>(viewExtent.width), static_cast<int32_t>(viewExtent.height)};
    data.maxDistance      = maxDistance;
    data.thickness        = thickness;
    data.intensity        = intensity;
    data.stepCount        = stepCount;
    data.reverseY         = bReverseY ? 1u : 0u;
    data.fadeEdge         = 0.1f;
    data.projectMat       = frameData.projection;
    data.invProjectMat    = glm::inverse(frameData.projection);
    data.viewMat          = frameData.view;
    return data;
}

void SSRStage::beginFrame()
{
    YA_PROFILE_FUNCTION();
    if (_pipeline) {
        _pipeline->beginFrame();
    }
}

void SSRStage::execute(const RenderStageContext& ctx)
{
    (void)ctx;
    // SSR is recorded exclusively by SSRFeature's graph pass.
}

void SSRStage::writeInputDescriptors(
    DescriptorSetHandle                          inputDS,
    const RGRenderContext::RGPassBindingContext& binding,
    RGTextureHandle                              sceneColor,
    RGTextureHandle                              normal,
    RGTextureHandle                              depth) const
{
    if (!_render || !inputDS) {
        return;
    }
    auto sampler = TextureLibrary::get().getDefaultSampler();
    const auto sceneInfo = binding.resolveTextureDescriptor(sceneColor, sampler.get());
    const auto normalInfo = binding.resolveTextureDescriptor(normal, sampler.get());
    const auto depthInfo  = binding.resolveTextureDescriptor(depth, sampler.get());
    YA_CORE_ASSERT(sceneInfo && normalInfo && depthInfo, "SSR pass failed to resolve input textures");
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genImageWrite(inputDS, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {*sceneInfo}),
        IDescriptorSetHelper::genImageWrite(inputDS, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {*normalInfo}),
        IDescriptorSetHelper::genImageWrite(inputDS, 2, 0, EPipelineDescriptorType::CombinedImageSampler, {*depthInfo}),
    });
}

} // namespace ya

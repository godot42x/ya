#include "Render3D/Deferred/SSAOStage.h"

#include "Graph/RenderGraphImportUtils.h"
#include "Core/Profiling/Instrumentor.h"

#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "RHI/Backend/TextureLibrary.h"

#include <array>
#include <vector>

namespace ya
{

namespace
{

std::array<ColorRGBA<uint8_t>, 16> buildNoisePixels()
{
    return {
        ColorRGBA<uint8_t>{191, 115, 128, 255},
        ColorRGBA<uint8_t>{64, 191, 128, 255},
        ColorRGBA<uint8_t>{223, 159, 128, 255},
        ColorRGBA<uint8_t>{96,  32, 128, 255},
        ColorRGBA<uint8_t>{159, 223, 128, 255},
        ColorRGBA<uint8_t>{32,  96, 128, 255},
        ColorRGBA<uint8_t>{207, 64, 128, 255},
        ColorRGBA<uint8_t>{80,  175, 128, 255},
        ColorRGBA<uint8_t>{239, 128, 128, 255},
        ColorRGBA<uint8_t>{112, 207, 128, 255},
        ColorRGBA<uint8_t>{175, 48, 128, 255},
        ColorRGBA<uint8_t>{48,  143, 128, 255},
        ColorRGBA<uint8_t>{223, 96, 128, 255},
        ColorRGBA<uint8_t>{96,  223, 128, 255},
        ColorRGBA<uint8_t>{143, 80, 128, 255},
        ColorRGBA<uint8_t>{16,  159, 128, 255},
    };
}

} // namespace

void SSAOStage::refreshPipelineFormat()
{
    if (!_pipeline) {
        return;
    }

    auto ci                                         = _pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = {AO_FORMAT};
    ci.pipelineRenderingInfo.depthAttachmentFormat  = EFormat::Undefined;
    _pipeline->updateDesc(std::move(ci));
}

void SSAOStage::initNoiseTexture()
{
    auto noisePixels = buildNoisePixels();
    _noiseTexture = Texture::fromData(*_render, 4, 4, std::vector<ColorRGBA<uint8_t>>(noisePixels.begin(), noisePixels.end()), "ssao-noise");
}

void SSAOStage::init(IRender* render)
{
    _render = render;

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSAO_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });
    YA_CORE_ASSERT(_frameDSL != nullptr, "SSAOStage requires a frame descriptor layout");

    _inputDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSAO_Input_DSL",
            .set      = 1,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        });

    _pipelineLayout = IPipelineLayout::create(
        _render,
        "Deferred_SSAO_PPL",
        {},
        {_frameDSL, _inputDSL});

    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                  = "Deferred SSAO Pass",
            .colorAttachmentFormats = {AO_FORMAT},
            .depthAttachmentFormat  = EFormat::Undefined,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc     = ShaderDesc{.shaderName = "DeferredRender/SSAO.slang"},
        .dynamicFeatures = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType   = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = false, .bDepthWriteEnable = false},
        .colorBlendState    = {.attachments = {ColorBlendAttachmentState{.index = 0, .bBlendEnable = false, .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A}}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    }), "Failed to create SSAO pipeline");

    initNoiseTexture();
}

void SSAOStage::destroy()
{
    _noiseTexture.reset();
    _inputDSL.reset();
    _frameDSL.reset();
    _pipeline.reset();
    _pipelineLayout.reset();

    _render           = nullptr;
}

SSAOStage::FrameData SSAOStage::buildFrameData(const RenderFrameData& frameData,
                                               Extent2D               viewExtent,
                                               float                  radius,
                                               float                  bias,
                                               float                  power,
                                               float                  intensity,
                                               bool                   bReverseY)
{
    FrameData data{};
    data.screenResolution = {static_cast<int32_t>(viewExtent.width), static_cast<int32_t>(viewExtent.height)};
    data.radius           = radius;
    data.bias             = bias;
    data.power            = power;
    data.intensity        = intensity;
    data.reverseY         = bReverseY ? 1u : 0u;
    data.projectMat       = frameData.projection;
    data.invProjectMat    = glm::inverse(frameData.projection);
    data.viewMat          = frameData.view;
    return data;
}

void SSAOStage::beginFrame()
{
    YA_PROFILE_FUNCTION();
    if (_pipeline) {
        _pipeline->beginFrame();
    }
}

void SSAOStage::execute(const RenderStageContext& ctx)
{
    (void)ctx;
    // SSAO is recorded exclusively by SSAOFeature's graph pass.
}

void SSAOStage::writeInputDescriptors(
    DescriptorSetHandle                          inputDS,
    const RGRenderContext::RGPassBindingContext& binding,
    RGTextureHandle                              albedo,
    RGTextureHandle                              normal,
    RGTextureHandle                              depth,
    RGTextureHandle                              noise) const
{
    if (!_render || !inputDS) {
        return;
    }
    auto sampler = TextureLibrary::get().getDefaultSampler();
    const auto albedoInfo = binding.resolveTextureDescriptor(albedo, sampler.get());
    const auto normalInfo = binding.resolveTextureDescriptor(normal, sampler.get());
    const auto depthInfo  = binding.resolveTextureDescriptor(depth, sampler.get());
    const auto noiseInfo  = binding.resolveTextureDescriptor(noise, sampler.get());
    YA_CORE_ASSERT(albedoInfo && normalInfo && depthInfo && noiseInfo,
                   "SSAO pass failed to resolve input textures");
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genImageWrite(inputDS, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {*albedoInfo}),
        IDescriptorSetHelper::genImageWrite(inputDS, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {*normalInfo}),
        IDescriptorSetHelper::genImageWrite(inputDS, 2, 0, EPipelineDescriptorType::CombinedImageSampler, {*depthInfo}),
        IDescriptorSetHelper::genImageWrite(inputDS, 3, 0, EPipelineDescriptorType::CombinedImageSampler, {*noiseInfo}),
    });
}

} // namespace ya

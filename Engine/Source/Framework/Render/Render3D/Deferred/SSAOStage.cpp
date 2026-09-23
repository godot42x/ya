#include "Render3D/Deferred/SSAOStage.h"

#include "Render3D/Deferred/DeferredFrameGraphPasses.h"
#include "Graph/RenderGraphImportUtils.h"
#include "Core/Profiling/Instrumentor.h"

#include "Core/Config/ConfigManager.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "RHI/Backend/TextureLibrary.h"

#include <array>
#include <vector>

namespace ya
{

namespace
{

constexpr const char* SSAO_CONFIG_DOC_NAME  = "runtime";
constexpr const char* SSAO_CONFIG_KEY_RADIUS = "render.deferred.ssao.radius";
constexpr const char* SSAO_CONFIG_KEY_BIAS   = "render.deferred.ssao.bias";
constexpr const char* SSAO_CONFIG_KEY_POWER  = "render.deferred.ssao.power";
constexpr const char* SSAO_CONFIG_KEY_INTENSITY = "render.deferred.ssao.intensity";

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

RGImportedTextureDesc makeSSAOImportedTextureDesc(const std::shared_ptr<ImageResource>& resource,
                                                  std::string_view                    label,
                                                  EImageLayout::T                     finalLayout)
{
    return makeImportedTextureDesc(resource, label, finalLayout);
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

void SSAOStage::setSettings(float radius, float bias, float power, float intensity, bool bReverseY)
{
    _radius     = radius;
    _bias       = bias;
    _power      = power;
    _intensity  = intensity;
    _bReverseY  = bReverseY;
}

void SSAOStage::initNoiseTexture()
{
    auto noisePixels = buildNoisePixels();
    _noiseTexture = Texture::fromData(*_render, 4, 4, std::vector<ColorRGBA<uint8_t>>(noisePixels.begin(), noisePixels.end()), "ssao-noise");
}

void SSAOStage::init(IRender* render, stdptr<IDescriptorSetLayout> frameDSL)
{
    _render = render;

    auto& config = ConfigManager::get();
    _radius = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_RADIUS, _radius);
    _bias   = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_BIAS, _bias);
    _power  = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_POWER, _power);
    _intensity = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_INTENSITY, _intensity);

    _frameDSL = std::move(frameDSL);
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

SSAOStage::FrameData SSAOStage::buildFrameData(const RenderStageContext& ctx) const
{
    YA_CORE_ASSERT(ctx.frameData != nullptr, "SSAOStage requires frame data to build frame parameters");

    FrameData frameData{};
    frameData.screenResolution = {static_cast<int32_t>(ctx.viewExtent.width), static_cast<int32_t>(ctx.viewExtent.height)};
    frameData.radius           = _radius;
    frameData.bias             = _bias;
    frameData.power            = _power;
    frameData.intensity        = _intensity;
    frameData.reverseY         = _bReverseY ? 1u : 0u;
    frameData.projectMat       = ctx.frameData->projection;
    frameData.invProjectMat    = glm::inverse(ctx.frameData->projection);
    frameData.viewMat          = ctx.frameData->view;
    return frameData;
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
    // SSAO is recorded exclusively by DeferredFrameGraphOrchestrator.
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

RGTextureHandle SSAOStage::appendGraphPass(RenderGraph& graph,
                                           const RenderStageContext& ctx,
                                           const DeferredSSAOPassParams& params,
                                           RGTextureHandle output)
{
    YA_CORE_ASSERT(_noiseTexture != nullptr, "SSAOStage requires initialized noise texture before graph pass append");

    const auto  noise = graph.importTexture(makeSSAOImportedTextureDesc(_noiseTexture ? _noiseTexture->getResourceShared() : nullptr, "SSAO.Noise", EImageLayout::ShaderReadOnlyOptimal));
    YA_CORE_ASSERT(output.isValid(), "SSAOStage requires a prepared output target");

    [[maybe_unused]] const auto pass = graph.addPass(
        makeViewGraphName("SSAO Pass", params.viewId),
        [params, noise, output, viewExtent = ctx.viewExtent](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(params.frame, params.frameRange);
            passBuilder.read(params.albedo);
            passBuilder.read(params.normal);
            passBuilder.read(params.depth);
            passBuilder.read(noise);
            passBuilder.declareRaster({
                .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = glm::vec2(viewExtent.width, viewExtent.height)},
                .layerCount  = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ClearValue(1.0f, 1.0f, 1.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [this, params, noise](RGRenderContext& rgCtx) {
            writeInputDescriptors(
                params.inputDescriptorSet,
                rgCtx.getBindingContext(),
                params.albedo,
                params.normal,
                params.depth,
                noise);

            const auto rasterParams   = rgCtx.getRasterPassExecutionParams();
            const auto renderExtent   = rasterParams.getRenderExtent();
            const auto viewWidth  = renderExtent.width;
            const auto viewHeight = renderExtent.height;
            rgCtx.beginDeclaredRasterRendering();

            rgCtx.getCommandBuffer().bindPipeline(_pipeline.get());
            rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(viewWidth), static_cast<float>(viewHeight));
            rgCtx.getCommandBuffer().setScissor(0, 0, viewWidth, viewHeight);
            rgCtx.getCommandBuffer().bindDescriptorSets(
                _pipelineLayout.get(), 0, {params.frameDescriptorSet, params.inputDescriptorSet});
            rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
            rgCtx.endRendering();
        });

    return output;
}

} // namespace ya

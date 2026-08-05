#include "SSAOStage.h"

#include "Core/Profiling/Instrumentor.h"

#include "Config/ConfigManager.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Resource/Texture/TextureLibrary.h"

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

RGImportedTextureDesc makeSSAOImportedTextureDesc(const Texture& texture,
                                                  std::string_view label,
                                                  EImageLayout::T finalLayout)
{
    return makeImportedTextureDesc(texture, label, finalLayout);
}

RGImportedTextureDesc makeSSAOImportedTextureDesc(const RenderImage& image,
                                                  std::string_view label,
                                                  EImageLayout::T finalLayout)
{
    return makeImportedTextureDesc(image, label, finalLayout);
}

} // namespace

void SSAOStage::setup(const DeferredGBufferResources& gBufferResources)
{
    _gBufferResources = gBufferResources;
}

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

    _descriptorPool = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
        .label     = "Deferred_SSAO_DSP",
        .maxSets   = 1,
        .poolSizes = {
            {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 4},
        },
    });

    _inputDS = _descriptorPool->allocateDescriptorSets(_inputDSL);

    initNoiseTexture();
}

void SSAOStage::destroy()
{
    _noiseTexture.reset();
    _descriptorPool.reset();
    _inputDSL.reset();
    _frameDSL.reset();
    _frameInputs = {};
    _pipeline.reset();
    _pipelineLayout.reset();

    _render                       = nullptr;
    _gBufferResources             = {};
    _lastInputDescriptorWriteCount = 0;
}

SSAOStage::FrameData SSAOStage::buildFrameData(const RenderStageContext& ctx) const
{
    YA_CORE_ASSERT(ctx.frameData != nullptr, "SSAOStage requires frame data to build frame parameters");

    FrameData frameData{};
    frameData.screenResolution = {static_cast<int32_t>(ctx.viewportExtent.width), static_cast<int32_t>(ctx.viewportExtent.height)};
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

void SSAOStage::prepare(const RenderStageContext& ctx)
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

RGTextureHandle SSAOStage::appendGraphPass(RenderGraph& graph,
                                           const RenderStageContext& ctx,
                                           const DeferredSSAOPassParams& params)
{
    YA_CORE_ASSERT(_noiseTexture != nullptr, "SSAOStage requires initialized noise texture before graph pass append");

    const auto  noise = graph.importTexture(makeSSAOImportedTextureDesc(*_noiseTexture, "SSAO.Noise", EImageLayout::ShaderReadOnlyOptimal));
    const auto  output = graph.createPersistentTexture(RGTextureDesc{
         .label  = "SSAO.Output",
         .format = AO_FORMAT,
         .extent = Extent3D{ctx.viewportExtent.width, ctx.viewportExtent.height, 1},
         .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, RGPersistentTextureKey{.value = "SSAO.Output"});

    [[maybe_unused]] const auto pass = graph.addPass(
        "SSAO Pass",
        [&params, noise, output, viewportExtent = ctx.viewportExtent](RGPassBuilder& passBuilder) {
            passBuilder.uniformRead(params.frame, params.frameRange);
            passBuilder.read(params.albedo);
            passBuilder.read(params.normal);
            passBuilder.read(params.depth);
            passBuilder.read(noise);
            passBuilder.declareRaster({
                .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = glm::vec2(viewportExtent.width, viewportExtent.height)},
                .layerCount  = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ClearValue(1.0f, 1.0f, 1.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [this, &params, noise](RGRenderContext& rgCtx) {
            // FG-502: set 1 (input DS) is written each pass from the graph
            // binding context, so no image-view handles are cached across
            // frames. Resolved image/view owners are retained on the command
            // buffer by the binding context.
            const auto binding = rgCtx.getBindingContext();
            auto sampler       = TextureLibrary::get().getDefaultSampler();
            const auto albedo = binding.resolveTextureDescriptor(params.albedo, sampler.get());
            const auto normal = binding.resolveTextureDescriptor(params.normal, sampler.get());
            const auto depth  = binding.resolveTextureDescriptor(params.depth, sampler.get());
            const auto noiseDescriptor = binding.resolveTextureDescriptor(noise, sampler.get());
            YA_CORE_ASSERT(albedo && normal && depth && noiseDescriptor,
                           "SSAO pass failed to resolve input textures");
            _render->getDescriptorHelper()->updateDescriptorSets({
                IDescriptorSetHelper::genImageWrite(_inputDS, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {*albedo}),
                IDescriptorSetHelper::genImageWrite(_inputDS, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {*normal}),
                IDescriptorSetHelper::genImageWrite(_inputDS, 2, 0, EPipelineDescriptorType::CombinedImageSampler, {*depth}),
                IDescriptorSetHelper::genImageWrite(_inputDS, 3, 0, EPipelineDescriptorType::CombinedImageSampler, {*noiseDescriptor}),
            });
            _lastInputDescriptorWriteCount = 4;

            const auto rasterParams  = rgCtx.getRasterPassExecutionParams();
            const auto renderExtent  = rasterParams.getRenderExtent();
            const auto viewportWidth = renderExtent.width;
            const auto viewportHeight = renderExtent.height;
            rgCtx.beginDeclaredRasterRendering();

            rgCtx.getCommandBuffer().bindPipeline(_pipeline.get());
            rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
            rgCtx.getCommandBuffer().setScissor(0, 0, viewportWidth, viewportHeight);
            rgCtx.getCommandBuffer().bindDescriptorSets(_pipelineLayout.get(), 0, {params.frameDescriptorSet, _inputDS});
            rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
            rgCtx.endRendering();
        });

    return output;
}

} // namespace ya

#include "SSAOStage.h"

#include "Core/Profiling/Instrumentor.h"

#include "Config/ConfigManager.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderGraphImportUtils.h"
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
    invalidateInputDescriptors();
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

void SSAOStage::invalidateInputDescriptors()
{
    _lastGBufferImageViewHandles.fill(nullptr);
    _lastGBufferDepthImageViewHandle = nullptr;
    _bInputDescriptorsInitialized    = false;
    _lastInputDescriptorWriteCount   = 0;
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
    _noiseTexture = Texture::fromData(4, 4, std::vector<ColorRGBA<uint8_t>>(noisePixels.begin(), noisePixels.end()), "ssao-noise");
}

void SSAOStage::init(IRender* render, stdptr<IDescriptorSetLayout> frameDSL)
{
    _render = render;
    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());

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
    _graphExecutor.reset();
    _noiseTexture.reset();
    _descriptorPool.reset();
    _inputDSL.reset();
    _frameDSL.reset();
    _frameInputs = {};
    _pipeline.reset();
    _pipelineLayout.reset();

    _render                       = nullptr;
    _gBufferResources             = {};
    _lastGBufferImageViewHandles.fill(nullptr);
    _lastGBufferDepthImageViewHandle = nullptr;
    _bInputDescriptorsInitialized = false;
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

void SSAOStage::updateInputDescriptors()
{
    if (!_gBufferResources.isComplete() || !_noiseTexture) {
        return;
    }

    auto* albedo = _gBufferResources.color[0];
    auto* normal = _gBufferResources.color[1];
    auto* depth  = _gBufferResources.depth;
    if (!albedo || !normal || !depth) {
        return;
    }

    const std::array<ImageViewHandle, 4> gbufferImageViewHandles = {
        albedo->getImageView() ? albedo->getImageView()->getHandle() : ImageViewHandle{},
        normal->getImageView() ? normal->getImageView()->getHandle() : ImageViewHandle{},
        _gBufferResources.color[2] && _gBufferResources.color[2]->getImageView() ? _gBufferResources.color[2]->getImageView()->getHandle() : ImageViewHandle{},
        _gBufferResources.color[3] && _gBufferResources.color[3]->getImageView() ? _gBufferResources.color[3]->getImageView()->getHandle() : ImageViewHandle{},
    };
    const auto depthHandle = depth->getImageView() ? depth->getImageView()->getHandle() : ImageViewHandle{};
    if (_bInputDescriptorsInitialized &&
        _lastGBufferImageViewHandles == gbufferImageViewHandles &&
        _lastGBufferDepthImageViewHandle == depthHandle) {
        _lastInputDescriptorWriteCount = 0;
        return;
    }

    auto sampler = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_inputDS, 0, albedo->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 1, normal->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 2, depth->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 3, _noiseTexture->getImageView(), sampler.get()),
    });

    _lastGBufferImageViewHandles     = gbufferImageViewHandles;
    _lastGBufferDepthImageViewHandle = depthHandle;
    _bInputDescriptorsInitialized    = true;
    _lastInputDescriptorWriteCount   = 4;
}

void SSAOStage::updateInputDescriptors(const RenderImage* albedo,
                                       const RenderImage* normal,
                                       const RenderImage* depth)
{
    if (!_render || !_inputDS || !_noiseTexture || !albedo || !normal || !depth) {
        return;
    }

    auto sampler = TextureLibrary::get().getDefaultSampler();
    const auto albedoHandle = albedo->getImageView() ? albedo->getImageView()->getHandle() : ImageViewHandle{};
    const auto normalHandle = normal->getImageView() ? normal->getImageView()->getHandle() : ImageViewHandle{};
    const auto depthHandle  = depth->getImageView() ? depth->getImageView()->getHandle() : ImageViewHandle{};
    if (_bInputDescriptorsInitialized &&
        _lastGBufferImageViewHandles[0] == albedoHandle &&
        _lastGBufferImageViewHandles[1] == normalHandle &&
        _lastGBufferDepthImageViewHandle == depthHandle) {
        _lastInputDescriptorWriteCount = 0;
        return;
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_inputDS, 0, albedo->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 1, normal->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 2, depth->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 3, _noiseTexture->getImageView(), sampler.get()),
    });

    _lastGBufferImageViewHandles     = {albedoHandle, normalHandle, {}, {}};
    _lastGBufferDepthImageViewHandle = depthHandle;
    _bInputDescriptorsInitialized    = true;
    _lastInputDescriptorWriteCount   = 4;
}

void SSAOStage::prepare(const RenderStageContext& ctx)
{
    YA_PROFILE_FUNCTION();
    if (_pipeline) {
        _pipeline->beginFrame();
    }
    if (!ctx.frameData || !_frameInputs.isValid()) {
        return;
    }

    updateInputDescriptors();
}

void SSAOStage::execute(const RenderStageContext& ctx)
{
    YA_PROFILE_FUNCTION();
    if (!ctx.cmdBuf || !_pipeline || !_gBufferResources.isComplete()) {
        return;
    }

    auto* gbufferAlbedo = _gBufferResources.color[0];
    auto* gbufferNormal = _gBufferResources.color[1];
    auto* gbufferDepth  = _gBufferResources.depth;
    if (!gbufferAlbedo || !gbufferNormal || !gbufferDepth || !_noiseTexture) {
        return;
    }

    ICommandBuffer::LabelScope labelScope(ctx.cmdBuf, "SSAOStage");

    RenderGraph graph;
    const auto  albedo = graph.importTexture(makeSSAOImportedTextureDesc(*gbufferAlbedo, "SSAO.GBufferAlbedo", EImageLayout::ShaderReadOnlyOptimal));
    const auto  normal = graph.importTexture(makeSSAOImportedTextureDesc(*gbufferNormal, "SSAO.GBufferNormal", EImageLayout::ShaderReadOnlyOptimal));
    const auto  depth = graph.importTexture(makeSSAOImportedTextureDesc(*gbufferDepth, "SSAO.GBufferDepth", EImageLayout::ShaderReadOnlyOptimal));
    YA_CORE_ASSERT(_frameInputs.isValid(), "SSAOStage standalone execution requires a frame-resource binding");
    const auto frameBuffer = graph.importBuffer(RGImportedBufferDesc{
        .desc = RGBufferDesc{
            .label = "SSAO.FrameUBO",
            .usage = EBufferUsage::UniformBuffer,
            .size  = _frameInputs.frame.buffer->getSize(),
        },
        .buffer = _frameInputs.frame.buffer.get(),
        .initialState = BufferResourceState{
            .stages = EPipelineStage::Host,
            .access = EResourceAccess::HostWrite,
            .offset = _frameInputs.frame.offset,
            .size   = _frameInputs.frame.size,
        },
        .retainedResources = {_frameInputs.frame.buffer},
    });
    const auto output = appendGraphPass(
        graph,
        ctx,
        DeferredSSAOPassParams{
            .frame      = frameBuffer,
            .frameRange = RGBufferRange{.offset = _frameInputs.frame.offset, .size = _frameInputs.frame.size},
            .albedo     = albedo,
            .normal     = normal,
            .depth      = depth,
            .frameDescriptorSet = _frameInputs.descriptorSet,
        });

    YA_CORE_ASSERT(_graphExecutor != nullptr, "SSAOStage graph executor is not initialized");
    [[maybe_unused]] const bool bExecuted = _graphExecutor->execute(graph, *ctx.cmdBuf);
    (void)output;
    (void)bExecuted;
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
        [this, &params](RGRenderContext& rgCtx) {
            // FG-103 resolve validation + FG-303: GBuffer inputs resolve from the
            // graph pass instead of a resolved-image snapshot stored on the stage.
            [[maybe_unused]] const RenderImage* albedo = rgCtx.resolveTexture(params.albedo);
            [[maybe_unused]] const RenderImage* normal = rgCtx.resolveTexture(params.normal);
            [[maybe_unused]] const RenderImage* depth  = rgCtx.resolveTexture(params.depth);
            updateInputDescriptors(albedo, normal, depth);

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

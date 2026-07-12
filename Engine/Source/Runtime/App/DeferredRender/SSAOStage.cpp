#include "SSAOStage.h"

#include "Core/Profiling/Instrumentor.h"

#include "Config/ConfigManager.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Resource/Texture/TextureLibrary.h"

#include "imgui.h"

#include <array>
#include <format>
#include <vector>

namespace ya
{

namespace
{

constexpr const char* SSAO_CONFIG_DOC_NAME  = "editor";
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

} // namespace

void SSAOStage::setup(IRenderTarget* gBufferRT, RenderImage* targetTexture)
{
    if (_gBufferRT) {
        _gBufferRT->onFramebufferRecreated.removeAll(this);
    }

    _gBufferRT      = gBufferRT;
    _targetTexture  = targetTexture;
    invalidateInputDescriptors();

    if (_gBufferRT) {
        _gBufferRT->onFramebufferRecreated.addLambda(this, [this]() {
            invalidateInputDescriptors();
        });
    }
}

void SSAOStage::refreshPipelineFormat()
{
    if (!_pipeline || !_targetTexture) {
        return;
    }

    auto ci                                         = _pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = {_targetTexture->getFormat()};
    ci.pipelineRenderingInfo.depthAttachmentFormat  = EFormat::Undefined;
    _pipeline->updateDesc(std::move(ci));
}

void SSAOStage::invalidateInputDescriptors()
{
    _lastGBufferFrameBuffer          = nullptr;
    _lastTargetImageViewHandle       = _targetTexture && _targetTexture->getImageView() ? _targetTexture->getImageView()->getHandle() : ImageViewHandle{};
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

void SSAOStage::init(IRender* render)
{
    _render = render;

    auto& config = ConfigManager::get();
    _radius = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_RADIUS, _radius);
    _bias   = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_BIAS, _bias);
    _power  = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_POWER, _power);
    _intensity = config.getOr<float>(SSAO_CONFIG_DOC_NAME, SSAO_CONFIG_KEY_INTENSITY, _intensity);

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSAO_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });

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
        .maxSets   = MAX_FLIGHTS_IN_FLIGHT + 1,
        .poolSizes = {
            {.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT},
            {.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 4},
        },
    });

    for (uint32_t i = 0; i < MAX_FLIGHTS_IN_FLIGHT; ++i) {
        _frameUBO[i] = _render->getResourceFactory()->createBuffer(BufferCreateInfo{
            .label       = std::format("Deferred_SSAO_Frame_UBO_{}", i),
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(FrameData),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        _frameDS[i] = _descriptorPool->allocateDescriptorSets(_frameDSL);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneUniformBuffer(_frameDS[i], 0, _frameUBO[i].get()),
        });
    }

    _inputDS = _descriptorPool->allocateDescriptorSets(_inputDSL);

    initNoiseTexture();
}

void SSAOStage::destroy()
{
    if (_gBufferRT) {
        _gBufferRT->onFramebufferRecreated.removeAll(this);
    }

    _noiseTexture.reset();
    for (auto& buffer : _frameUBO) {
        buffer.reset();
    }
    _descriptorPool.reset();
    _inputDSL.reset();
    _frameDSL.reset();
    _pipeline.reset();
    _pipelineLayout.reset();

    _render        = nullptr;
    _gBufferRT     = nullptr;
    _targetTexture = nullptr;
    _lastGBufferFrameBuffer = nullptr;
    _lastTargetImageViewHandle = nullptr;
    _bInputDescriptorsInitialized = false;
    _lastInputDescriptorWriteCount = 0;
}

void SSAOStage::updateFrameUBO(const RenderStageContext& ctx)
{
    if (ctx.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return;
    }

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

    auto& frameUBO = _frameUBO[ctx.flightIndex];
    frameUBO->writeData(&frameData, sizeof(frameData), 0);
    frameUBO->flush();
}

void SSAOStage::updateInputDescriptors()
{
    if (!_gBufferRT || !_targetTexture || !_noiseTexture) {
        return;
    }

    auto* frameBuffer = _gBufferRT->getCurFrameBuffer();
    if (!frameBuffer) {
        return;
    }

    const auto targetHandle = _targetTexture->getImageView() ? _targetTexture->getImageView()->getHandle() : ImageViewHandle{};
    if (_bInputDescriptorsInitialized && _lastGBufferFrameBuffer == frameBuffer && _lastTargetImageViewHandle == targetHandle) {
        _lastInputDescriptorWriteCount = 0;
        return;
    }

    auto sampler = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_inputDS, 0, frameBuffer->getColorTexture(0)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 1, frameBuffer->getColorTexture(1)->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 2, frameBuffer->getDepthTexture()->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_inputDS, 3, _noiseTexture->getImageView(), sampler.get()),
    });

    _lastGBufferFrameBuffer        = frameBuffer;
    _lastTargetImageViewHandle     = targetHandle;
    _bInputDescriptorsInitialized  = true;
    _lastInputDescriptorWriteCount = 4;
}

void SSAOStage::prepare(const RenderStageContext& ctx)
{
    YA_PROFILE_FUNCTION();
    if (_pipeline) {
        _pipeline->beginFrame();
    }
    if (!ctx.frameData || !_targetTexture || ctx.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return;
    }

    updateFrameUBO(ctx);
    updateInputDescriptors();
}

void SSAOStage::execute(const RenderStageContext& ctx)
{
    YA_PROFILE_FUNCTION();
    if (!ctx.cmdBuf || !_pipeline || !_targetTexture) {
        return;
    }

    ctx.cmdBuf->debugBeginLabel("SSAOStage");
    ctx.cmdBuf->transitionImageLayoutAuto(_targetTexture->getImage(), EImageLayout::ColorAttachmentOptimal);

    RenderingInfo renderingInfo{
        .label = "SSAO Pass",
        .renderArea = Rect2D{.pos = {0, 0}, .extent = _targetTexture->getExtent().toVec2()},
        .layerCount = 1,
        .colorClearValues = {ClearValue(1.0f, 1.0f, 1.0f, 1.0f)},
        .colorAttachments = {
            RenderingInfo::ImageSpec{
                .image         = _targetTexture ? _targetTexture->getImage() : nullptr,
                .imageView     = _targetTexture ? _targetTexture->getImageView() : nullptr,
                .loadOp        = EAttachmentLoadOp::Clear,
                .storeOp       = EAttachmentStoreOp::Store,
                .initialLayout = EImageLayout::ColorAttachmentOptimal,
                .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
            },
        },
    };

    ctx.cmdBuf->beginRendering(renderingInfo);
    ctx.cmdBuf->bindPipeline(_pipeline.get());
    ctx.cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(ctx.viewportExtent.width), static_cast<float>(ctx.viewportExtent.height));
    ctx.cmdBuf->setScissor(0, 0, ctx.viewportExtent.width, ctx.viewportExtent.height);
    ctx.cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {_frameDS[ctx.flightIndex], _inputDS});
    ctx.cmdBuf->draw(3, 1, 0, 0);
    ctx.cmdBuf->endRendering(renderingInfo);
    ctx.cmdBuf->debugEndLabel();
}

void SSAOStage::renderSettingsGUI()
{
    bool bDirty = false;
    bDirty |= ImGui::DragFloat("Radius", &_radius, 0.01f, 0.05f, 5.0f, "%.3f");
    bDirty |= ImGui::DragFloat("Bias", &_bias, 0.001f, 0.0f, 0.2f, "%.4f");
    bDirty |= ImGui::DragFloat("Power", &_power, 0.01f, 0.1f, 4.0f, "%.3f");
    bDirty |= ImGui::DragFloat("Intensity", &_intensity, 0.05f, 0.0f, 8.0f, "%.3f");

    if (bDirty) {
        ConfigManager::Editor(SSAO_CONFIG_DOC_NAME)
            .set(SSAO_CONFIG_KEY_RADIUS, _radius)
            .set(SSAO_CONFIG_KEY_BIAS, _bias)
            .set(SSAO_CONFIG_KEY_POWER, _power)
            .set(SSAO_CONFIG_KEY_INTENSITY, _intensity);
    }
}

void SSAOStage::renderTechnicalGUI()
{
    ImGui::Text("Descriptor writes: %u", _lastInputDescriptorWriteCount);

    if (ImGui::TreeNode("SSAO Pipeline")) {
        _pipeline->renderGUI();
        ImGui::TreePop();
    }
}

void SSAOStage::renderGUI()
{
    if (!ImGui::TreeNode("Ambient Occlusion")) {
        return;
    }

    renderSettingsGUI();

    renderTechnicalGUI();

    ImGui::TreePop();
}

} // namespace ya

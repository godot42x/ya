#include "BasicPostprocessing.h"

#include "Config/ConfigManager.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Sampler.h"
#include "Render/Render.h"
#include "Resource/Texture/TextureLibrary.h"

#include "imgui.h"

#include <algorithm>

namespace ya
{

namespace
{

constexpr uint32_t POSTPROCESS_FLAG_INVERSION    = 1u << 0;
constexpr uint32_t POSTPROCESS_FLAG_TONEMAPPING  = 1u << 1;
constexpr uint32_t POSTPROCESS_FLAG_GAMMA        = 1u << 2;
constexpr uint32_t POSTPROCESS_FLAG_RANDOM_GRAIN = 1u << 3;

constexpr const char* kGrayscaleModeLabels[] = {
    "None",
    "Average",
    "Weighted",
};

constexpr const char* kKernelModeLabels[] = {
    "None",
    "Sharpen",
    "Blur",
    "Edge Detection",
};

constexpr const char* kToneMappingCurveLabels[] = {
    "ACES",
    "Uncharted2",
};

constexpr const char* POSTPROCESS_CONFIG_DOC_NAME                         = "editor";
constexpr const char* POSTPROCESS_CONFIG_KEY_INVERSION                    = "render.postprocess.basic.inversion";
constexpr const char* POSTPROCESS_CONFIG_KEY_GRAYSCALE                    = "render.postprocess.basic.grayscale";
constexpr const char* POSTPROCESS_CONFIG_KEY_KERNEL                       = "render.postprocess.basic.kernel";
constexpr const char* POSTPROCESS_CONFIG_KEY_KERNEL_TEXEL_OFFSET          = "render.postprocess.basic.kernelTexelOffset";
constexpr const char* POSTPROCESS_CONFIG_KEY_TONEMAPPING_ENABLE           = "render.postprocess.basic.tonemapping.enabled";
constexpr const char* POSTPROCESS_CONFIG_KEY_TONEMAPPING_CURVE            = "render.postprocess.basic.tonemapping.curve";
constexpr const char* POSTPROCESS_CONFIG_KEY_TONEMAPPING_EXPOSURE         = "render.postprocess.basic.tonemapping.exposure";
constexpr const char* POSTPROCESS_CONFIG_KEY_GAMMA_CORRECTION_ENABLE      = "render.postprocess.basic.output.gammaCorrection";
constexpr const char* POSTPROCESS_CONFIG_KEY_GAMMA                        = "render.postprocess.basic.output.gamma";
constexpr const char* POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_ENABLE          = "render.postprocess.basic.output.randomGrain";
constexpr const char* POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_STRENGTH        = "render.postprocess.basic.output.randomGrainStrength";

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

    _descriptorPool = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                           .label     = "BasicPostprocessingPool",
                                                           .maxSets   = 1,
                                                           .poolSizes = {
                                                               DescriptorPoolSize{
                                                                   .type            = EPipelineDescriptorType::CombinedImageSampler,
                                                                   .descriptorCount = 1,
                                                               },
                                                           },
                                                       });

    std::vector<DescriptorSetHandle> descriptorSets;
    const bool                       ok = _descriptorPool->allocateDescriptorSets(_dslInputTexture, 1, descriptorSets);
    YA_CORE_ASSERT(ok, "Failed to allocate descriptor set");
    _descriptorSet = descriptorSets[0];
}

void BasicPostprocessing::shutdown()
{
    _descriptorPool.reset();
    _dslInputTexture.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _render                      = nullptr;
    _currentInputImageViewHandle = nullptr;
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

void BasicPostprocessing::render(const RenderDesc& desc)
{
    if (!desc.cmdBuf || !desc.inputImageView || !desc.state) {
        return;
    }
    if (desc.renderExtent.width == 0 || desc.renderExtent.height == 0) {
        return;
    }

    const auto imageViewHandle = desc.inputImageView->getHandle();
    if (_currentInputImageViewHandle != imageViewHandle) {
        _currentInputImageViewHandle = imageViewHandle;

        static auto         sampler = TextureLibrary::get().getDefaultSampler();
        DescriptorImageInfo imageInfo(
            _currentInputImageViewHandle,
            sampler->getHandle(),
            EImageLayout::ShaderReadOnlyOptimal);

        _render->getDescriptorHelper()->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(
                    _descriptorSet,
                    0,
                    0,
                    EPipelineDescriptorType::CombinedImageSampler,
                    {imageInfo}),
            },
            {});
    }

    rebuildPushConstants(*desc.state, desc.bOutputIsSRGB);

    desc.cmdBuf->bindPipeline(_pipeline.get());
    desc.cmdBuf->setViewport(0, 0, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
    desc.cmdBuf->setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
    desc.cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {_descriptorSet}, {});
    desc.cmdBuf->pushConstants(
        _pipelineLayout.get(),
        _pipelineLayoutDesc.pushConstants[0].stageFlags,
        _pipelineLayoutDesc.pushConstants[0].offset,
        _pipelineLayoutDesc.pushConstants[0].size,
        &_pushConstants);
    desc.cmdBuf->draw(3, 1, 0, 0);
}

void BasicPostprocessing::renderSettingsGUI(PostProcessingState& state)
{
    bool bDirty = false;

    ImGui::SeparatorText("Color Transform");
    bDirty |= ImGui::Checkbox("Inversion", &state.bEnableInversion);

    int grayscaleMode = static_cast<int>(state.grayscaleMode);
    if (ImGui::Combo("Grayscale", &grayscaleMode, kGrayscaleModeLabels, IM_ARRAYSIZE(kGrayscaleModeLabels))) {
        state.grayscaleMode = static_cast<PostProcessingState::EGrayscaleMode>(grayscaleMode);
        bDirty = true;
    }

    ImGui::SeparatorText("Spatial Filter");
    int kernelMode = static_cast<int>(state.kernelMode);
    if (ImGui::Combo("Kernel", &kernelMode, kKernelModeLabels, IM_ARRAYSIZE(kKernelModeLabels))) {
        state.kernelMode = static_cast<PostProcessingState::EKernelMode>(kernelMode);
        bDirty = true;
    }
    ImGui::BeginDisabled(state.kernelMode == PostProcessingState::EKernelMode::None);
    bDirty |= ImGui::DragFloat("Kernel Texel Offset", &state.kernelTexelOffset, 0.0001f, 0.0001f, 0.02f, "%.5f");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Tone Mapping");
    bDirty |= ImGui::Checkbox("Enable Tone Mapping", &state.bEnableToneMapping);
    ImGui::BeginDisabled(!state.bEnableToneMapping);
    int toneMappingCurve = static_cast<int>(state.toneMappingCurve);
    if (ImGui::Combo("ToneMapping Curve", &toneMappingCurve, kToneMappingCurveLabels, IM_ARRAYSIZE(kToneMappingCurveLabels))) {
        state.toneMappingCurve = static_cast<PostProcessingState::EToneMappingCurve>(toneMappingCurve);
        bDirty = true;
    }
    bDirty |= ImGui::DragFloat("Exposure", &state.exposure, 0.01f, 0.0f, 8.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Output");
    bDirty |= ImGui::Checkbox("Gamma Correction", &state.bEnableGammaCorrection);
    ImGui::BeginDisabled(!state.bEnableGammaCorrection);
    bDirty |= ImGui::DragFloat("Gamma", &state.gamma, 0.01f, 0.1f, 4.0f);
    ImGui::EndDisabled();

    bDirty |= ImGui::Checkbox("Random Grain", &state.bEnableRandomGrain);
    ImGui::BeginDisabled(!state.bEnableRandomGrain);
    bDirty |= ImGui::DragFloat("Grain Strength", &state.randomGrainStrength, 0.001f, 0.0f, 0.25f, "%.3f");
    ImGui::EndDisabled();

    if (bDirty) {
        ConfigManager::Editor(POSTPROCESS_CONFIG_DOC_NAME)
            .set(POSTPROCESS_CONFIG_KEY_INVERSION, state.bEnableInversion)
            .set(POSTPROCESS_CONFIG_KEY_GRAYSCALE, static_cast<int>(state.grayscaleMode))
            .set(POSTPROCESS_CONFIG_KEY_KERNEL, static_cast<int>(state.kernelMode))
            .set(POSTPROCESS_CONFIG_KEY_KERNEL_TEXEL_OFFSET, state.kernelTexelOffset)
            .set(POSTPROCESS_CONFIG_KEY_TONEMAPPING_ENABLE, state.bEnableToneMapping)
            .set(POSTPROCESS_CONFIG_KEY_TONEMAPPING_CURVE, static_cast<int>(state.toneMappingCurve))
            .set(POSTPROCESS_CONFIG_KEY_TONEMAPPING_EXPOSURE, state.exposure)
            .set(POSTPROCESS_CONFIG_KEY_GAMMA_CORRECTION_ENABLE, state.bEnableGammaCorrection)
            .set(POSTPROCESS_CONFIG_KEY_GAMMA, state.gamma)
            .set(POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_ENABLE, state.bEnableRandomGrain)
            .set(POSTPROCESS_CONFIG_KEY_RANDOM_GRAIN_STRENGTH, state.randomGrainStrength);
    }
}

void BasicPostprocessing::renderTechnicalGUI()
{
    if (_pipeline && ImGui::TreeNode("Basic Postprocess Pipeline")) {
        _pipeline->renderGUI();
        ImGui::TreePop();
    }
}

void BasicPostprocessing::reloadShader()
{
    if (_pipeline) {
        _pipeline->markDirty();
    }
}

} // namespace ya

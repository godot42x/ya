#include "BloomPostprocessing.h"

#include "Config/ConfigManager.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Render.h"
#include "Resource/Texture/TextureLibrary.h"

#include "imgui.h"

#include <algorithm>
#include <string>

namespace ya
{

namespace
{

constexpr const char* BLOOM_CONFIG_DOC_NAME              = "editor";
constexpr const char* BLOOM_CONFIG_KEY_ENABLE            = "render.postprocess.bloom.enabled";
constexpr const char* BLOOM_CONFIG_KEY_THRESHOLD         = "render.postprocess.bloom.threshold";
constexpr const char* BLOOM_CONFIG_KEY_SOFT_KNEE         = "render.postprocess.bloom.softKnee";
constexpr const char* BLOOM_CONFIG_KEY_EXTRACT_INTENSITY = "render.postprocess.bloom.extractIntensity";
constexpr const char* BLOOM_CONFIG_KEY_BLUR_PASSES       = "render.postprocess.bloom.blurPasses";
constexpr const char* BLOOM_CONFIG_KEY_STRENGTH          = "render.postprocess.bloom.strength";

template <typename TPushConstants>
PipelineLayoutDesc makePipelineLayoutDesc(const char* label, uint32_t descriptorCount)
{
    PipelineLayoutDesc desc{};
    desc.label         = label;
    desc.pushConstants = {
        PushConstantRange{
            .offset     = 0,
            .size       = sizeof(TPushConstants),
            .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment,
        },
    };
    desc.descriptorSetLayouts = {
        DescriptorSetLayoutDesc{
            .label    = std::string(label) + "_DSL",
            .set      = 0,
            .bindings = {},
        },
    };

    auto& bindings = desc.descriptorSetLayouts[0].bindings;
    for (uint32_t binding = 0; binding < descriptorCount; ++binding) {
        bindings.push_back(DescriptorSetLayoutBinding{
            .binding         = binding,
            .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags      = EShaderStage::Fragment,
        });
    }
    return desc;
}

GraphicsPipelineCreateInfo makePipelineDesc(const BloomPostprocessing::InitDesc& initDesc,
                                            IPipelineLayout*                     pipelineLayout,
                                            const char*                          shaderName)
{
    return GraphicsPipelineCreateInfo{
        .renderPass            = nullptr,
        .pipelineRenderingInfo = initDesc.pipelineRenderingInfo,
        .pipelineLayout        = pipelineLayout,
        .shaderDesc            = ShaderDesc{.shaderName = shaderName},
        .dynamicFeatures       = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType         = EPrimitiveType::TriangleList,
        .rasterizationState    = RasterizationState{.polygonMode = EPolygonMode::Fill, .cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState     = DepthStencilState{.bDepthTestEnable = false, .bDepthWriteEnable = false, .depthCompareOp = ECompareOp::Always},
        .colorBlendState       = ColorBlendState{.attachments = {ColorBlendAttachmentState{.index = 0, .bBlendEnable = false}}},
        .viewportState         = ViewportState{.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
}

} // namespace

void BloomPostprocessing::init(const InitDesc& initDesc)
{
    _render   = initDesc.render;
    _initDesc = initDesc;
    initExtractPipeline();
    initBlurPipeline();
    initCompositePipeline();
}

void BloomPostprocessing::shutdown()
{
    _extractDSP.reset();
    _extractDSL.reset();
    _extractPipeline.reset();
    _extractPPL.reset();
    _blurDSP.reset();
    _blurDSL.reset();
    _blurPipeline.reset();
    _blurPPL.reset();
    _compositeDSP.reset();
    _compositeDSL.reset();
    _compositePipeline.reset();
    _compositePPL.reset();
    _render                        = nullptr;
    _extractInputImageViewHandle   = nullptr;
    _blurInputImageViewHandle      = nullptr;
    _compositeSceneImageViewHandle = nullptr;
    _compositeBloomImageViewHandle = nullptr;
    _lastBlurPassCount             = 0;
}

void BloomPostprocessing::beginFrame()
{
    if (_extractPipeline) {
        _extractPipeline->beginFrame();
    }
    if (_blurPipeline) {
        _blurPipeline->beginFrame();
    }
    if (_compositePipeline) {
        _compositePipeline->beginFrame();
    }
}

void BloomPostprocessing::initExtractPipeline()
{
    using PushConstants = slang_types::Misc::BloomExtract::PushConstants;

    auto layoutDesc  = makePipelineLayoutDesc<PushConstants>("BloomExtract_PipelineLayout", 1);
    auto dsls        = IDescriptorSetLayout::create(_render, layoutDesc.descriptorSetLayouts);
    _extractDSL      = dsls[0];
    _extractPPL      = IPipelineLayout::create(_render, layoutDesc.label, layoutDesc.pushConstants, dsls);
    _extractPipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_extractPipeline->recreate(makePipelineDesc(_initDesc, _extractPPL.get(), "Misc/BloomExtract.slang")), "Failed to create BloomExtract pipeline");

    _extractDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                       .label     = "BloomExtract_DSP",
                                                       .maxSets   = 1,
                                                       .poolSizes = {{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1}},
                                                   });
    _extractDS  = _extractDSP->allocateDescriptorSets(_extractDSL);
}

void BloomPostprocessing::initBlurPipeline()
{
    using PushConstants = slang_types::Misc::BloomBlur::PushConstants;

    auto layoutDesc = makePipelineLayoutDesc<PushConstants>("BloomBlur_PipelineLayout", 1);
    auto dsls       = IDescriptorSetLayout::create(_render, layoutDesc.descriptorSetLayouts);
    _blurDSL        = dsls[0];
    _blurPPL        = IPipelineLayout::create(_render, layoutDesc.label, layoutDesc.pushConstants, dsls);
    _blurPipeline   = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_blurPipeline->recreate(makePipelineDesc(_initDesc, _blurPPL.get(), "Misc/BloomBlur.slang")), "Failed to create BloomBlur pipeline");

    _blurDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                    .label     = "BloomBlur_DSP",
                                                    .maxSets   = 1,
                                                    .poolSizes = {{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1}},
                                                });
    _blurDS  = _blurDSP->allocateDescriptorSets(_blurDSL);
}

void BloomPostprocessing::initCompositePipeline()
{
    using PushConstants = slang_types::Misc::BloomComposite::PushConstants;

    auto layoutDesc    = makePipelineLayoutDesc<PushConstants>("BloomComposite_PipelineLayout", 2);
    auto dsls          = IDescriptorSetLayout::create(_render, layoutDesc.descriptorSetLayouts);
    _compositeDSL      = dsls[0];
    _compositePPL      = IPipelineLayout::create(_render, layoutDesc.label, layoutDesc.pushConstants, dsls);
    _compositePipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_compositePipeline->recreate(makePipelineDesc(_initDesc, _compositePPL.get(), "Misc/BloomComposite.slang")), "Failed to create BloomComposite pipeline");

    _compositeDSP = IDescriptorPool::create(_render, DescriptorPoolCreateInfo{
                                                         .label     = "BloomComposite_DSP",
                                                         .maxSets   = 1,
                                                         .poolSizes = {{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 2}},
                                                     });
    _compositeDS  = _compositeDSP->allocateDescriptorSets(_compositeDSL);
}

void BloomPostprocessing::updateExtractDescriptor(Texture* inputTexture)
{
    const auto imageViewHandle = inputTexture && inputTexture->getImageView() ? inputTexture->getImageView()->getHandle() : ImageViewHandle{};
    if (_extractInputImageViewHandle == imageViewHandle) {
        return;
    }

    _extractInputImageViewHandle = imageViewHandle;
    auto sampler                 = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_extractDS, 0, inputTexture->getImageView(), sampler.get()),
    });
}

void BloomPostprocessing::updateBlurDescriptor(Texture* inputTexture)
{
    const auto imageViewHandle = inputTexture && inputTexture->getImageView() ? inputTexture->getImageView()->getHandle() : ImageViewHandle{};
    if (_blurInputImageViewHandle == imageViewHandle) {
        return;
    }

    _blurInputImageViewHandle = imageViewHandle;
    auto sampler              = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_blurDS, 0, inputTexture->getImageView(), sampler.get()),
    });
}

void BloomPostprocessing::updateCompositeDescriptor(Texture* sceneTexture, Texture* bloomTexture)
{
    const auto sceneHandle = sceneTexture && sceneTexture->getImageView() ? sceneTexture->getImageView()->getHandle() : ImageViewHandle{};
    const auto bloomHandle = bloomTexture && bloomTexture->getImageView() ? bloomTexture->getImageView()->getHandle() : ImageViewHandle{};
    if (_compositeSceneImageViewHandle == sceneHandle && _compositeBloomImageViewHandle == bloomHandle) {
        return;
    }

    _compositeSceneImageViewHandle = sceneHandle;
    _compositeBloomImageViewHandle = bloomHandle;
    auto           sampler         = TextureLibrary::get().getDefaultSampler();
    auto           bloomTexturePtr = bloomTexture ? ya::Ptr<Texture>(bloomTexture) : TextureLibrary::get().getBlackTexture();
    TextureBinding bloomBinding{
        .texture = bloomTexturePtr,
        .sampler = sampler,
    };
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_compositeDS, 0, sceneTexture->getImageView(), sampler.get()),
        IDescriptorSetHelper::writeOneImage(_compositeDS, 1, bloomBinding),
    });
}

void BloomPostprocessing::render(const RenderDesc& desc)
{
    if (!desc.cmdBuf || !desc.sceneTexture || !desc.outputTexture || !desc.state) {
        return;
    }
    if (desc.renderExtent.width == 0 || desc.renderExtent.height == 0) {
        return;
    }

    const bool bBloomEnabled = desc.state->bEnableBloom && desc.bloomExtract && desc.blurPingTexture && desc.blurPongTexture;

    if (bBloomEnabled) {
        desc.cmdBuf->debugBeginLabel("BloomExtract");
        desc.cmdBuf->transitionImageLayoutAuto(desc.bloomExtract->getImage(), EImageLayout::ColorAttachmentOptimal);
        updateExtractDescriptor(desc.sceneTexture);

        RenderingInfo extractRI{
            .label            = "BloomExtract",
            .renderArea       = Rect2D{.pos = {0, 0}, .extent = desc.renderExtent.toVec2()},
            .layerCount       = 1,
            .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
            .colorAttachments = {RenderingInfo::ImageSpec{.texture = desc.bloomExtract, .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal}},
        };

        slang_types::Misc::BloomExtract::PushConstants extractPC{};
        extractPC.threshold = desc.state->bloomThreshold;
        extractPC.knee      = desc.state->bloomSoftKnee;
        extractPC.intensity = desc.state->bloomExtractIntensity;

        desc.cmdBuf->beginRendering(extractRI);
        desc.cmdBuf->bindPipeline(_extractPipeline.get());
        desc.cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
        desc.cmdBuf->setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
        desc.cmdBuf->bindDescriptorSets(_extractPPL.get(), 0, {_extractDS});
        desc.cmdBuf->pushConstants(_extractPPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(extractPC), &extractPC);
        desc.cmdBuf->draw(3, 1, 0, 0);
        desc.cmdBuf->endRendering(extractRI);
        desc.cmdBuf->debugEndLabel();

        Texture*       blurInput     = desc.bloomExtract;
        const uint32_t blurPassCount = std::max<uint32_t>(1, desc.state->bloomBlurPasses * 2);
        _lastBlurPassCount           = blurPassCount;

        for (uint32_t passIndex = 0; passIndex < blurPassCount; ++passIndex) {
            const bool bHorizontal = (passIndex % 2) == 0;
            Texture*   blurTarget  = bHorizontal ? desc.blurPingTexture : desc.blurPongTexture;

            desc.cmdBuf->transitionImageLayoutAuto(blurTarget->getImage(), EImageLayout::ColorAttachmentOptimal);
            updateBlurDescriptor(blurInput);

            RenderingInfo blurRI{
                .label            = bHorizontal ? "BloomBlurHorizontal" : "BloomBlurVertical",
                .renderArea       = Rect2D{.pos = {0, 0}, .extent = desc.renderExtent.toVec2()},
                .layerCount       = 1,
                .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
                .colorAttachments = {RenderingInfo::ImageSpec{.texture = blurTarget, .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal}},
            };

            slang_types::Misc::BloomBlur::PushConstants blurPC{};
            blurPC.texelSize  = glm::vec2(1.0f / static_cast<float>(desc.renderExtent.width), 1.0f / static_cast<float>(desc.renderExtent.height));
            blurPC.horizontal = bHorizontal ? 1u : 0u;

            desc.cmdBuf->beginRendering(blurRI);
            desc.cmdBuf->bindPipeline(_blurPipeline.get());
            desc.cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
            desc.cmdBuf->setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
            desc.cmdBuf->bindDescriptorSets(_blurPPL.get(), 0, {_blurDS});
            desc.cmdBuf->pushConstants(_blurPPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(blurPC), &blurPC);
            desc.cmdBuf->draw(3, 1, 0, 0);
            desc.cmdBuf->endRendering(blurRI);

            blurInput = blurTarget;
        }

        updateCompositeDescriptor(desc.sceneTexture, blurInput);
    }
    else {
        _lastBlurPassCount = 0;
        updateCompositeDescriptor(desc.sceneTexture, nullptr);
    }

    desc.cmdBuf->debugBeginLabel("BloomComposite");
    desc.cmdBuf->transitionImageLayoutAuto(desc.outputTexture->getImage(), EImageLayout::ColorAttachmentOptimal);

    RenderingInfo compositeRI{
        .label            = "BloomComposite",
        .renderArea       = Rect2D{.pos = {0, 0}, .extent = desc.renderExtent.toVec2()},
        .layerCount       = 1,
        .colorClearValues = {ClearValue(0.0f, 0.0f, 0.0f, 1.0f)},
        .colorAttachments = {RenderingInfo::ImageSpec{.texture = desc.outputTexture, .loadOp = EAttachmentLoadOp::Clear, .storeOp = EAttachmentStoreOp::Store, .initialLayout = EImageLayout::ColorAttachmentOptimal, .finalLayout = EImageLayout::ShaderReadOnlyOptimal}},
    };

    slang_types::Misc::BloomComposite::PushConstants compositePC{};
    compositePC.bloomStrength = desc.state->bloomStrength;
    compositePC.bloomEnabled  = bBloomEnabled ? 1u : 0u;

    desc.cmdBuf->beginRendering(compositeRI);
    desc.cmdBuf->bindPipeline(_compositePipeline.get());
    desc.cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
    desc.cmdBuf->setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
    desc.cmdBuf->bindDescriptorSets(_compositePPL.get(), 0, {_compositeDS});
    desc.cmdBuf->pushConstants(_compositePPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(compositePC), &compositePC);
    desc.cmdBuf->draw(3, 1, 0, 0);
    desc.cmdBuf->endRendering(compositeRI);
    desc.cmdBuf->debugEndLabel();
}

void BloomPostprocessing::renderSettingsGUI(PostProcessingState& state)
{
    bool bDirty = false;
    bDirty |= ImGui::Checkbox("Enable Bloom", &state.bEnableBloom);
    ImGui::BeginDisabled(!state.bEnableBloom);
    bDirty |= ImGui::DragFloat("Bloom Threshold", &state.bloomThreshold, 0.01f, 0.0f, 16.0f, "%.2f");
    bDirty |= ImGui::DragFloat("Bloom Soft Knee", &state.bloomSoftKnee, 0.01f, 0.0f, 2.0f, "%.2f");
    bDirty |= ImGui::DragFloat("Bloom Extract Intensity", &state.bloomExtractIntensity, 0.05f, 0.0f, 8.0f, "%.2f");
    int blurPasses = static_cast<int>(state.bloomBlurPasses);
    if (ImGui::DragInt("Bloom Blur Passes", &blurPasses, 1.0f, 1, 12)) {
        state.bloomBlurPasses = static_cast<uint32_t>(std::max(1, blurPasses));
        bDirty                = true;
    }
    bDirty |= ImGui::DragFloat("Bloom Strength", &state.bloomStrength, 0.05f, 0.0f, 4.0f, "%.2f");
    ImGui::Text("Blur Passes (H+V): %u", _lastBlurPassCount);
    ImGui::EndDisabled();

    if (bDirty) {
        ConfigManager::Editor(BLOOM_CONFIG_DOC_NAME)
            .set(BLOOM_CONFIG_KEY_ENABLE, state.bEnableBloom)
            .set(BLOOM_CONFIG_KEY_THRESHOLD, state.bloomThreshold)
            .set(BLOOM_CONFIG_KEY_SOFT_KNEE, state.bloomSoftKnee)
            .set(BLOOM_CONFIG_KEY_EXTRACT_INTENSITY, state.bloomExtractIntensity)
            .set(BLOOM_CONFIG_KEY_BLUR_PASSES, static_cast<int>(state.bloomBlurPasses))
            .set(BLOOM_CONFIG_KEY_STRENGTH, state.bloomStrength);
    }

}

void BloomPostprocessing::renderTechnicalGUI()
{
    if (_extractPipeline && ImGui::TreeNode("Bloom Pipelines")) {
        if (ImGui::TreeNode("Extract")) {
            _extractPipeline->renderGUI();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Blur")) {
            _blurPipeline->renderGUI();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Composite")) {
            _compositePipeline->renderGUI();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
}

} // namespace ya

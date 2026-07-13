#include "BloomPostprocessing.h"

#include "Config/ConfigManager.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderGraphExecutor.h"
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

RGImportedTextureDesc makeBloomImportedTextureDesc(const Texture& texture,
                                                   std::string_view label,
                                                   EImageLayout::T finalLayout)
{
    YA_CORE_ASSERT(texture.getImageShared() != nullptr, "Bloom graph import requires a backing image");

    IImage* image = texture.getImage();
    YA_CORE_ASSERT(image != nullptr, "Bloom graph import requires a valid image");

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = texture.getFormat(),
            .extent      = Extent3D{texture.getWidth(), texture.getHeight(), 1},
            .mipLevels   = image->getMipLevels(),
            .arrayLayers = image->getArrayLayers(),
            .usage       = image->getUsage(),
        },
        .importDesc = ImportedImageDesc{
            .label         = std::string(label),
            .nativeHandle  = static_cast<void*>(image->getHandle()),
            .format        = texture.getFormat(),
            .usage         = image->getUsage(),
            .extent        = Extent3D{texture.getWidth(), texture.getHeight(), 1},
            .mipLevels     = image->getMipLevels(),
            .arrayLayers   = image->getArrayLayers(),
            .initialLayout = image->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = texture.getImageShared(),
    };
}

RGImportedTextureDesc makeBloomImportedTextureDesc(const RenderImage& image,
                                                   std::string_view label,
                                                   EImageLayout::T finalLayout)
{
    YA_CORE_ASSERT(image.getImageShared() != nullptr, "Bloom graph import requires a backing image");

    IImage* rawImage = image.getImage();
    YA_CORE_ASSERT(rawImage != nullptr, "Bloom graph import requires a valid image");

    return RGImportedTextureDesc{
        .desc = RGTextureDesc{
            .label       = std::string(label),
            .format      = image.getFormat(),
            .extent      = Extent3D{image.getWidth(), image.getHeight(), 1},
            .mipLevels   = rawImage->getMipLevels(),
            .arrayLayers = rawImage->getArrayLayers(),
            .usage       = rawImage->getUsage(),
        },
        .importDesc = ImportedImageDesc{
            .label         = std::string(label),
            .nativeHandle  = static_cast<void*>(rawImage->getHandle()),
            .format        = image.getFormat(),
            .usage         = rawImage->getUsage(),
            .extent        = Extent3D{image.getWidth(), image.getHeight(), 1},
            .mipLevels     = rawImage->getMipLevels(),
            .arrayLayers   = rawImage->getArrayLayers(),
            .initialLayout = rawImage->getCompatibilityLayout(),
            .finalLayout   = finalLayout,
        },
        .image = image.getImageShared(),
    };
}

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
    _graphExecutor = std::make_unique<RenderGraphExecutor>(*_render->getResourceFactory());
    initExtractPipeline();
    initBlurPipeline();
    initCompositePipeline();
}

void BloomPostprocessing::shutdown()
{
    _graphExecutor.reset();
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
    _blurDSs.clear();
    _blurInputImageViewHandles.clear();
    _compositeSceneImageViewHandle = nullptr;
    _compositeBloomImageViewHandle = nullptr;
    _lastBlurPassCount             = 0;
    _extractImage                  = nullptr;
    _blurPingImage                 = nullptr;
    _blurPongImage                 = nullptr;
    _compositeImage                = nullptr;
    _preparedGraphResources        = {};
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

void BloomPostprocessing::clearPreparedResources()
{
    _preparedGraphResources = {};
    _extractImage           = nullptr;
    _blurPingImage          = nullptr;
    _blurPongImage          = nullptr;
    _compositeImage         = nullptr;
}

void BloomPostprocessing::resolvePreparedResources(const RenderGraphResourceRegistry& registry)
{
    _extractImage = _preparedGraphResources.extract.isValid()
        ? registry.resolveTexture(_preparedGraphResources.extract)
        : nullptr;
    _blurPingImage = _preparedGraphResources.blurPing.isValid()
        ? registry.resolveTexture(_preparedGraphResources.blurPing)
        : nullptr;
    _blurPongImage = _preparedGraphResources.blurPong.isValid()
        ? registry.resolveTexture(_preparedGraphResources.blurPong)
        : nullptr;
    _compositeImage = _preparedGraphResources.output.isValid()
        ? registry.resolveTexture(_preparedGraphResources.output)
        : nullptr;
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
                                                    .maxSets   = MAX_BLOOM_BLUR_DESCRIPTOR_SETS,
                                                    .poolSizes = {{.type = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = MAX_BLOOM_BLUR_DESCRIPTOR_SETS}},
                                                });
    _blurDSs.resize(MAX_BLOOM_BLUR_DESCRIPTOR_SETS);
    const bool ok = _blurDSP->allocateDescriptorSets(_blurDSL, MAX_BLOOM_BLUR_DESCRIPTOR_SETS, _blurDSs);
    YA_CORE_ASSERT(ok, "Failed to allocate BloomBlur descriptor sets");
    _blurInputImageViewHandles.assign(MAX_BLOOM_BLUR_DESCRIPTOR_SETS, ImageViewHandle{});
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

void BloomPostprocessing::updateExtractDescriptor(IImageView* inputImageView)
{
    const auto imageViewHandle = inputImageView ? inputImageView->getHandle() : ImageViewHandle{};
    if (_extractInputImageViewHandle == imageViewHandle) {
        return;
    }

    _extractInputImageViewHandle = imageViewHandle;
    auto sampler                 = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_extractDS, 0, inputImageView, sampler.get()),
    });
}

DescriptorSetHandle BloomPostprocessing::updateBlurDescriptor(uint32_t passIndex, IImageView* inputImageView)
{
    YA_CORE_ASSERT(passIndex < _blurDSs.size(), "Bloom blur pass index {} exceeds descriptor set pool size {}", passIndex, _blurDSs.size());

    const auto imageViewHandle = inputImageView ? inputImageView->getHandle() : ImageViewHandle{};
    if (_blurInputImageViewHandles[passIndex] == imageViewHandle) {
        return _blurDSs[passIndex];
    }

    _blurInputImageViewHandles[passIndex] = imageViewHandle;
    auto sampler                          = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_blurDSs[passIndex], 0, inputImageView, sampler.get()),
    });

    return _blurDSs[passIndex];
}

void BloomPostprocessing::updateCompositeDescriptor(IImageView* sceneImageView, IImageView* bloomImageView)
{
    auto*      resolvedBloomImageView = bloomImageView ? bloomImageView : TextureLibrary::get().getBlackTexture()->getImageView();
    const auto sceneHandle            = sceneImageView ? sceneImageView->getHandle() : ImageViewHandle{};
    const auto bloomHandle            = resolvedBloomImageView ? resolvedBloomImageView->getHandle() : ImageViewHandle{};
    if (_compositeSceneImageViewHandle == sceneHandle && _compositeBloomImageViewHandle == bloomHandle) {
        return;
    }

    _compositeSceneImageViewHandle = sceneHandle;
    _compositeBloomImageViewHandle = bloomHandle;
    auto sampler = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(_compositeDS, 0, sceneImageView, sampler.get()),
        IDescriptorSetHelper::writeOneImage(_compositeDS, 1, resolvedBloomImageView, sampler.get()),
    });
}

RGTextureHandle BloomPostprocessing::appendGraphPasses(RenderGraph& graph, const RenderDesc& desc)
{
    clearPreparedResources();
    if (!desc.sceneTexture || !desc.sceneImageView || !desc.state) {
        return {};
    }
    if (desc.renderExtent.width == 0 || desc.renderExtent.height == 0) {
        return {};
    }

    const bool bBloomEnabled = desc.state->bEnableBloom;

    const auto scene = graph.importTexture(makeBloomImportedTextureDesc(*desc.sceneTexture, "Bloom.Scene", EImageLayout::ShaderReadOnlyOptimal));
    const auto output = graph.createTexture(RGTextureDesc{
         .label  = "Bloom.CompositeOutput",
         .format = EFormat::R16G16B16A16_SFLOAT,
         .extent = Extent3D{desc.renderExtent.width, desc.renderExtent.height, 1},
         .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    }, ERGResourceLifetime::Persistent);
    std::optional<RGTextureHandle> bloomExtract{};
    std::optional<RGTextureHandle> blurPing{};
    std::optional<RGTextureHandle> blurPong{};

    if (bBloomEnabled) {
        bloomExtract = graph.createTexture(RGTextureDesc{
            .label  = "Bloom.Extract",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{desc.renderExtent.width, desc.renderExtent.height, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        }, ERGResourceLifetime::Persistent);
        blurPing = graph.createTexture(RGTextureDesc{
            .label  = "Bloom.BlurPing",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{desc.renderExtent.width, desc.renderExtent.height, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        }, ERGResourceLifetime::Persistent);
        blurPong = graph.createTexture(RGTextureDesc{
            .label  = "Bloom.BlurPong",
            .format = EFormat::R16G16B16A16_SFLOAT,
            .extent = Extent3D{desc.renderExtent.width, desc.renderExtent.height, 1},
            .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        }, ERGResourceLifetime::Persistent);
    }

    _preparedGraphResources.output        = output;
    _preparedGraphResources.bBloomEnabled = bBloomEnabled;
    _preparedGraphResources.extract       = bloomExtract.value_or(RGTextureHandle{});
    _preparedGraphResources.blurPing      = blurPing.value_or(RGTextureHandle{});
    _preparedGraphResources.blurPong      = blurPong.value_or(RGTextureHandle{});

    if (bBloomEnabled) {
        updateExtractDescriptor(desc.sceneImageView);

        slang_types::Misc::BloomExtract::PushConstants extractPC{};
        extractPC.threshold = desc.state->bloomThreshold;
        extractPC.knee      = desc.state->bloomSoftKnee;
        extractPC.intensity = desc.state->bloomExtractIntensity;

        [[maybe_unused]] const auto extractPass = graph.addPass(
            "BloomExtract",
            [&](RGPassBuilder& pass) {
                pass.read(scene);
                pass.useColorAttachment(*bloomExtract);
            },
            [&](RGRenderContext& rgCtx) {
                rgCtx.beginColorRendering({
                    .color      = *bloomExtract,
                    .renderArea = Rect2D{.pos = {0.0f, 0.0f}, .extent = desc.renderExtent.toVec2()},
                    .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                });
                rgCtx.getCommandBuffer().bindPipeline(_extractPipeline.get());
                rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
                rgCtx.getCommandBuffer().setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
                rgCtx.getCommandBuffer().bindDescriptorSets(_extractPPL.get(), 0, {_extractDS});
                rgCtx.getCommandBuffer().pushConstants(_extractPPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(extractPC), &extractPC);
                rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
                rgCtx.endRendering();
            });

        const uint32_t blurPassCount = std::max<uint32_t>(1, desc.state->bloomBlurPasses * 2);
        _lastBlurPassCount           = blurPassCount;

        for (uint32_t passIndex = 0; passIndex < blurPassCount; ++passIndex) {
            const bool bHorizontal = (passIndex % 2) == 0;
            const RGTextureHandle     blurInputHandle = (passIndex == 0)
                ? *bloomExtract
                : ((passIndex - 1) % 2 == 0 ? *blurPing : *blurPong);
            const RGTextureHandle     blurTargetHandle = bHorizontal ? *blurPing : *blurPong;

            slang_types::Misc::BloomBlur::PushConstants blurPC{};
            blurPC.texelSize  = glm::vec2(1.0f / static_cast<float>(desc.renderExtent.width), 1.0f / static_cast<float>(desc.renderExtent.height));
            blurPC.horizontal = bHorizontal ? 1u : 0u;

            [[maybe_unused]] const auto blurPass = graph.addPass(
                bHorizontal ? "BloomBlurHorizontal" : "BloomBlurVertical",
                [&](RGPassBuilder& pass) {
                    pass.read(blurInputHandle);
                    pass.useColorAttachment(blurTargetHandle);
                },
                [&](RGRenderContext& rgCtx) {
                    rgCtx.beginColorRendering({
                        .color      = blurTargetHandle,
                        .renderArea = Rect2D{.pos = {0.0f, 0.0f}, .extent = desc.renderExtent.toVec2()},
                        .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                        .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                    });
                    const auto* blurInputImage = rgCtx.resolveTexture(blurInputHandle);
                    YA_CORE_ASSERT(blurInputImage != nullptr && blurInputImage->getImageView() != nullptr,
                                   "Bloom blur pass failed to resolve input texture {}", blurInputHandle.index);
                    const DescriptorSetHandle blurDS = updateBlurDescriptor(passIndex, blurInputImage->getImageView());
                    rgCtx.getCommandBuffer().bindPipeline(_blurPipeline.get());
                    rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
                    rgCtx.getCommandBuffer().setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
                    rgCtx.getCommandBuffer().bindDescriptorSets(_blurPPL.get(), 0, {blurDS});
                    rgCtx.getCommandBuffer().pushConstants(_blurPPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(blurPC), &blurPC);
                    rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
                    rgCtx.endRendering();
            });
        }
    }
    else {
        _lastBlurPassCount = 0;
    }

    slang_types::Misc::BloomComposite::PushConstants compositePC{};
    compositePC.bloomStrength = desc.state->bloomStrength;
    compositePC.bloomEnabled  = bBloomEnabled ? 1u : 0u;

    [[maybe_unused]] const auto compositePass = graph.addPass(
        "BloomComposite",
        [&](RGPassBuilder& pass) {
            pass.read(scene);
            if (bBloomEnabled) {
                pass.read((_lastBlurPassCount == 0 || (_lastBlurPassCount % 2) == 0) ? *blurPong : *blurPing);
            }
            pass.useColorAttachment(output);
        },
        [&](RGRenderContext& rgCtx) {
            rgCtx.beginColorRendering({
                .color      = output,
                .renderArea = Rect2D{.pos = {0.0f, 0.0f}, .extent = desc.renderExtent.toVec2()},
                .clearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
            });
            IImageView* compositeBloomImageView = nullptr;
            if (bBloomEnabled) {
                const RGTextureHandle finalBloomHandle = (_lastBlurPassCount == 0 || (_lastBlurPassCount % 2) == 0) ? *blurPong : *blurPing;
                const auto* finalBloomImage = rgCtx.resolveTexture(finalBloomHandle);
                YA_CORE_ASSERT(finalBloomImage != nullptr && finalBloomImage->getImageView() != nullptr,
                               "Bloom composite pass failed to resolve bloom texture {}", finalBloomHandle.index);
                compositeBloomImageView = finalBloomImage->getImageView();
            }
            updateCompositeDescriptor(desc.sceneImageView, compositeBloomImageView);
            rgCtx.getCommandBuffer().bindPipeline(_compositePipeline.get());
            rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(desc.renderExtent.width), static_cast<float>(desc.renderExtent.height));
            rgCtx.getCommandBuffer().setScissor(0, 0, desc.renderExtent.width, desc.renderExtent.height);
            rgCtx.getCommandBuffer().bindDescriptorSets(_compositePPL.get(), 0, {_compositeDS});
            rgCtx.getCommandBuffer().pushConstants(_compositePPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(compositePC), &compositePC);
            rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
            rgCtx.endRendering();
        });
    return output;
}

void BloomPostprocessing::render(const RenderDesc& desc)
{
    if (!desc.cmdBuf) {
        clearPreparedResources();
        return;
    }

    ICommandBuffer::LabelScope labelScope(desc.cmdBuf, "BloomPostprocessing");
    RenderGraph graph;
    const auto  output = appendGraphPasses(graph, desc);
    if (!output.isValid()) {
        return;
    }

    YA_CORE_ASSERT(_graphExecutor != nullptr, "BloomPostprocessing graph executor is not initialized");
    [[maybe_unused]] const bool bExecuted = _graphExecutor->execute(graph, *desc.cmdBuf);
    if (!bExecuted) {
        clearPreparedResources();
        return;
    }

    resolvePreparedResources(_graphExecutor->getRegistry());
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

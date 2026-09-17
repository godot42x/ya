#include "Render3D/Pipelines/BloomPostprocessing.h"

#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "Graph/RenderGraphExecutor.h"
#include "Graph/RenderGraphImportUtils.h"
#include "RHI/Render.h"
#include "RHI/Backend/TextureLibrary.h"
#include "Render3D/Common/ViewPersistentResourceKey.h"

#include <algorithm>
#include <optional>
#include <string>

namespace ya
{

namespace
{

RGImportedTextureDesc makeBloomImportedTextureDesc(const std::shared_ptr<ImageResource>& resource,
                                                   std::string_view                    label,
                                                   EImageLayout::T                     finalLayout)
{
    return makeImportedTextureDesc(resource, label, finalLayout);
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
    _extractDSL.reset();
    _extractPipeline.reset();
    _extractPPL.reset();
    _blurDSL.reset();
    _blurPipeline.reset();
    _blurPPL.reset();
    _compositeDSL.reset();
    _compositePipeline.reset();
    _compositePPL.reset();
    _render            = nullptr;
    _lastBlurPassCount = 0;
    _extractImage.reset();
    _blurPingImage.reset();
    _blurPongImage.reset();
    _compositeImage.reset();
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
    _extractImage.reset();
    _blurPingImage.reset();
    _blurPongImage.reset();
    _compositeImage.reset();
}

void BloomPostprocessing::capturePreparedResources(const RenderGraphExecutionResult& result, uint64_t viewId)
{
    _extractImage   = result.getExportedTextureShared(makeViewGraphName(kExtractExportName, viewId));
    _blurPingImage  = result.getExportedTextureShared(makeViewGraphName(kBlurPingExportName, viewId));
    _blurPongImage  = result.getExportedTextureShared(makeViewGraphName(kBlurPongExportName, viewId));
    _compositeImage = result.getExportedTextureShared(makeViewGraphName(kOutputExportName, viewId));
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
}

void BloomPostprocessing::writeSampler(DescriptorSetHandle set, IImageView* inputImageView)
{
    if (!_render || !set) {
        return;
    }
    auto sampler = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(set, 0, inputImageView, sampler.get()),
    });
}

void BloomPostprocessing::writeComposite(DescriptorSetHandle set, IImageView* sceneImageView, IImageView* bloomImageView)
{
    if (!_render || !set) {
        return;
    }
    auto* resolvedBloomImageView = bloomImageView ? bloomImageView : TextureLibrary::get().getBlackTexture()->getImageView();
    auto sampler = TextureLibrary::get().getDefaultSampler();
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(set, 0, sceneImageView, sampler.get()),
        IDescriptorSetHelper::writeOneImage(set, 1, resolvedBloomImageView, sampler.get()),
    });
}

RGTextureHandle BloomPostprocessing::appendGraphPasses(RenderGraph& graph, const RenderDesc& desc)
{
    clearPreparedResources();
    if ((!desc.sceneTexture && !desc.sceneImage && !desc.sceneHandle.isValid()) || !desc.state) {
        return {};
    }
    if (desc.renderExtent.width == 0 || desc.renderExtent.height == 0) {
        return {};
    }

    const bool bBloomEnabled = desc.state->bEnableBloom;

    const auto scene = desc.sceneHandle.isValid()
        ? desc.sceneHandle
        : desc.sceneImage
            ? graph.importTexture(makeBloomImportedTextureDesc(desc.sceneImage->getResourceShared(), "Bloom.Scene", EImageLayout::ShaderReadOnlyOptimal))
            : graph.importTexture(makeBloomImportedTextureDesc(desc.sceneTexture->getResourceShared(), "Bloom.Scene", EImageLayout::ShaderReadOnlyOptimal));
    const auto bloomDesc = RGTextureDesc{
        .label  = "Bloom.CompositeOutput",
        .format = BloomPostprocessing::BLOOM_FORMAT,
        .extent = Extent3D{desc.renderExtent.width, desc.renderExtent.height, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    };
    const auto output = createViewPersistentTexture(graph, bloomDesc, "Bloom.CompositeOutput", desc.viewId);
    std::optional<RGTextureHandle> bloomExtract{};
    std::optional<RGTextureHandle> blurPing{};
    std::optional<RGTextureHandle> blurPong{};

    if (bBloomEnabled) {
        bloomExtract = createViewPersistentTexture(graph, bloomDesc, "Bloom.Extract", desc.viewId);
        blurPing     = createViewPersistentTexture(graph, bloomDesc, "Bloom.BlurPing", desc.viewId);
        blurPong     = createViewPersistentTexture(graph, bloomDesc, "Bloom.BlurPong", desc.viewId);
    }

    graph.exportTexture(output, makeViewGraphName(kOutputExportName, desc.viewId));
    if (bloomExtract.has_value()) {
        graph.exportTexture(*bloomExtract, makeViewGraphName(kExtractExportName, desc.viewId));
    }
    if (blurPing.has_value()) {
        graph.exportTexture(*blurPing, makeViewGraphName(kBlurPingExportName, desc.viewId));
    }
    if (blurPong.has_value()) {
        graph.exportTexture(*blurPong, makeViewGraphName(kBlurPongExportName, desc.viewId));
    }

    if (bBloomEnabled) {
        slang_types::Misc::BloomExtract::PushConstants extractPC{};
        extractPC.threshold = desc.state->bloomThreshold;
        extractPC.knee      = desc.state->bloomSoftKnee;
        extractPC.intensity = desc.state->bloomExtractIntensity;
        const RGTextureHandle bloomExtractHandle = *bloomExtract;
        [[maybe_unused]] const auto extractPass = graph.addPass(
            makeViewGraphName("BloomExtract", desc.viewId),
            [scene, bloomExtractHandle, renderExtent = desc.renderExtent](RGPassBuilder& pass) {
                pass.read(scene);
                pass.declareRaster({
                    .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = renderExtent.toVec2()},
                    .layerCount  = 1,
                    .colors = {{
                        .color       = bloomExtractHandle,
                        .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                        .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                    }},
                });
            },
            [this, scene, extractPC, extractDS = desc.bloom.extract.set](RGRenderContext& rgCtx) {
                const auto rasterParams = rgCtx.getRasterPassExecutionParams();
                const auto renderExtent = rasterParams.getRenderExtent();
                rgCtx.beginDeclaredRasterRendering();
                const auto* sceneImage = rgCtx.resolveTexture(scene);
                YA_CORE_ASSERT(sceneImage != nullptr && sceneImage->getImageView() != nullptr,
                               "Bloom extract pass failed to resolve scene texture {}", scene.index);
                writeSampler(extractDS, sceneImage->getImageView());
                rgCtx.getCommandBuffer().bindPipeline(_extractPipeline.get());
                rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(renderExtent.width), static_cast<float>(renderExtent.height));
                rgCtx.getCommandBuffer().setScissor(0, 0, renderExtent.width, renderExtent.height);
                rgCtx.getCommandBuffer().bindDescriptorSets(_extractPPL.get(), 0, {extractDS});
                rgCtx.getCommandBuffer().pushConstants(_extractPPL.get(), EShaderStage::Vertex | EShaderStage::Fragment, 0, sizeof(extractPC), &extractPC);
                rgCtx.getCommandBuffer().draw(3, 1, 0, 0);
                rgCtx.endRendering();
            });

        const uint32_t blurPassCount = std::min(
            kMaxBloomBlurPasses,
            std::max<uint32_t>(1, desc.state->bloomBlurPasses * 2));
        _lastBlurPassCount = blurPassCount;

        for (uint32_t passIndex = 0; passIndex < blurPassCount; ++passIndex) {
            const bool bHorizontal = (passIndex % 2) == 0;
            const RGTextureHandle     blurInputHandle = (passIndex == 0)
                ? bloomExtractHandle
                : ((passIndex - 1) % 2 == 0 ? *blurPing : *blurPong);
            const RGTextureHandle     blurTargetHandle = bHorizontal ? *blurPing : *blurPong;

            slang_types::Misc::BloomBlur::PushConstants blurPC{};
            blurPC.texelSize  = glm::vec2(1.0f / static_cast<float>(desc.renderExtent.width), 1.0f / static_cast<float>(desc.renderExtent.height));
            blurPC.horizontal = bHorizontal ? 1u : 0u;
            [[maybe_unused]] const auto blurPass = graph.addPass(
                makeViewGraphName(bHorizontal ? "BloomBlurHorizontal" : "BloomBlurVertical", desc.viewId),
                [blurInputHandle, blurTargetHandle, renderExtent = desc.renderExtent](RGPassBuilder& pass) {
                    pass.read(blurInputHandle);
                    pass.declareRaster({
                        .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = renderExtent.toVec2()},
                        .layerCount  = 1,
                        .colors = {{
                            .color       = blurTargetHandle,
                            .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                            .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                        }},
                    });
                },
                [this, blurInputHandle, blurPC, blurDS = desc.bloom.blur[passIndex].set](RGRenderContext& rgCtx) {
                    const auto rasterParams = rgCtx.getRasterPassExecutionParams();
                    const auto renderExtent = rasterParams.getRenderExtent();
                    rgCtx.beginDeclaredRasterRendering();
                    const auto* blurInputImage = rgCtx.resolveTexture(blurInputHandle);
                    YA_CORE_ASSERT(blurInputImage != nullptr && blurInputImage->getImageView() != nullptr,
                                   "Bloom blur pass failed to resolve input texture {}", blurInputHandle.index);
                    writeSampler(blurDS, blurInputImage->getImageView());
                    rgCtx.getCommandBuffer().bindPipeline(_blurPipeline.get());
                    rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(renderExtent.width), static_cast<float>(renderExtent.height));
                    rgCtx.getCommandBuffer().setScissor(0, 0, renderExtent.width, renderExtent.height);
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
        makeViewGraphName("BloomComposite", desc.viewId),
        [scene, bBloomEnabled, renderExtent = desc.renderExtent, finalBloomHandle = (_lastBlurPassCount == 0 || (_lastBlurPassCount % 2) == 0) ? blurPong.value_or(RGTextureHandle{}) : blurPing.value_or(RGTextureHandle{}), output](RGPassBuilder& pass) {
            pass.read(scene);
            if (bBloomEnabled) {
                pass.read(finalBloomHandle);
            }
            pass.declareRaster({
                .renderArea  = Rect2D{.pos = {0.0f, 0.0f}, .extent = renderExtent.toVec2()},
                .layerCount  = 1,
                .colors = {{
                    .color       = output,
                    .clearValue  = ClearValue(0.0f, 0.0f, 0.0f, 1.0f),
                    .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                }},
            });
        },
        [this, scene, bBloomEnabled, compositePC, compositeDS = desc.bloom.composite.set, finalBloomHandle = (_lastBlurPassCount == 0 || (_lastBlurPassCount % 2) == 0) ? blurPong.value_or(RGTextureHandle{}) : blurPing.value_or(RGTextureHandle{})](RGRenderContext& rgCtx) {
            const auto rasterParams = rgCtx.getRasterPassExecutionParams();
            const auto renderExtent = rasterParams.getRenderExtent();
            rgCtx.beginDeclaredRasterRendering();
            const auto* sceneImage = rgCtx.resolveTexture(scene);
            YA_CORE_ASSERT(sceneImage != nullptr && sceneImage->getImageView() != nullptr,
                           "Bloom composite pass failed to resolve scene texture {}", scene.index);
            IImageView* compositeBloomImageView = nullptr;
            if (bBloomEnabled) {
                const auto* finalBloomImage = rgCtx.resolveTexture(finalBloomHandle);
                YA_CORE_ASSERT(finalBloomImage != nullptr && finalBloomImage->getImageView() != nullptr,
                               "Bloom composite pass failed to resolve bloom texture {}", finalBloomHandle.index);
                compositeBloomImageView = finalBloomImage->getImageView();
            }
            writeComposite(compositeDS, sceneImage->getImageView(), compositeBloomImageView);
            rgCtx.getCommandBuffer().bindPipeline(_compositePipeline.get());
            rgCtx.getCommandBuffer().setViewport(0.0f, 0.0f, static_cast<float>(renderExtent.width), static_cast<float>(renderExtent.height));
            rgCtx.getCommandBuffer().setScissor(0, 0, renderExtent.width, renderExtent.height);
            rgCtx.getCommandBuffer().bindDescriptorSets(_compositePPL.get(), 0, {compositeDS});
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
    RenderGraphExecutionResult result;
    [[maybe_unused]] const bool bExecuted = _graphExecutor->execute(graph, *desc.cmdBuf, nullptr, &result);
    if (!bExecuted) {
        clearPreparedResources();
        return;
    }

    capturePreparedResources(result, desc.viewId);
}

} // namespace ya

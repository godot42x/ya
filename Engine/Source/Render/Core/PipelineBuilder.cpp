#include "PipelineBuilder.h"

#include "Render/Core/IRenderTarget.h"
#include "Render/Shader.h"
#include "Runtime/App/App.h"

#include <format>
#include <stdexcept>

namespace ya
{

PipelineBuilder::Result PipelineBuilder::create(IRender* render, const CreateDesc& desc)
{
    YA_CORE_ASSERT(render, "PipelineBuilder::create: render must not be null");
    YA_CORE_ASSERT(!desc.shaderName.empty(), "PipelineBuilder::create: shaderName must not be empty");

    // ------------------------------------------------------------------
    // 1. Build ShaderDesc and load / compile the shader
    // ------------------------------------------------------------------
    ShaderDesc shaderDesc{
        .sourceMode        = ShaderDesc::ESourceMode::SingleShader,
        .shaderName        = desc.shaderName,
        .bDeriveFromShader = true, // enable full shader-driven derivation
        .defines           = desc.defines,
    };

    // Apply manual vertex layout overrides if provided
    if (!desc.vertexBufferDescs.empty()) {
        shaderDesc.vertexBufferDescs = desc.vertexBufferDescs;
        shaderDesc.vertexAttributes  = desc.vertexAttributes;
    }

    auto shaderStorage = App::get()->getShaderStorage();
    auto stage2Spirv   = shaderStorage->load(shaderDesc);
    if (!stage2Spirv) {
        throw std::runtime_error(
            std::format("PipelineBuilder: Failed to load shader '{}'", desc.shaderName));
    }

    // ------------------------------------------------------------------
    // 2. Reflect all stages and merge
    // ------------------------------------------------------------------
    auto                                           processor = shaderStorage->selectProcessor(shaderDesc);
    std::vector<ShaderReflection::ShaderResources> allStageResources;

    for (const auto& [stage, spirv] : *stage2Spirv) {
        allStageResources.push_back(processor->reflect(stage, spirv));
    }

    auto merged = ShaderReflection::merge(allStageResources);

    // ------------------------------------------------------------------
    // 3. Build Descriptor Set Layouts (with optional overrides)
    // ------------------------------------------------------------------
    std::vector<std::shared_ptr<IDescriptorSetLayout>> DSLs;

    for (size_t i = 0; i < merged.descriptorSetLayouts.size(); ++i) {
        // Check if caller provided an override for this set index
        if (i < desc.dslOverrides.size() && desc.dslOverrides[i]) {
            DSLs.push_back(desc.dslOverrides[i]);
        }
        else {
            DSLs.push_back(IDescriptorSetLayout::create(render, merged.descriptorSetLayouts[i]));
        }
    }

    // ------------------------------------------------------------------
    // 4. Build Pipeline Layout (with optional push constant overrides)
    // ------------------------------------------------------------------
    const auto& pushConstants = desc.pushConstantOverrides.empty()
                                  ? merged.pushConstants
                                  : desc.pushConstantOverrides;

    auto pipelineLayout = IPipelineLayout::create(
        render,
        std::format("PipelineBuilder_Layout_{}", desc.shaderName),
        pushConstants,
        DSLs);

    // ------------------------------------------------------------------
    // 5. Build PipelineRenderingInfo from render target or explicit info
    // ------------------------------------------------------------------
    PipelineRenderingInfo renderingInfo;
    IRenderPass*          renderPass = desc.renderPass;
    int32_t               subPassRef = desc.subPassRef;

    if (desc.renderTarget) {
        // Extract format info from render target
        for (const auto& colorDesc : desc.renderTarget->getColorAttachmentDescs()) {
            renderingInfo.colorAttachmentFormats.push_back(colorDesc.format);
        }
        if (desc.renderTarget->getDepthAttachmentDesc().has_value()) {
            renderingInfo.depthAttachmentFormat =
                desc.renderTarget->getDepthAttachmentDesc()->format;
        }

        // If render target has a render pass, use it
        if (desc.renderTarget->getRenderPass()) {
            renderPass = desc.renderTarget->getRenderPass();
            subPassRef = static_cast<int32_t>(desc.renderTarget->getSubpassIndex());
        }
    }
    else if (desc.pipelineRenderingInfo.has_value()) {
        renderingInfo = *desc.pipelineRenderingInfo;
    }

    // ------------------------------------------------------------------
    // 6. Build color blend state (default: one no-blend attachment per
    //    color attachment if caller didn't specify)
    // ------------------------------------------------------------------
    ColorBlendState colorBlendState = desc.colorBlendState;
    if (colorBlendState.attachments.empty()) {
        for (size_t i = 0; i < renderingInfo.colorAttachmentFormats.size(); ++i) {
            colorBlendState.attachments.push_back(ColorBlendAttachmentState{
                .index = static_cast<int>(i),
            });
        }
    }

    // ------------------------------------------------------------------
    // 7. Create the Graphics Pipeline
    // ------------------------------------------------------------------
    auto pipeline = IGraphicsPipeline::create(render);

    GraphicsPipelineCreateInfo gpCI{
        .subPassRef            = subPassRef,
        .renderPass            = renderPass,
        .pipelineRenderingInfo = renderingInfo,
        .pipelineLayout        = pipelineLayout.get(),
        .shaderDesc            = shaderDesc,
        .dynamicFeatures       = desc.dynamicFeatures,
        .primitiveType         = desc.primitiveType,
        .rasterizationState    = desc.rasterizationState,
        .multisampleState      = desc.multisampleState,
        .depthStencilState     = desc.depthStencilState,
        .colorBlendState       = colorBlendState,
        .viewportState         = desc.viewportState,
    };

    if (!pipeline->recreate(gpCI)) {
        throw std::runtime_error(
            std::format("PipelineBuilder: Failed to create pipeline for shader '{}'",
                        desc.shaderName));
    }

    return Result{
        .pipeline             = std::move(pipeline),
        .pipelineLayout       = std::move(pipelineLayout),
        .descriptorSetLayouts = std::move(DSLs),
    };
}

} // namespace ya

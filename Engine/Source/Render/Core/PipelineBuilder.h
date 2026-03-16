#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ya
{

/**
 * @brief High-level API for creating a complete graphics pipeline from a shader name.
 *
 * PipelineBuilder loads the shader, reflects all stages, and automatically creates:
 *   - Descriptor Set Layouts (from uniform buffers + sampled images)
 *   - Pipeline Layout (from push constants + DSLs)
 *   - Graphics Pipeline (with vertex input derived from shader)
 *
 * Usage:
 * @code
 *   auto result = PipelineBuilder::create(render, {
 *       .shaderName        = "DeferredRender/GBufferPass.slang",
 *       .renderTarget      = myRT,
 *       .depthStencilState = { .bDepthTestEnable = true, .bDepthWriteEnable = true },
 *   });
 *   // result.pipeline, result.pipelineLayout, result.descriptorSetLayouts
 * @endcode
 */
struct PipelineBuilder
{
    /// Describes how to create the pipeline.
    struct CreateDesc
    {
        // ---- Required ----
        std::string shaderName; ///< Shader path (e.g. "DeferredRender/GBufferPass.slang")

        // Either provide a render target (for dynamic rendering) ...
        IRenderTarget* renderTarget = nullptr;
        // ... or explicit rendering info
        std::optional<PipelineRenderingInfo> pipelineRenderingInfo;
        // ... or a render pass + subpass for traditional mode
        IRenderPass* renderPass = nullptr;
        int32_t      subPassRef = -1;

        // ---- Optional state overrides (sensible defaults used otherwise) ----
        RasterizationState rasterizationState;
        DepthStencilState  depthStencilState;
        MultisampleState   multisampleState;
        ColorBlendState    colorBlendState;
        ViewportState      viewportState;
        EPrimitiveType::T  primitiveType = EPrimitiveType::TriangleList;

        std::vector<EPipelineDynamicFeature::T> dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
        };

        // ---- Optional shader overrides ----
        std::vector<std::string> defines; ///< Preprocessor defines

        // ---- Optional pipeline layout overrides ----
        /// If non-empty, these DSLs replace the auto-derived ones at the
        /// corresponding set indices.
        std::vector<std::shared_ptr<IDescriptorSetLayout>> dslOverrides;

        /// If non-empty, replaces the auto-derived push constant ranges.
        std::vector<PushConstantRange> pushConstantOverrides;

        /// If non-empty, use manual vertex layout instead of auto-derived.
        std::vector<VertexBufferDescription> vertexBufferDescs;
        std::vector<VertexAttribute>         vertexAttributes;
    };

    /// Result of a successful PipelineBuilder::create() call.
    struct Result
    {
        std::shared_ptr<IGraphicsPipeline>                 pipeline;
        std::shared_ptr<IPipelineLayout>                   pipelineLayout;
        std::vector<std::shared_ptr<IDescriptorSetLayout>> descriptorSetLayouts;
    };

    /**
     * @brief Create a complete graphics pipeline from a shader name.
     * @param render The render backend.
     * @param desc   Description of the pipeline to create.
     * @return Result containing the pipeline, pipeline layout, and DSLs.
     * @throws std::runtime_error if shader loading or pipeline creation fails.
     */
    static Result create(IRender* render, const CreateDesc& desc);
};

} // namespace ya

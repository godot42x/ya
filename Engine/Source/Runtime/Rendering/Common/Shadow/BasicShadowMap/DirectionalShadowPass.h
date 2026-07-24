#pragma once

#include "BasicShadowPayload.h"
#include "Runtime/Rendering/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Image.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Stage/IRenderStage.h"

#include "CombineShadowMappingGenerate.slang.h"

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct RenderFrameData;
struct RenderShadingDrawBuckets;
struct RenderDrawItem;

// ═══════════════════════════════════════════════════════════════════════════
// DirectionalShadowPass
// Renders one directional-light depth map per active cascade using direct draws.
// ═══════════════════════════════════════════════════════════════════════════

class DirectionalShadowPass
{
  public:
    using FrameUBO          = slang_types::CombineShadowMappingGenerate::FrameData;
    using ModelPushConstant = slang_types::CombineShadowMappingGenerate::PushConstants;

    void init(IRender* render, Extent2D shadowExtent);
    void destroy();

    void prepare(const BasicShadowFramePayload& payload);

    void execute(ICommandBuffer* cmdBuf, const BasicShadowFramePayload& payload);
    [[nodiscard]] std::optional<RGPassHandle> appendGraphPasses(
        RenderGraph& graph,
        const BasicShadowFramePayload& payload,
        std::optional<RGPassHandle> dependency = std::nullopt);

    void setShadowExtent(Extent2D extent) { _shadowExtent = extent; }
    void refreshPipeline(EFormat::T depthFormat);
    void setDepthAttachments(stdptr<IImage> image,
                             std::array<stdptr<IImageView>, MAX_DIRECTIONAL_CASCADES> views);
    [[nodiscard]] IImageView* getDepthView() const { return _depthViews[0].get(); }
    [[nodiscard]] IGraphicsPipeline* getStaticPipeline() const { return _staticVariant.pipeline.get(); }
    [[nodiscard]] IGraphicsPipeline* getSkinnedPipeline() const { return _skinnedVariant.pipeline.get(); }

  private:
    struct ShadowPipelineVariant
    {
        stdptr<IGraphicsPipeline> pipeline;
        stdptr<IPipelineLayout>   pipelineLayout;
    };

    struct PerFlightResources
    {
        std::array<stdptr<IBuffer>, MAX_DIRECTIONAL_CASCADES>     frameUBOs{};
        std::array<DescriptorSetHandle, MAX_DIRECTIONAL_CASCADES> frameDSs{};
        stdptr<IBuffer>     skinningSSBO;
        DescriptorSetHandle skinningDS    = nullptr;
    };

    void ensureSkinningCapacity(uint32_t paletteCount);
    [[nodiscard]] std::optional<RGPassHandle> appendCascadePass(
        RenderGraph& graph,
        const BasicShadowFramePayload& payload,
        uint32_t cascadeIndex,
        std::optional<RGPassHandle> dependency);

    IRender* _render       = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};

    ShadowPipelineVariant        _staticVariant;
    ShadowPipelineVariant        _skinnedVariant;
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    stdptr<IDescriptorPool>      _dsp;
    GraphicsPipelineCreateInfo   _pipelineCI{};

    stdptr<IImage> _depthImage;
    std::array<stdptr<IImageView>, MAX_DIRECTIONAL_CASCADES> _depthViews{};
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
    uint32_t _skinningCapacity = 0;
};

} // namespace ya

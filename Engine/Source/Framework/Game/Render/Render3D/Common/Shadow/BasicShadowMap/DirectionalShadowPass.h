#pragma once

#include "BasicShadowPayload.h"
#include "Framework/Game/Render/Graph/RenderGraph.h"
#include "Framework/Game/Render/Render3D/Common/Shadow/ShadowFrameResources.h"
#include "Framework/Game/Render/Render3D/Common/Shadow/ShadowTypes.h"

#include "Foundation/RHI/Core/Buffer.h"
#include "Foundation/RHI/Core/DescriptorSet.h"
#include "Foundation/RHI/Core/Pipeline.h"
#include "Foundation/RHI/Core/Image.h"
#include "Framework/Game/Render/Render3D/Stage/IRenderStage.h"

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

    void init(IRender* render,
              Extent2D shadowExtent,
              ShadowFrameResources& frameResources);
    void destroy();

    void prepare(const BasicShadowFramePayload& payload);

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

    [[nodiscard]] std::optional<RGPassHandle> appendCascadePass(
        RenderGraph& graph,
        const BasicShadowFramePayload& payload,
        uint32_t cascadeIndex,
        std::optional<RGPassHandle> dependency);

    IRender* _render = nullptr;
    ShadowFrameResources* _frameResources = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};

    ShadowPipelineVariant        _staticVariant;
    ShadowPipelineVariant        _skinnedVariant;
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    GraphicsPipelineCreateInfo   _pipelineCI{};

    stdptr<IImage> _depthImage;
    std::array<stdptr<IImageView>, MAX_DIRECTIONAL_CASCADES> _depthViews{};
};

} // namespace ya

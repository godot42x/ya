#pragma once

#include "BasicShadowPayload.h"
#include "PointShadowIndirectRenderer.h"
#include "Runtime/Rendering/Common/Shadow/ShadowFrameResources.h"
#include "Runtime/Rendering/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Image.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

#include "CombineShadowMappingGenerate.slang.h"

#include <memory>

namespace ya
{

struct IRender;
struct ICommandBuffer;
struct IImage;
struct RenderFrameData;
struct RenderShadingDrawBuckets;
struct RenderDrawItem;
struct Mesh;
class RenderGraphExecutor;

// ═══════════════════════════════════════════════════════════════════════════
// PointShadowPass
// Point-light shadow rendering with GPU-driven indirect draw.
// Falls back to direct draw when indirect is unavailable.
// ═══════════════════════════════════════════════════════════════════════════

class PointShadowPass
{
  public:
    using FrameUBO          = BasicShadowFramePayload::FrameUBO;
    using PointFaceUBO      = BasicShadowFramePayload::PointFaceUBO;
    using ModelPushConstant = slang_types::CombineShadowMappingGenerate::PushConstants;

    void init(IRender* render,
              Extent2D shadowExtent,
              ShadowFrameResources& frameResources,
              RenderGraphExecutor* standaloneGraphExecutor);
    void destroy();
    void prepare(const BasicShadowFramePayload& payload);
    void execute(ICommandBuffer* cmdBuf, const BasicShadowFramePayload& payload);
    [[nodiscard]] std::optional<RGPassHandle> appendGraphPasses(
        RenderGraph& graph,
        const BasicShadowFramePayload& payload,
        std::optional<RGPassHandle> dependency = std::nullopt);

    void setShadowExtent(Extent2D extent) { _shadowExtent = extent; }
    void refreshPipeline(EFormat::T depthFormat);
    [[nodiscard]] PointShadowIndirectRenderer& getIndirectRenderer() { return _indirectRenderer; }
    [[nodiscard]] const PointShadowIndirectRenderer& getIndirectRenderer() const { return _indirectRenderer; }
    [[nodiscard]] IGraphicsPipeline* getDirectStaticPipeline() const { return _directStaticVariant.pipeline.get(); }
    [[nodiscard]] IGraphicsPipeline* getDirectSkinnedPipeline() const { return _directSkinnedVariant.pipeline.get(); }

    void rebuildFaceTextures(std::shared_ptr<IImage> shadowImage);

  private:
    // ─── Rendering helpers ───────────────────────────────
    void renderFaceDirect(ICommandBuffer*                 cmdBuf,
                          const BasicShadowFramePayload& payload,
                          const PointShadowFacePayload&  facePayload) const;

    // ─── State ─────────────────────────────────────────────────
    IRender* _render       = nullptr;
    ShadowFrameResources* _frameResources = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};

    // Pipeline: direct draw (reuses CombineShadowMappingGenerate)
    struct ShadowPipelineVariant
    {
        stdptr<IGraphicsPipeline> pipeline;
        stdptr<IPipelineLayout>   pipelineLayout;
    };
    ShadowPipelineVariant _directStaticVariant;
    ShadowPipelineVariant _directSkinnedVariant;

    // Shared descriptor set layouts
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _skinningDSL;

    GraphicsPipelineCreateInfo _directPipelineCI{};

    stdptr<IImage> _shadowImage;
    RenderGraphExecutor* _standaloneGraphExecutor = nullptr;

    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> _faceDepthViews{};

    PointShadowIndirectRenderer _indirectRenderer;

};

} // namespace ya

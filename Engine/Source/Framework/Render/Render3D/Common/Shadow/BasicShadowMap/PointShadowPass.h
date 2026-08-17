#pragma once

#include "BasicShadowPayload.h"
#include "PointShadowIndirectRenderer.h"
#include "Render3D/Common/Shadow/ShadowFrameResources.h"
#include "Render3D/Common/Shadow/ShadowTypes.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Image.h"
#include "RHI/Core/ImageResource.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Stage/IRenderStage.h"

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
              ShadowFrameResources& frameResources);
    void destroy();
    void prepare(const BasicShadowFramePayload& payload);
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

    void rebuildFaceTextures(std::shared_ptr<ImageResource> shadowResource);

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

    stdptr<ImageResource> _shadowResource;
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> _faceDepthViews{};

    PointShadowIndirectRenderer _indirectRenderer;

};

} // namespace ya

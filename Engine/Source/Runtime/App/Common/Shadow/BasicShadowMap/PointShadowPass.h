#pragma once

#include "BasicShadowPayload.h"
#include "PointShadowIndirectRenderer.h"
#include "Runtime/App/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
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

    void init(IRender* render, Extent2D shadowExtent);
    void destroy();

    void prepare(const BasicShadowFramePayload& payload);

    void execute(ICommandBuffer* cmdBuf, const BasicShadowFramePayload& payload);

    void renderGUI();

    void setUseIndirectDraw(bool enabled) { _bUseIndirectDraw = enabled; }

    void setShadowExtent(Extent2D extent) { _shadowExtent = extent; }
    void refreshPipeline(EFormat::T depthFormat);

    [[nodiscard]] Texture* getFaceDepthTexture(uint32_t lightIndex, uint32_t faceIndex) const;

    void rebuildFaceTextures(std::shared_ptr<IImage> shadowImage);

  private:
    // ─── Frame UBO per face ──────────────────────────────────────────
    struct PerFlightResources
    {
        // Per-face UBOs (one UBO per light×face)
        std::array<stdptr<IBuffer>, ShadowConstants::POINT_SHADOW_FACE_COUNT>     faceUBO{};
        std::array<DescriptorSetHandle, ShadowConstants::POINT_SHADOW_FACE_COUNT> faceDS{};

        // Skinning
        stdptr<IBuffer>     skinningSSBO;
        DescriptorSetHandle skinningDS = nullptr;

    };

    // ─── Rendering helpers ───────────────────────────────────────────
    void renderFaceDirect(ICommandBuffer*                 cmdBuf,
                          const BasicShadowFramePayload& payload,
                          const PointShadowFacePayload&  facePayload) const;

    void drawStaticBucketsDirect(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                 const RenderShadingDrawBuckets& buckets, uint32_t layerIndex) const;
    void drawSkinnedBucketsDirect(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                  const RenderShadingDrawBuckets& buckets, uint32_t layerIndex) const;

    void ensureSkinningCapacity(uint32_t paletteCount);

    // ─── State ───────────────────────────────────────────────────────
    IRender* _render       = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};

    bool _bUseIndirectDraw = false; // TODO: enable after indirect path is validated

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
    stdptr<IDescriptorPool>      _dsp;

    GraphicsPipelineCreateInfo _directPipelineCI{};

    // Per-face depth textures
    std::array<std::array<stdptr<Texture>, 6>, MAX_POINT_LIGHTS>    _faceDepthTextures{};
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> _faceDepthViews{};

    PointShadowIndirectRenderer _indirectRenderer;

    // Per-flight
    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
    uint32_t _skinningCapacity = 0;
};

} // namespace ya

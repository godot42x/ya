#pragma once

#include "PointShadowCullPass.h"
#include "Runtime/App/Common/Shadow/ShadowTypes.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/Stage/IRenderStage.h"

#include "CombineShadowMappingGenerate.slang.h"

#include <memory>
#include <unordered_map>
#include <vector>

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
    using FrameUBO          = slang_types::CombineShadowMappingGenerate::FrameData;
    using ModelPushConstant = slang_types::CombineShadowMappingGenerate::PushConstants;

    void init(IRender* render, Extent2D shadowExtent);
    void destroy();

    void prepare(uint32_t               flightIndex,
                 const RenderFrameData& frameData,
                 const FrameUBO&        frameUBO,
                 uint32_t               pointLightCount);

    void execute(ICommandBuffer*        cmdBuf,
                 uint32_t               flightIndex,
                 const RenderFrameData& frameData,
                 uint32_t               pointLightCount);

    void renderGUI();

    void setShadowExtent(Extent2D extent) { _shadowExtent = extent; }
    void refreshPipeline(EFormat::T depthFormat);

    [[nodiscard]] Texture* getFaceDepthTexture(uint32_t lightIndex, uint32_t faceIndex) const;

    void rebuildFaceTextures(std::shared_ptr<IImage> shadowImage);

  private:
    // ─── Instance batching ───────────────────────────────────────────
    struct MeshBatch
    {
        Mesh*    mesh           = nullptr;
        uint32_t firstInstance  = 0;
        uint32_t instanceCount  = 0;
    };

    // ─── Frame UBO per face ──────────────────────────────────────────
    struct PerFlightResources
    {
        // Per-face UBOs (one UBO per light×face)
        std::array<stdptr<IBuffer>, ShadowConstants::POINT_SHADOW_FACE_COUNT>     faceUBO{};
        std::array<DescriptorSetHandle, ShadowConstants::POINT_SHADOW_FACE_COUNT> faceDS{};

        // Skinning
        stdptr<IBuffer>     skinningSSBO;
        DescriptorSetHandle skinningDS = nullptr;

        // Instance buffer for indirect draw
        stdptr<IBuffer>     instanceBuffer;
        DescriptorSetHandle indirectDS = nullptr;

        // Batching state for this frame
        std::vector<MeshBatch> meshBatches;
        uint32_t               totalInstances = 0;
    };

    // ─── Rendering helpers ───────────────────────────────────────────
    void renderFaceDirect(ICommandBuffer*        cmdBuf,
                          uint32_t               flightIndex,
                          const RenderFrameData& frameData,
                          uint32_t               layerIndex) const;

    void renderFaceIndirect(ICommandBuffer* cmdBuf,
                            uint32_t        flightIndex,
                            uint32_t        faceGlobalIndex) const;

    void drawStaticBucketsDirect(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                 const RenderShadingDrawBuckets& buckets, uint32_t layerIndex) const;
    void drawSkinnedBucketsDirect(ICommandBuffer* cmdBuf, uint32_t flightIndex,
                                  const RenderShadingDrawBuckets& buckets, uint32_t layerIndex) const;

    void ensureSkinningCapacity(uint32_t paletteCount);
    void ensureInstanceCapacity(uint32_t requiredCount);
    void updateIndirectDescriptors(uint32_t flightIndex);

    // ─── State ───────────────────────────────────────────────────────
    IRender* _render       = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};

    bool _bUseIndirectDraw   = false; // TODO: enable after indirect path is validated
    bool _bIndirectSupported = false;

    // Pipeline: direct draw (reuses CombineShadowMappingGenerate)
    struct ShadowPipelineVariant
    {
        stdptr<IGraphicsPipeline> pipeline;
        stdptr<IPipelineLayout>   pipelineLayout;
    };
    ShadowPipelineVariant _directStaticVariant;
    ShadowPipelineVariant _directSkinnedVariant;

    // Pipeline: indirect draw (PointShadowIndirect.slang)
    ShadowPipelineVariant _indirectStaticVariant;

    // Shared descriptor set layouts
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    stdptr<IDescriptorSetLayout> _indirectDSL;  // instance + visible indices
    stdptr<IDescriptorPool>      _dsp;

    GraphicsPipelineCreateInfo _directPipelineCI{};
    GraphicsPipelineCreateInfo _indirectPipelineCI{};

    // Per-face depth textures
    std::array<std::array<stdptr<Texture>, 6>, MAX_POINT_LIGHTS>    _faceDepthTextures{};
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS> _faceDepthViews{};

    // Cull pass
    PointShadowCullPass _cullPass;

    // Per-flight
    std::array<PerFlightResources, MAX_FLIGHTS_IN_FLIGHT> _perFlight{};
    uint32_t _skinningCapacity = 0;
    uint32_t _instanceCapacity = 0;

    // Instance grouping map (reused each frame to avoid reallocation)
    std::unordered_map<Mesh*, uint32_t> _meshBatchMap;
};

} // namespace ya

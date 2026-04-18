#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PBRMaterial.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Stage/IRenderStage.h"

#include "DeferredRender.Unified_GBufferPass_PBR.slang.h"
#include "DeferredRender.Unified_GBufferPass_Phong.slang.h"
#include "DeferredRender.Unified_GBufferPass_Unlit.slang.h"

#include <algorithm>

namespace ya
{

struct IRenderTarget;

/// GBuffer stage for the Deferred pipeline.
///
/// Owns per-flight frame/light UBOs and descriptor sets (set 0),
/// plus three shading-model pipelines (PBR, Phong, Unlit) with their
/// MaterialDescPools.
///
/// LightStage reads the same frame/light DS via getFrameAndLightDS().
struct GBufferStage : public IRenderStage
{
    // ── Slang-generated type aliases ─────────────────────────────
    using PBRPushConstant = slang_types::DeferredRender::Unified_GBufferPass_PBR::PushConstants;
    using PBRFrameData    = slang_types::DeferredRender::Unified_GBufferPass_PBR::FrameData;
    using PBRParamUBO     = slang_types::DeferredRender::Unified_GBufferPass_PBR::PBRParamsData;

    using PhongPushConstant = slang_types::DeferredRender::Unified_GBufferPass_Phong::PushConstants;
    using PhongFrameData    = slang_types::DeferredRender::Unified_GBufferPass_Phong::FrameData;
    using PhongParamUBO     = slang_types::DeferredRender::Unified_GBufferPass_Phong::ParamsData;

    using UnlitPushConstant = slang_types::DeferredRender::Unified_GBufferPass_Unlit::PushConstants;
    using UnlitFrameData    = slang_types::DeferredRender::Unified_GBufferPass_Unlit::FrameData;
    using UnlitParamUBO     = slang_types::DeferredRender::Unified_GBufferPass_Unlit::ParamsData;

    // ── GBuffer format constants ─────────────────────────────────
    static constexpr EFormat::T LINEAR_FORMAT        = EFormat::R8G8B8A8_UNORM;
    static constexpr EFormat::T SIGNED_LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T SHADING_MODEL_FORMAT = EFormat::R8_UNORM;
    static constexpr EFormat::T DEPTH_FORMAT         = EFormat::D32_SFLOAT;

    // ── Shared frame + light resources (per-flight) ──────────────
    IRender*                     _render = nullptr;
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;
    stdptr<IDescriptorPool>      _frameAndLightDSP;

    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _frameAndLightDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _frameUBO{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _lightUBO{};

    PBRFrameData _frameData{};
    uint32_t     _maxShadowedPointLights  = 1;
    uint32_t     _lastShadowedPointLights = 0;

    // ── Per-shading-model pipeline + material pool ───────────────
    struct ShadingPipeline
    {
        stdptr<IGraphicsPipeline>    pipeline;
        stdptr<IPipelineLayout>      pipelineLayout;
        stdptr<IDescriptorSetLayout> materialResourceDSL;
        stdptr<IDescriptorSetLayout> materialParamsDSL;
    };

    ShadingPipeline                                _pbr;
    ShadingPipeline                                _phong;
    ShadingPipeline                                _unlit;
    MaterialDescPool<PBRMaterial, PBRParamUBO>     _pbrMatPool;
    MaterialDescPool<PhongMaterial, PhongParamUBO> _phongMatPool;
    MaterialDescPool<UnlitMaterial, UnlitParamUBO> _unlitMatPool;
    UnlitMaterial*                                 _fallbackMaterial = nullptr;

    // ── Common vertex attributes ─────────────────────────────────
    std::vector<VertexAttribute> _commonVertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
        {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
        {.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
    };

    // ── IRenderStage interface ───────────────────────────────────
    GBufferStage() : IRenderStage("GBuffer") {}

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void renderGUI() override;

    // ── Accessors for LightStage ─────────────────────────────────
    [[nodiscard]] DescriptorSetHandle getFrameAndLightDS(uint32_t flightIndex) const
    {
        return _frameAndLightDS[flightIndex];
    }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameAndLightDSL() const { return _frameAndLightDSL; }
    void                                       setMaxShadowedPointLights(uint32_t count) { _maxShadowedPointLights = std::min(count, static_cast<uint32_t>(MAX_POINT_LIGHTS)); }
    [[nodiscard]] uint32_t                     getMaxShadowedPointLights() const { return _maxShadowedPointLights; }
    [[nodiscard]] uint32_t                     getLastShadowedPointLights() const { return _lastShadowedPointLights; }
    void                                       refreshPipelineFormats(const IRenderTarget* gBufferRT);

  private:
    void initSharedResources();
    void initPBR();
    void initPhong();
    void initUnlit();
    void initFallbackMaterial();

    void preparePBR(const RenderFrameData& frameData);
    void preparePhong(const RenderFrameData& frameData);
    void prepareUnlit(const RenderFrameData& frameData);
    void updateFrameUBOs(const RenderStageContext& ctx);

    void drawPBR(const RenderStageContext& ctx);
    void drawPhong(const RenderStageContext& ctx);
    void drawUnlit(const RenderStageContext& ctx);
    void drawFallback(const RenderStageContext& ctx);
};

} // namespace ya

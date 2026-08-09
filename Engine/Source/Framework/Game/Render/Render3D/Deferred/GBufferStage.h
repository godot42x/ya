#pragma once

#include "DeferredAttachmentFormats.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Material/MaterialDescPool.h"
#include "Render3D/Material/PBRMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Render3D/Material/UnlitMaterial.h"
#include "AppServices/ShadowSettings.h"
#include "Render3D/Stage/IRenderStage.h"

#include "DeferredFrameResourceSet.h"
#include "DeferredRender.GBufferPass_PBR.slang.h"
#include "DeferredRender.GBufferPass_Phong.slang.h"
#include "DeferredRender.GBufferPass_Unlit.slang.h"

#include <algorithm>

namespace ya
{

/// GBuffer stage for the Deferred pipeline.
///
/// Consumes the current frame/light binding from DeferredFrameResourceSet,
/// plus three shading-model pipelines (PBR, Phong, Unlit) with their
/// MaterialDescPools.
///
/// LightStage consumes the same frame/light DS from the pipeline resource set.
struct GBufferStage : public IRenderStage
{
    // ── Slang-generated type aliases ─────────────────────────────
    using PBRPushConstant = slang_types::DeferredRender::GBufferPass_PBR::PushConstants;
    using PBRParamUBO     = slang_types::DeferredRender::GBufferPass_PBR::PBRParamsData;

    using PhongPushConstant = slang_types::DeferredRender::GBufferPass_Phong::PushConstants;
    using PhongFrameData    = slang_types::DeferredRender::GBufferPass_Phong::FrameData;
    using PhongParamUBO     = slang_types::DeferredRender::GBufferPass_Phong::ParamsData;

    using UnlitPushConstant = slang_types::DeferredRender::GBufferPass_Unlit::PushConstants;
    using UnlitFrameData    = slang_types::DeferredRender::GBufferPass_Unlit::FrameData;
    using UnlitParamUBO     = slang_types::DeferredRender::GBufferPass_Unlit::ParamsData;

    // ── GBuffer format constants ─────────────────────────────────
    static constexpr EFormat::T LINEAR_FORMAT        = EFormat::R8G8B8A8_UNORM;
    static constexpr EFormat::T SIGNED_LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T SHADING_MODEL_FORMAT = EFormat::R8_UNORM;
    static constexpr EFormat::T DEPTH_FORMAT         = EFormat::D32_SFLOAT;

    // ── Shared frame + light binding (owned by pipeline resource set) ──
    IRender*                     _render = nullptr;
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;

    // ── Per-shading-model pipeline + material pool ───────────────
    struct ShadingPipeline
    {
        stdptr<IGraphicsPipeline>    pipeline;
        stdptr<IPipelineLayout>      pipelineLayout;
        stdptr<IDescriptorSetLayout> materialResourceDSL;
        stdptr<IDescriptorSetLayout> materialParamsDSL;
    };

    ShadingPipeline                                _pbr;
    ShadingPipeline                                _pbrSkinned;
    ShadingPipeline                                _phong;
    ShadingPipeline                                _phongSkinned;
    ShadingPipeline                                _unlit;
    ShadingPipeline                                _unlitSkinned;
    MaterialDescPool<PBRMaterial, PBRParamUBO>     _pbrMatPool;
    MaterialDescPool<PhongMaterial, PhongParamUBO> _phongMatPool;
    MaterialDescPool<UnlitMaterial, UnlitParamUBO> _unlitMatPool;
    UnlitMaterial*                                 _fallbackMaterial = nullptr;

    // Kept alive for graphics pipeline layouts; buffers, descriptor sets and
    // capacity are owned by DeferredFrameResourceSet.
    stdptr<IDescriptorSetLayout> _skinningDSL;

    // ── Common vertex attributes ─────────────────────────────────
    std::vector<VertexAttribute> _commonVertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
        {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
        {.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
    };

    // ── IRenderStage interface ───────────────────────────────────
    struct FrameInputs
    {
        DescriptorSetHandle frameAndLightDescriptorSet{};
        DescriptorSetHandle skinningDescriptorSet{};

        [[nodiscard]] bool isValid() const
        {
            return frameAndLightDescriptorSet && skinningDescriptorSet;
        }
    };

    GBufferStage() : IRenderStage("GBuffer") {}

    void init(IRender* render,
              stdptr<IDescriptorSetLayout> frameAndLightDSL,
              stdptr<IDescriptorSetLayout> skinningDSL);
    void init(IRender* render) override { init(render, nullptr, nullptr); }
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    /// Graph pass entry: explicit current-flight binding. Does not read stage members.
    void execute(const RenderStageContext& ctx, const FrameInputs& inputs);
    /// IRenderStage conformance; graph passes must use the parameterized overload.
    void execute(const RenderStageContext& ctx) override;

    void                                       refreshPipelineFormats(const DeferredAttachmentFormats& formats);
    [[nodiscard]] IGraphicsPipeline*           getPBRPipeline() const { return _pbr.pipeline.get(); }
    [[nodiscard]] IGraphicsPipeline*           getPBRSkinnedPipeline() const { return _pbrSkinned.pipeline.get(); }
    [[nodiscard]] IGraphicsPipeline*           getPhongPipeline() const { return _phong.pipeline.get(); }
    [[nodiscard]] IGraphicsPipeline*           getPhongSkinnedPipeline() const { return _phongSkinned.pipeline.get(); }
    [[nodiscard]] IGraphicsPipeline*           getUnlitPipeline() const { return _unlit.pipeline.get(); }
    [[nodiscard]] IGraphicsPipeline*           getUnlitSkinnedPipeline() const { return _unlitSkinned.pipeline.get(); }

  private:
    void initSharedResources(stdptr<IDescriptorSetLayout> frameAndLightDSL,
                             stdptr<IDescriptorSetLayout> skinningDSL);
    void initPBR();
    void initPhong();
    void initUnlit();
    void initFallbackMaterial();
    void preparePBR(const RenderFrameData& frameData);
    void preparePhong(const RenderFrameData& frameData);
    void prepareUnlit(const RenderFrameData& frameData);

    void drawPBR(const RenderStageContext& ctx, const FrameInputs& inputs);
    void drawPhong(const RenderStageContext& ctx, const FrameInputs& inputs);
    void drawUnlit(const RenderStageContext& ctx, const FrameInputs& inputs);
    void drawFallback(const RenderStageContext& ctx, const FrameInputs& inputs);
};

} // namespace ya

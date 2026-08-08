#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/RenderAttachmentFormats.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Common/IRenderRuntimeServices.h"
#include "Render3D/Common/RenderViewportUtils.h"
#include "Render3D/Forward/ForwardViewportAuxPasses.h"
#include "Render3D/Forward/ForwardFrameResourceSet.h"
#include "Render3D/Forward/ForwardViewportLitPasses.h"
#include "Render3D/Forward/ForwardViewportUnlitPass.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"

#include <array>
#include <glm/glm.hpp>

namespace ya
{

struct EnvironmentLightingProcessor;

/// Forward viewport stage — renders PBR / Phong / Unlit / Simple / Skybox / Debug into the viewport.
///
/// Internalizes all the logic that was previously spread across
/// PhongMaterialSystem, UnlitMaterialSystem, SimpleMaterialSystem,
/// SkyBoxSystem and DebugRenderSystem.
///
/// Consumes RenderFrameData snapshot for draw items.
struct ForwardViewportStage : public IRenderStage
{
    struct InitDesc
    {
        IRender*                              render                               = nullptr;
        IRenderPass*                          renderPass                           = nullptr;
        PipelineRenderingInfo                pipelineRenderingInfo                = {};
        stdptr<IDescriptorSetLayout>          skinningDSL;
        stdptr<IDescriptorSetLayout>          pbrFrameDSL;
        stdptr<IDescriptorSetLayout>          phongFrameDSL;
        stdptr<IDescriptorSetLayout>          unlitFrameDSL;
        stdptr<IDescriptorSetLayout>          skyboxFrameDSL;
        DescriptorSetHandle                  depthBufferShadowDS                  = nullptr;
        ShadowRuntimeState                   shadowState                          = {};
        IRenderRuntimeServices*              runtimeServices                      = nullptr;
    };

    enum class EPass : uint8_t
    {
        Skybox,
        PBR,
        Phong,
        Unlit,
        Simple,
        DirectionOverlay,
        Debug,
    };

    struct PassContext
    {
        using SkyboxInput    = ForwardViewportAuxPasses::DrawContext::SkyboxInput;
        using DebugDrawInput = ForwardViewportAuxPasses::DrawContext::DebugDrawInput;

        const RenderStageContext& stageCtx;
        Scene*                    activeScene             = nullptr;
        EnvironmentLightingProcessor* environmentLightingProcessor = nullptr;
        DescriptorSetHandle       sceneEnvironmentLightingDescriptorSet = nullptr;
        DescriptorSetHandle       skinningDescriptorSet   = nullptr;
        DescriptorSetHandle       pbrFrameDescriptorSet   = nullptr;
        DescriptorSetHandle       phongFrameDescriptorSet = nullptr;
        DescriptorSetHandle       unlitFrameDescriptorSet = nullptr;
        DescriptorSetHandle       skyboxFrameDescriptorSet = nullptr;
        SkyboxInput               skybox{};
        DebugDrawInput            debugDraw{};
        std::vector<ForwardDirectionGizmoInput> directionGizmos{};
    };

    // ═══════════════════════════════════════════════════════════════
    // State
    // ═══════════════════════════════════════════════════════════════

    IRender* _render = nullptr;
    bool     bReverseViewportY = true;

    ShadowRuntimeState _shadowState{};
    ForwardViewportAuxPasses _auxPasses{};
    ForwardViewportLitPasses _litPasses{};
    ForwardViewportUnlitPass _unlitPass{};

    DescriptorSetHandle _depthBufferShadowDS = nullptr;
    ForwardFrameResourceSet::FramePayloads _framePayloads{};
    IRenderRuntimeServices* _runtimeServices = nullptr;

    // Kept alive for graphics pipeline layouts; storage buffers, descriptor
    // sets and capacity are owned by ForwardFrameResourceSet.
    stdptr<IDescriptorSetLayout> _skinningDSL;

    // ═══════════════════════════════════════════════════════════════
    // IRenderStage interface
    // ═══════════════════════════════════════════════════════════════

    ForwardViewportStage() : IRenderStage("ForwardViewport") {}

    void initWithDesc(const InitDesc& desc);
    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    /// IRenderStage conformance entry. Graph passes must use the per-pass
    /// overloads below (FG-702); this bare entry has no current-flight binding.
    void execute(const RenderStageContext& ctx) override;
    /// Per-pass graph entries (FG-702): the top-level Forward graph declares
    /// skybox / PBR / Phong / rest as separate passes, so the stage no longer
    /// hides pass order inside one fixed loop. Explicit current-flight binding,
    /// does not read stage members for per-flight resources.
    void executeSkybox(const RenderStageContext& ctx, const ForwardFrameResourceSet::Binding& binding, const PassContext* snapshot = nullptr);
    void executePBR(const RenderStageContext& ctx, const ForwardFrameResourceSet::Binding& binding, const PassContext* snapshot = nullptr);
    void executePhong(const RenderStageContext& ctx, const ForwardFrameResourceSet::Binding& binding, const PassContext* snapshot = nullptr);
    void executeUnlit(const RenderStageContext& ctx, const ForwardFrameResourceSet::Binding& binding, const PassContext* snapshot = nullptr);
    void executeSimple(const RenderStageContext& ctx, const PassContext* snapshot = nullptr);
    /// Direction overlay draws only the prebuilt gizmo snapshot (FG-704).
    void executeDirection(const RenderStageContext& ctx, std::vector<ForwardDirectionGizmoInput> directionGizmos, const PassContext* snapshot = nullptr);
    void executeDebug(const RenderStageContext& ctx, const PassContext* snapshot = nullptr);

    void applyShadowState(const ShadowRuntimeState& shadowState);
    void setDepthBufferShadowDescriptorSet(DescriptorSetHandle depthBufferShadowDS);
    void refreshPipelineFormats(const RenderAttachmentFormats& formats);
    [[nodiscard]] ForwardViewportAuxPasses&       getAuxPasses() { return _auxPasses; }
    [[nodiscard]] const ForwardViewportAuxPasses& getAuxPasses() const { return _auxPasses; }
    [[nodiscard]] ForwardViewportLitPasses&       getLitPasses() { return _litPasses; }
    [[nodiscard]] const ForwardViewportLitPasses& getLitPasses() const { return _litPasses; }
    [[nodiscard]] ForwardViewportUnlitPass&       getUnlitPass() { return _unlitPass; }
    [[nodiscard]] const ForwardViewportUnlitPass& getUnlitPass() const { return _unlitPass; }
    [[nodiscard]] const ForwardFrameResourceSet::FramePayloads& getFramePayloads() const { return _framePayloads; }
    [[nodiscard]] PassContext buildPassContext(const RenderStageContext& ctx);

  private:
    [[nodiscard]] PassContext::SkyboxInput buildSkyboxInput(Scene* activeScene, EnvironmentLightingProcessor* envProcessor) const;
    [[nodiscard]] PassContext::DebugDrawInput buildDebugDrawInput(const RenderFrameData* frameData) const;
    [[nodiscard]] ForwardViewportAuxPasses::DrawContext makeAuxDrawContext(
        const PassContext& passCtx,
        bool               bIncludeSkybox = false,
        bool               bIncludeDebug = false,
        bool               bIncludeDirection = false) const;
    [[nodiscard]] ForwardViewportLitPasses::DrawContext makePBRDrawContext(const PassContext& passCtx) const;
    [[nodiscard]] ForwardViewportLitPasses::DrawContext makePhongDrawContext(const PassContext& passCtx) const;
    [[nodiscard]] ForwardViewportUnlitPass::DrawContext makeUnlitDrawContext(const PassContext& passCtx) const;
    void                                            executePass(EPass pass, const PassContext& passCtx);
};

} // namespace ya

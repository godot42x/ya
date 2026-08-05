#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderAttachmentFormats.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/Rendering/Forward/ForwardViewportAuxPasses.h"
#include "Runtime/Rendering/Forward/ForwardFrameResourceSet.h"
#include "Runtime/Rendering/Forward/ForwardViewportLitPasses.h"
#include "Runtime/Rendering/Forward/ForwardViewportUnlitPass.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowRuntimeState.h"

#include <array>
#include <functional>
#include <glm/glm.hpp>

namespace ya
{

struct Scene;
class ResourceResolveSystem;

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
        std::function<uint64_t()>            getFrameIndex;
        std::function<double()>              getElapsedTimeSeconds;
        std::function<Scene*()>              getActiveScene;
        std::function<ResourceResolveSystem*()> getResourceResolveSystem;
        std::function<DescriptorSetHandle(Scene*)> getSceneSkyboxDescriptorSet;
        std::function<DescriptorSetHandle(Scene*)> getSceneEnvironmentLightingDescriptorSet;
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
        struct SkyboxInput
        {
            bool                bAvailable     = false;
            DescriptorSetHandle descriptorSet  = nullptr;
            Mesh*               mesh           = nullptr;
        };

        struct DebugDrawInput
        {
            struct Bucket
            {
                const std::vector<RenderDrawItem>* items = nullptr;
                bool                               bSkinned = false;
            };

            std::array<Bucket, 10> buckets{};
            uint32_t               count     = 0;
            bool                   bHasDraws = false;
        };

        const RenderStageContext& stageCtx;
        Scene*                    activeScene             = nullptr;
        ResourceResolveSystem*    resourceResolveSystem   = nullptr;
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
    std::function<uint64_t()>            _getFrameIndex;
    std::function<double()>              _getElapsedTimeSeconds;
    std::function<Scene*()>              _getActiveScene;
    std::function<ResourceResolveSystem*()> _getResourceResolveSystem;
    std::function<DescriptorSetHandle(Scene*)> _getSceneSkyboxDescriptorSet;
    std::function<DescriptorSetHandle(Scene*)> _getSceneEnvironmentLightingDescriptorSet;

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
    void                      executePass(EPass pass, const PassContext& passCtx);

    // Helpers
    void setViewportAndScissor(ICommandBuffer* cmdBuf, uint32_t w, uint32_t h);
};

} // namespace ya

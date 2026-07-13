#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/RenderAttachmentFormats.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/App/ForwardRender/ForwardViewportAuxPasses.h"
#include "Runtime/App/ForwardRender/ForwardViewportLitPasses.h"
#include "Runtime/App/ForwardRender/ForwardViewportUnlitPass.h"
#include "Runtime/App/Common/Shadow/Common/ShadowRuntimeState.h"

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
        SkyboxInput               skybox{};
        DebugDrawInput            debugDraw{};
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
    std::function<uint64_t()>            _getFrameIndex;
    std::function<double()>              _getElapsedTimeSeconds;
    std::function<Scene*()>              _getActiveScene;
    std::function<ResourceResolveSystem*()> _getResourceResolveSystem;
    std::function<DescriptorSetHandle(Scene*)> _getSceneSkyboxDescriptorSet;
    std::function<DescriptorSetHandle(Scene*)> _getSceneEnvironmentLightingDescriptorSet;

    stdptr<IDescriptorSetLayout> _skinningDSL;
    stdptr<IDescriptorPool>      _skinningDSP;
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _skinningDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _skinningSSBO{};
    uint32_t                                           _skinningCapacity = 0;

    // ═══════════════════════════════════════════════════════════════
    // IRenderStage interface
    // ═══════════════════════════════════════════════════════════════

    ForwardViewportStage() : IRenderStage("ForwardViewport") {}

    void initWithDesc(const InitDesc& desc);
    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void renderGUI() override;

    void applyShadowState(const ShadowRuntimeState& shadowState);
    void setDepthBufferShadowDescriptorSet(DescriptorSetHandle depthBufferShadowDS);
    void refreshPipelineFormats(const RenderAttachmentFormats& formats);

  private:
    void initSkinningResources();
    void ensureSkinningCapacity(uint32_t paletteCount);

    void updateSkinningBuffer(const RenderStageContext& ctx);
    [[nodiscard]] PassContext buildPassContext(const RenderStageContext& ctx);
    void                      executePasses(const PassContext& passCtx);
    void                      executePass(EPass pass, const PassContext& passCtx);

    // Helpers
    void setViewportAndScissor(ICommandBuffer* cmdBuf, uint32_t w, uint32_t h);
};

} // namespace ya

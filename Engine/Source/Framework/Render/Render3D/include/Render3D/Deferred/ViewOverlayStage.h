#pragma once

#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "Render3D/Common/ViewOverlayInputs.h"
#include "Render3D/Common/ViewPassResources.h"
#include "Render3D/Pipelines/DebugSkinning.h"
#include "Render3D/Services/DebugRenderSystem.h"
#include "Render3D/Stage/IRenderStage.h"

#include <glm/glm.hpp>

namespace ya
{

struct Mesh;
struct RenderFrameData;

/// Billboard/gizmo/SimpleMaterial overlay device object.
///
/// The per-View packet it draws from is `ViewOverlayFrameInputs`, assembled by
/// the pipeline; the skybox's pipeline lives on `SkyboxStage`, which the skybox
/// feature owns. The two used to share one stage object, which made each
/// feature's device half invisible in the other's header.
struct YA_RENDER_3D_API ViewOverlayStage : public IRenderStage
{
    struct OverlayPushConstant
    {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        int       colorType;
    };

    struct BillboardFrameUBO
    {
        glm::mat4 viewProjection = glm::mat4(1.0f);
        glm::mat4 view           = glm::mat4(1.0f);
    };

    struct BillboardPushConstant
    {
        glm::vec3 worldCenter    = glm::vec3(0.0f);
        uint32_t  textureIndex   = 0;
        glm::vec3 worldDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        uint32_t  _pad0          = 0;
        glm::vec2 worldSize      = glm::vec2(1.0f);
        glm::vec2 _pad1          = glm::vec2(0.0f);
        glm::vec4 tint           = glm::vec4(1.0f);
    };

    /// Matches Misc/EditorWorldGrid.slang. Two rows of viewProjection stand in
    /// for the full matrix so the block stays inside the 128-byte push limit.
    struct WorldGridPushConstant
    {
        glm::mat4 invViewProjection = glm::mat4(1.0f);
        glm::vec4 clipZRow          = glm::vec4(0.0f);
        glm::vec4 clipWRow          = glm::vec4(0.0f);
        glm::vec4 cameraPos         = glm::vec4(0.0f);
    };
    static_assert(sizeof(WorldGridPushConstant) <= 128);

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;

    IRender* _render = nullptr;

    stdptr<IGraphicsPipeline>    _billboardPipeline;
    stdptr<IPipelineLayout>      _billboardPPL;
    stdptr<IDescriptorSetLayout> _billboardFrameDSL;
    stdptr<IDescriptorSetLayout> _billboardTextureDSL;
    Mesh* _billboardMesh = nullptr;

    stdptr<IGraphicsPipeline> _worldGridPipeline;
    stdptr<IPipelineLayout>   _worldGridPPL;

    bool bReverseViewportY = true;

    // ── Overlay pipeline (push constant only) ────────────────────
    stdptr<IGraphicsPipeline> _overlayPipeline;
    stdptr<IPipelineLayout>   _overlayPPL;
    int                       _defaultColorType = 0;
    OverlayPushConstant       _overlayPC{};
    Mesh*                     _directionCone = nullptr;
    Mesh*                     _directionCylinder = nullptr;

    DebugRenderSystem*                     _debugRenderSystem = nullptr;
    DebugSkinning                          _debugSkinning;

    // ── IRenderStage ─────────────────────────────────────────────
    ViewOverlayStage() : IRenderStage("ViewportOverlay") {}

    void init(IRender* render) override;
    void destroy() override;
    /// Submission-scoped PSO / debug-system warmup; the stage holds no
    /// per-View state.
    void beginFrame();
    /// IRenderStage conformance; graph passes must use executeOverlay.
    void execute(const RenderStageContext& ctx) override;
    void executeOverlay(const RenderStageContext& ctx, const ViewOverlayFrameInputs& frameInputs, const OverlayPassBindings& overlay);
    void updateBillboardTextures(ViewOverlayFrameInputs& frameInputs, OverlayPassBindings& overlay);
    void refreshPipelineFormats(const DeferredAttachmentFormats& formats);
    void setDebugRenderSystem(DebugRenderSystem* debugRenderSystem);
    [[nodiscard]] IGraphicsPipeline* getOverlayPipeline() const { return _overlayPipeline.get(); }
    [[nodiscard]] DebugRenderSystem* getDebugRenderSystem() const { return _debugRenderSystem; }
    [[nodiscard]] DebugSkinning&       getDebugSkinning() { return _debugSkinning; }
    [[nodiscard]] const DebugSkinning& getDebugSkinning() const { return _debugSkinning; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getBillboardFrameDSL() const { return _billboardFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getBillboardTextureDSL() const { return _billboardTextureDSL; }
    static constexpr uint32_t kBillboardTextureCount = 16;

  private:
    void initBillboards();
    void initWorldGrid();
    void initOverlay();
    uint32_t resolveBillboardTextureIndex(std::vector<TextureBinding>& bindings, const TextureBinding& binding);
    void drawWorldGrid(const RenderStageContext& ctx);
    void drawBillboards(const RenderStageContext& ctx, const ViewOverlayFrameInputs& frameInputs, const OverlayPassBindings& overlay);
    void drawOverlay(const RenderStageContext& ctx, const ViewOverlayFrameInputs& frameInputs);
};

} // namespace ya

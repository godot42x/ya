#pragma once

#include "DeferredAttachmentFormats.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Pipelines/DebugSkinning.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/Rendering/Services/DebugRenderSystem.h"

#include <functional>
#include <glm/glm.hpp>

namespace ya
{

struct Mesh;

/// Deferred viewport overlay stage — Skybox background + SimpleMaterial debug overlay.
///
/// Skybox: per-flight frame UBO (view/proj without translation) + cubemap DS from RenderRuntime.
/// Overlay: push constant only (view/proj/model/colorType), no UBO/DS.
struct ViewportOverlayStage : public IRenderStage
{
    struct Services
    {
        std::function<DebugRenderSystem&()>        getDebugRenderSystem;
    };

    struct FrameInputs
    {
        struct DirectionGizmoInput
        {
            glm::mat4 coneModel     = glm::mat4(1.0f);
            glm::mat4 cylinderModel = glm::mat4(1.0f);
            glm::vec3 lineStart     = glm::vec3(0.0f);
            glm::vec3 lineEnd       = glm::vec3(0.0f);
        };

        struct SkyboxInput
        {
            bool                bAvailable    = false;
            DescriptorSetHandle descriptorSet = nullptr;
            Mesh*               mesh          = nullptr;
        };

        std::vector<DirectionGizmoInput> directionGizmos{};
        SkyboxInput            skybox{};
    };

    struct SkyboxFrameUBO
    {
        glm::mat4 projection;
        glm::mat4 view;
    };

    struct OverlayPushConstant
    {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        int       colorType;
    };

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;

    IRender* _render = nullptr;

    // ── Skybox pipeline ──────────────────────────────────────────
    stdptr<IGraphicsPipeline>    _skyboxPipeline;
    stdptr<IPipelineLayout>      _skyboxPPL;
    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    stdptr<IDescriptorSetLayout> _skyboxResourceDSL;
    stdptr<IDescriptorPool>      _skyboxDSP;

    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _skyboxFrameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _skyboxFrameUBO{};

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

    std::function<DebugRenderSystem&()>        _getDebugRenderSystem;
    FrameInputs                                _frameInputs{};

    // ── IRenderStage ─────────────────────────────────────────────
    ViewportOverlayStage() : IRenderStage("ViewportOverlay") {}

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void executeSkybox(const RenderStageContext& ctx);
    void executeOverlay(const RenderStageContext& ctx);
    void refreshPipelineFormats(const DeferredAttachmentFormats& formats);
    void setServices(Services services);
    void setFrameInputs(FrameInputs frameInputs);
    [[nodiscard]] IGraphicsPipeline* getSkyboxPipeline() const { return _skyboxPipeline.get(); }
    [[nodiscard]] IGraphicsPipeline* getOverlayPipeline() const { return _overlayPipeline.get(); }
    [[nodiscard]] DebugRenderSystem* getDebugRenderSystem() const { return _debugRenderSystem; }
    [[nodiscard]] DebugSkinning&       getDebugSkinning() { return _debugSkinning; }
    [[nodiscard]] const DebugSkinning& getDebugSkinning() const { return _debugSkinning; }

  private:
    void initSkybox();
    void initOverlay();
    void drawSkybox(const RenderStageContext& ctx);
    void drawOverlay(const RenderStageContext& ctx);
};

} // namespace ya

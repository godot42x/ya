#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

#include <glm/glm.hpp>

namespace ya
{

/// Deferred viewport overlay stage — Skybox background + SimpleMaterial debug overlay.
///
/// Skybox: per-flight frame UBO (view/proj without translation) + cubemap DS from RenderRuntime.
/// Overlay: push constant only (view/proj/model/colorType), no UBO/DS.
struct ViewportOverlayStage : public IRenderStage
{
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

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R8G8B8A8_UNORM;
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

    // ── IRenderStage ─────────────────────────────────────────────
    ViewportOverlayStage() : IRenderStage("ViewportOverlay") {}

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void renderGUI() override;

  private:
    void initSkybox();
    void initOverlay();
    void drawSkybox(const RenderStageContext& ctx);
    void drawOverlay(const RenderStageContext& ctx);
};

} // namespace ya

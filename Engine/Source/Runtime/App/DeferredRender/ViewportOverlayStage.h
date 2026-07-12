#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Pipelines/DebugSkinning.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/App/DebugRenderSystem.h"

#include <functional>
#include <glm/glm.hpp>

namespace ya
{

struct IRenderTarget;
class ResourceResolveSystem;
struct Scene;

/// Deferred viewport overlay stage — Skybox background + SimpleMaterial debug overlay.
///
/// Skybox: per-flight frame UBO (view/proj without translation) + cubemap DS from RenderRuntime.
/// Overlay: push constant only (view/proj/model/colorType), no UBO/DS.
struct ViewportOverlayStage : public IRenderStage
{
    struct Services
    {
        std::function<DescriptorSetHandle(Scene*)> getSceneSkyboxDescriptorSet;
        std::function<DebugRenderSystem&()>        getDebugRenderSystem;
        std::function<Scene*()>                    getActiveScene;
        std::function<ResourceResolveSystem*()>    getResourceResolveSystem;
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

    DebugSkinning   _debugSkinning;

    std::function<DescriptorSetHandle(Scene*)> _getSceneSkyboxDescriptorSet;
    std::function<DebugRenderSystem&()>        _getDebugRenderSystem;
    std::function<Scene*()>                    _getActiveScene;
    std::function<ResourceResolveSystem*()>    _getResourceResolveSystem;

    // ── IRenderStage ─────────────────────────────────────────────
    ViewportOverlayStage() : IRenderStage("ViewportOverlay") {}

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void renderGUI() override;
    void refreshPipelineFormats(const IRenderTarget* viewportRT);
    void setServices(Services services);

  private:
    void initSkybox();
    void initOverlay();
    void drawSkybox(const RenderStageContext& ctx);
    void drawOverlay(const RenderStageContext& ctx);
};

} // namespace ya

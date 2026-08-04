#pragma once

#include "DeferredAttachmentFormats.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Pipelines/DebugSkinning.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/Rendering/Services/DebugRenderSystem.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"

#include "GLSL.Skybox.glsl.h"

#include <functional>
#include <glm/glm.hpp>

namespace ya
{

struct Mesh;
struct BillboardComponent;

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
        struct BillboardInput
        {
            glm::vec3      worldCenter    = glm::vec3(0.0f);
            glm::vec3      worldDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec2      worldSize      = glm::vec2(1.0f);
            glm::vec4      tint           = glm::vec4(1.0f);
            TextureBinding textureBinding{};
        };

        struct DirectionGizmoInput
        {
            glm::mat4 coneModel     = glm::mat4(1.0f);
            glm::mat4 cylinderModel = glm::mat4(1.0f);
            glm::vec3 lineStart     = glm::vec3(0.0f);
            glm::vec3 lineEnd       = glm::vec3(0.0f);
        };

        struct SkyboxInput
        {
            bool                bAvailable         = false;
            DescriptorSetHandle descriptorSet      = nullptr;
            DescriptorSetHandle frameDescriptorSet = nullptr;
            Mesh*               mesh               = nullptr;
        };

        std::vector<BillboardInput>      billboards{};
        std::vector<DirectionGizmoInput> directionGizmos{};
        SkyboxInput            skybox{};
    };

    using SkyboxFrameUBO = glsl_types::GLSL::Skybox::FrameUBO;

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

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;

    IRender* _render = nullptr;

    // ── Skybox pipeline ──────────────────────────────────────────
    stdptr<IGraphicsPipeline>    _skyboxPipeline;
    stdptr<IPipelineLayout>      _skyboxPPL;
    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    stdptr<IDescriptorSetLayout> _skyboxResourceDSL;

    stdptr<IGraphicsPipeline>    _billboardPipeline;
    stdptr<IPipelineLayout>      _billboardPPL;
    stdptr<IDescriptorSetLayout> _billboardFrameDSL;
    stdptr<IDescriptorSetLayout> _billboardTextureDSL;
    stdptr<IDescriptorPool>      _billboardDSP;
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _billboardFrameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _billboardFrameUBO{};
    DescriptorSetHandle _billboardTextureDS{};
    Mesh* _billboardMesh = nullptr;
    std::vector<TextureBinding> _billboardTextureBindings{};

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

    void init(IRender* render, stdptr<IDescriptorSetLayout> skyboxFrameDSL);
    void init(IRender* render) override { init(render, nullptr); }
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void executeSkybox(const RenderStageContext& ctx);
    void executeSkybox(const RenderStageContext& ctx, const FrameInputs::SkyboxInput& skyboxInput);
    void executeOverlay(const RenderStageContext& ctx);
    void executeOverlay(const RenderStageContext& ctx, const FrameInputs& frameInputs);
    void refreshPipelineFormats(const DeferredAttachmentFormats& formats);
    void setServices(Services services);
    void setFrameInputs(FrameInputs frameInputs);
    [[nodiscard]] const FrameInputs& getFrameInputs() const { return _frameInputs; }
    void setSkyboxFrameDescriptorSet(DescriptorSetHandle descriptorSet)
    {
        _frameInputs.skybox.frameDescriptorSet = descriptorSet;
    }
    [[nodiscard]] SkyboxFrameUBO buildSkyboxFrameData(const RenderStageContext& ctx) const;
    [[nodiscard]] IGraphicsPipeline* getSkyboxPipeline() const { return _skyboxPipeline.get(); }
    [[nodiscard]] IGraphicsPipeline* getOverlayPipeline() const { return _overlayPipeline.get(); }
    [[nodiscard]] DebugRenderSystem* getDebugRenderSystem() const { return _debugRenderSystem; }
    [[nodiscard]] DebugSkinning&       getDebugSkinning() { return _debugSkinning; }
    [[nodiscard]] const DebugSkinning& getDebugSkinning() const { return _debugSkinning; }

  private:
    void initSkybox(stdptr<IDescriptorSetLayout> skyboxFrameDSL);
    void initOverlay();
    void initBillboards();
    void updateBillboardTextures();
    uint32_t resolveBillboardTextureIndex(const TextureBinding& binding);
    void drawBillboards(const RenderStageContext& ctx, const FrameInputs& frameInputs);
    void drawSkybox(const RenderStageContext& ctx, const FrameInputs::SkyboxInput& skyboxInput);
    void drawOverlay(const RenderStageContext& ctx, const FrameInputs& frameInputs);
};

} // namespace ya

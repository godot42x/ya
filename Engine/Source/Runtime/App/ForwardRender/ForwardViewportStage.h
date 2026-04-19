#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Material/SimpleMaterial.h"
#include "Render/Stage/IRenderStage.h"

#include "PhongLit.Types.glsl.h"
#include "Test.Unlit.glsl.h"

#include <array>
#include <glm/glm.hpp>

namespace ya
{

/// Forward viewport stage — renders Phong / Unlit / Simple / Skybox / Debug into the viewport.
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
        IRender*             render                = nullptr;
        IRenderPass*         renderPass            = nullptr;
        PipelineRenderingInfo pipelineRenderingInfo = {};
        DescriptorSetHandle  depthBufferShadowDS   = nullptr;
        bool                 bShadowMapping        = true;
    };

    // ── Phong UBO type aliases (from shader-generated headers) ──
    using PhongFrameUBO      = glsl_types::PhongLit::Types::FrameData;
    using PhongLightUBO      = glsl_types::PhongLit::Types::LightData;
    using PhongDebugUBO      = glsl_types::PhongLit::Types::DebugData;
    using PhongDirLight      = glsl_types::PhongLit::Types::DirectionalLight;
    using PhongPointLight    = glsl_types::PhongLit::Types::PointLight;

    struct PhongModelPC
    {
        glm::mat4 modelMat;
    };

    // ── Unlit UBO type aliases ──
    using UnlitFrameUBO = glsl_types::Test::Unlit::FrameUBO;

    struct UnlitPC
    {
        alignas(16) glm::mat4 modelMatrix{1.0f};
        alignas(16) glm::mat3 normalMatrix{1.0f};
    };

    // ── Simple (push constant only) ──
    struct SimplePC
    {
        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 view       = glm::mat4(1.0f);
        glm::mat4 model      = glm::mat4(1.0f);
        uint32_t  colorType  = 0;
    };

    // ── Skybox UBO ──
    struct SkyboxFrameUBO
    {
        glm::mat4 projection;
        glm::mat4 view;
    };

    // ── Debug UBO ──
    struct DebugUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution{0, 0};
        alignas(4) int mode   = 0;
        alignas(4) float time = 0.f;
        glm::vec4 floatParam  = glm::vec4(0.0f);
    };

    enum EDebugMode
    {
        DebugNone = 0,
        DebugNormalColor,
        DebugNormalDir,
        DebugDepth,
        DebugUV,
    };

    struct DebugModelPC
    {
        glm::mat4 modelMat;
    };

    // ═══════════════════════════════════════════════════════════════
    // State
    // ═══════════════════════════════════════════════════════════════

    IRender* _render = nullptr;
    bool     bReverseViewportY = true;

    // ── Phong pipeline ──────────────────────────────────────────
    stdptr<IDescriptorSetLayout> _phongFrameDSL;       // set 0: frame+light+debug
    stdptr<IDescriptorSetLayout> _phongResourceDSL;    // set 1: textures
    stdptr<IDescriptorSetLayout> _phongParamDSL;       // set 2: params
    // set 3: skybox cubemap (reuse _skyboxResourceDSL)
    // set 4: shadow map DS  (external, from ForwardRenderPipeline)
    stdptr<IPipelineLayout>      _phongPPL;
    stdptr<IGraphicsPipeline>    _phongPipeline;
    GraphicsPipelineCreateInfo   _phongPipelineCI;

    stdptr<IDescriptorPool>      _phongFrameDSP;
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _phongFrameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _phongFrameUBO{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _phongLightUBO{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _phongDebugUBO{};

    MaterialDescPool<PhongMaterial, PhongMaterial::ParamUBO> _phongMatPool;
    bool _phongPoolRecreated = false;

    PhongLightUBO  _phongLight{};
    PhongDebugUBO  _phongDebug{};

    DescriptorSetHandle _depthBufferShadowDS = nullptr;
    bool                _bShadowMapping      = true;

    // ── Unlit pipeline ──────────────────────────────────────────
    stdptr<IDescriptorSetLayout> _unlitFrameDSL;
    stdptr<IDescriptorSetLayout> _unlitParamDSL;
    stdptr<IDescriptorSetLayout> _unlitResourceDSL;
    stdptr<IPipelineLayout>      _unlitPPL;
    stdptr<IGraphicsPipeline>    _unlitPipeline;

    static constexpr uint32_t UNLIT_FRAME_SLOTS = 8;
    uint32_t                  _unlitFrameSlot = 0;
    stdptr<IDescriptorPool>   _unlitFrameDSP;
    DescriptorSetHandle       _unlitFrameDSs[UNLIT_FRAME_SLOTS]{};
    stdptr<IBuffer>           _unlitFrameUBOs[UNLIT_FRAME_SLOTS]{};

    MaterialDescPool<UnlitMaterial, UnlitMaterial::ParamUBO> _unlitMatPool;
    bool _unlitPoolRecreated = false;

    // ── Simple pipeline (push constant only) ────────────────────
    stdptr<IPipelineLayout>   _simplePPL;
    stdptr<IGraphicsPipeline> _simplePipeline;
    int                       _simpleDefaultColorType = 0;

    // ── Skybox pipeline ─────────────────────────────────────────
    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    stdptr<IDescriptorSetLayout> _skyboxResourceDSL;
    stdptr<IPipelineLayout>      _skyboxPPL;
    stdptr<IGraphicsPipeline>    _skyboxPipeline;
    stdptr<IDescriptorPool>      _skyboxDSP;

    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _skyboxFrameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _skyboxFrameUBO{};

    // ── Debug pipeline ──────────────────────────────────────────
    stdptr<IDescriptorSetLayout> _debugDSL;
    stdptr<IPipelineLayout>      _debugPPL;
    stdptr<IGraphicsPipeline>    _debugPipeline;
    GraphicsPipelineCreateInfo   _debugPipelineCI;
    stdptr<IDescriptorPool>      _debugDSP;
    DescriptorSetHandle          _debugUboDS = nullptr;
    stdptr<IBuffer>              _debugUboBuffer;

    DebugUBO   _debugUBO{};
    EDebugMode _debugMode = DebugNone;

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

    void setShadowMappingEnabled(bool enabled);
    void refreshPipelineFormats(const IRenderTarget* viewportRT);

  private:
    void initPhong(const InitDesc& desc);
    void initUnlit(const InitDesc& desc);
    void initSimple(const InitDesc& desc);
    void initSkybox(const InitDesc& desc);
    void initDebug(const InitDesc& desc);

    void preparePhong(const RenderStageContext& ctx);
    void prepareUnlit(const RenderStageContext& ctx);

    void drawSkybox(const RenderStageContext& ctx);
    void drawPhong(const RenderStageContext& ctx);
    void drawUnlit(const RenderStageContext& ctx);
    void drawSimple(const RenderStageContext& ctx);
    void drawDebug(const RenderStageContext& ctx);

    // Helpers
    void setViewportAndScissor(ICommandBuffer* cmdBuf, uint32_t w, uint32_t h);
    void fillPhongLightFromFrameData(const RenderFrameData& fd);
    DescriptorImageInfo getDescriptorImageInfo(const TextureBinding& tb);
};

} // namespace ya

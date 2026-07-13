#pragma once

#include "DeferredAttachmentFormats.h"
#include "DeferredGBufferResources.h"
#include "Runtime/App/Common/Shadow/Common/ShadowRuntimeState.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

#include "DeferredRender.LightPass.slang.h"

#include <array>
#include <functional>

namespace ya
{

struct GBufferStage;
struct Scene;
struct Texture;
struct RenderImage;

/// Deferred light pass — fullscreen quad that reads GBuffer textures and computes lighting.
///
/// Uses the frame+light DS from GBufferStage (set 0), its own GBuffer texture DS (set 1),
/// and the environment lighting DS from RenderRuntime (set 2).
struct LightStage : public IRenderStage
{
    struct EnvironmentLightingInput
    {
        stdptr<IDescriptorSetLayout> environmentLightingDSL = nullptr;
        std::function<DescriptorSetHandle(Scene*)> getSceneEnvironmentLightingDescriptorSet;
    };

    struct FrameInputs
    {
        DescriptorSetHandle environmentLightingDescriptorSet = nullptr;
    };

    using PushConstant = slang_types::DeferredRender::LightPass::PushConstants;
    using LightData    = slang_types::DeferredRender::LightPass::LightData;

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;

    IRender*                  _render           = nullptr;
    GBufferStage*             _gBufferStage     = nullptr; // borrows frame+light DS
    DeferredGBufferResources  _gBufferResources{};
    RenderImage*              _ssaoTexture      = nullptr;

    // Pipeline (shared across flights)
    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _gBufferTextureDSL;
    GraphicsPipelineCreateInfo   _pipelineCI{};
    bool                         _bEnablePBRDiffuseIBL  = true;
    bool                         _bEnablePBRSpecularIBL = true;
    ShadowRuntimeState           _shadowState{};

    // GBuffer texture DS + pool (updated each frame from GBuffer RT)
    stdptr<IDescriptorPool> _dsp;
    DescriptorSetHandle     _gBufferTextureDS = nullptr;
    stdptr<IDescriptorSetLayout> _shadowDSL;
    DescriptorSetHandle          _shadowDS = nullptr;

    std::array<ImageViewHandle, 4> _lastGBufferImageViewHandles{};
    ImageViewHandle                _lastGBufferDepthImageViewHandle = nullptr;
    ImageViewHandle _lastSSAOImageViewHandle = nullptr;
    ImageViewHandle _lastShadowDirectionalImageViewHandle = nullptr;
    std::array<ImageViewHandle, MAX_POINT_LIGHTS> _lastShadowPointCubeImageViewHandles{};
    bool _bGBufferDescriptorsInitialized = false;
    bool _bShadowDescriptorsInitialized = false;
    uint32_t _lastGBufferDescriptorWriteCount = 0;
    uint32_t _lastShadowDescriptorWriteCount = 0;

    stdptr<IDescriptorSetLayout> _environmentLightingDSL;
    std::function<DescriptorSetHandle(Scene*)> _getSceneEnvironmentLightingDescriptorSet;
    FrameInputs _frameInputs{};

    // Vertex attributes (for fullscreen quad)
    std::vector<VertexAttribute> _commonVertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
        {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
        {.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
    };

    LightStage() : IRenderStage("LightPass") {}

    /// @param gBufferStage  Provides frame+light DSL and per-flight DS
    /// @param gBufferResources Provides GBuffer textures for sampling
    void setup(GBufferStage* gBufferStage, const DeferredGBufferResources& gBufferResources);
    void setEnvironmentLightingInput(EnvironmentLightingInput input);
    void setFrameInputs(FrameInputs frameInputs);
    void setSSAOTexture(RenderImage* ssaoTexture);
    void applyShadowState(const ShadowRuntimeState& shadowState);
    void setIBLSettings(bool bEnablePBRDiffuseIBL, bool bEnablePBRSpecularIBL);
    void refreshPipelineFormats(const DeferredAttachmentFormats& formats);
    void invalidateGBufferDescriptors();
    void invalidateShadowDescriptors();
    [[nodiscard]] bool shouldRefreshShadowDescriptors() const;
    [[nodiscard]] bool isPBRDiffuseIBLEnabled() const { return _bEnablePBRDiffuseIBL; }
    [[nodiscard]] bool isPBRSpecularIBLEnabled() const { return _bEnablePBRSpecularIBL; }

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;

    void renderGUI() override;
    void renderSettingsGUI() override;
    void renderTechnicalGUI() override;
};

} // namespace ya

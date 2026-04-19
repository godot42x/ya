#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/FrameBuffer.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

#include "DeferredRender.Unified_LightPass.slang.h"

#include <array>

namespace ya
{

struct GBufferStage;
struct Sampler;
struct IImageView;

/// Deferred light pass — fullscreen quad that reads GBuffer textures and computes lighting.
///
/// Uses the frame+light DS from GBufferStage (set 0), its own GBuffer texture DS (set 1),
/// and the environment lighting DS from RenderRuntime (set 2).
struct LightStage : public IRenderStage
{
    using PushConstant = slang_types::DeferredRender::Unified_LightPass::PushConstants;
    using LightData    = slang_types::DeferredRender::Unified_LightPass::LightData;

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R8G8B8A8_UNORM;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;

    IRender*       _render       = nullptr;
    GBufferStage*  _gBufferStage = nullptr; // borrows frame+light DS
    IRenderTarget* _gBufferRT    = nullptr; // borrows GBuffer textures

    // Pipeline (shared across flights)
    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _gBufferTextureDSL;
    GraphicsPipelineCreateInfo   _pipelineCI{};
    bool                         _bEnablePBRDiffuseIBL  = true;
    bool                         _bEnablePBRSpecularIBL = true;
    bool                         _bEnableShadowMapping  = true;
    bool                         _bEnablePointLightShadow = true;

    // GBuffer texture DS + pool (updated each frame from GBuffer RT)
    stdptr<IDescriptorPool> _dsp;
    DescriptorSetHandle     _gBufferTextureDS = nullptr;
    stdptr<IDescriptorSetLayout> _shadowDSL;
    DescriptorSetHandle          _shadowDS = nullptr;

    IImageView* _shadowDirectionalDepthIV = nullptr;
    std::array<IImageView*, MAX_POINT_LIGHTS> _shadowPointCubeIVs{};
    Sampler* _shadowSampler = nullptr;
    IFrameBuffer* _lastGBufferFrameBuffer = nullptr;
    bool _bGBufferDescriptorsInitialized = false;
    bool _bShadowDescriptorsInitialized = false;
    float _lastPrepareCpuMs = 0.0f;
    float _lastExecuteCpuMs = 0.0f;
    uint32_t _lastGBufferDescriptorWriteCount = 0;
    uint32_t _lastShadowDescriptorWriteCount = 0;

    // Vertex attributes (for fullscreen quad)
    std::vector<VertexAttribute> _commonVertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
        {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
        {.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
    };

    LightStage() : IRenderStage("LightPass") {}

    /// @param gBufferStage  Provides frame+light DSL and per-flight DS
    /// @param gBufferRT     Provides GBuffer color textures for sampling
    void setup(GBufferStage* gBufferStage, IRenderTarget* gBufferRT);
    void setShadowResources(IImageView* directionalDepthIV,
                            const std::array<IImageView*, MAX_POINT_LIGHTS>& pointCubeDepthIVs,
                            Sampler* shadowSampler);
    void setShadowSettings(bool bEnableShadowMapping, bool bEnablePointLightShadow);
    void refreshPipelineFormats(const IRenderTarget* viewportRT);

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;

    void renderGUI() override;
};

} // namespace ya

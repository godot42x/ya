#pragma once

#include "Render3D/Deferred/DeferredAttachmentFormats.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameBuffer.h"
#include "RHI/Core/Pipeline.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Stage/IRenderStage.h"

#include "DeferredRender.LightPass.slang.h"

#include <array>
#include <functional>
#include <optional>

namespace ya
{

struct Scene;
struct Texture;
struct Mesh;

/// Deferred light pass — fullscreen quad that reads GBuffer textures and computes lighting.
///
/// Uses the frame+light DS from GBufferStage (set 0), its own GBuffer texture DS (set 1),
/// and the environment lighting DS from RenderRuntime (set 2).
struct YA_RENDER_3D_API LightStage : public IRenderStage
{
    struct EnvironmentLightingInput
    {
        stdptr<IDescriptorSetLayout> environmentLightingDSL = nullptr;
    };

    struct SharedInputs
    {
        stdptr<IDescriptorSetLayout> frameAndLightDSL = nullptr;
    };

    struct FrameInputs
    {
        DescriptorSetHandle frameAndLightDescriptorSet = nullptr;
        DescriptorSetHandle environmentLightingDescriptorSet = nullptr;
    };

    using PushConstant = slang_types::DeferredRender::LightPass::PushConstants;
    using LightData    = slang_types::DeferredRender::LightPass::LightData;

    static constexpr EFormat::T LINEAR_FORMAT = EFormat::R16G16B16A16_SFLOAT;
    static constexpr EFormat::T DEPTH_FORMAT  = EFormat::D32_SFLOAT;

    IRender*                  _render           = nullptr;
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;

    // Pipeline (shared across flights)
    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _gBufferTextureDSL;
    GraphicsPipelineCreateInfo   _pipelineCI{};
    bool                         _bEnablePBRDiffuseIBL  = true;
    bool                         _bEnablePBRSpecularIBL = true;
    ShadowRuntimeState           _shadowState{};

    // GBuffer / shadow layouts are device-lifetime. Per-View CIS sets live on
    // DeferredViewResources, not this recipe.
    stdptr<IDescriptorSetLayout> _shadowDSL;
    Mesh*                        _fullscreenQuad = nullptr;

    stdptr<IDescriptorSetLayout> _environmentLightingDSL;
    FrameInputs _frameInputs{};

    // Vertex attributes (for fullscreen quad)
    std::vector<VertexAttribute> _commonVertexAttributes = {
        {.bufferSlot = 0, .location = 0, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, position)},
        {.bufferSlot = 0, .location = 1, .format = EVertexAttributeFormat::Float2, .offset = offsetof(ya::Vertex, texCoord0)},
        {.bufferSlot = 0, .location = 2, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, normal)},
        {.bufferSlot = 0, .location = 3, .format = EVertexAttributeFormat::Float3, .offset = offsetof(ya::Vertex, tangent)},
    };

    LightStage() : IRenderStage("LightPass") {}

    /// @param sharedInputs  Provides frame+light descriptor layout for set 0
    void setup(SharedInputs sharedInputs);
    void setEnvironmentLightingInput(EnvironmentLightingInput input);
    void setFrameInputs(FrameInputs frameInputs);
    /// Write GBuffer CIS into a View-owned set. Does not mutate this recipe.
    void writeGBufferTextureDescriptors(
        DescriptorSetHandle                          gBufferTextureDS,
        const RGRenderContext::RGPassBindingContext& binding,
        RGTextureHandle                              albedo,
        RGTextureHandle                              normal,
        RGTextureHandle                              orm,
        RGTextureHandle                              shading,
        RGTextureHandle                              depth,
        std::optional<RGTextureHandle>               ssao) const;
    /// Write shadow CIS into a View-owned set from the current shadow recipe.
    void writeShadowDescriptors(DescriptorSetHandle shadowDS) const;
    void applyShadowState(const ShadowRuntimeState& shadowState);
    void setIBLSettings(bool bEnablePBRDiffuseIBL, bool bEnablePBRSpecularIBL);
    void refreshPipelineFormats(const DeferredAttachmentFormats& formats);
    [[nodiscard]] bool isPBRDiffuseIBLEnabled() const { return _bEnablePBRDiffuseIBL; }
    [[nodiscard]] bool isPBRSpecularIBLEnabled() const { return _bEnablePBRSpecularIBL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getGBufferTextureDSL() const { return _gBufferTextureDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getShadowDSL() const { return _shadowDSL; }
    [[nodiscard]] IGraphicsPipeline* getPipeline() const { return _pipeline.get(); }

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx,
                 DescriptorSetHandle       frameAndLight,
                 DescriptorSetHandle       environmentLighting,
                 DescriptorSetHandle       gBufferTextures,
                 DescriptorSetHandle       shadows);
    void execute(const RenderStageContext& ctx) override;

};

} // namespace ya

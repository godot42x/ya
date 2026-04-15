#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

#include "DeferredRender.Unified_LightPass.slang.h"

namespace ya
{

struct GBufferStage;

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

    IRender*      _render       = nullptr;
    GBufferStage* _gBufferStage = nullptr; // borrows frame+light DS
    IRenderTarget* _gBufferRT   = nullptr; // borrows GBuffer textures

    // Pipeline (shared across flights)
    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _gBufferTextureDSL;

    // GBuffer texture DS + pool (updated each frame from GBuffer RT)
    stdptr<IDescriptorPool> _dsp;
    DescriptorSetHandle     _gBufferTextureDS = nullptr;

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

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
};

} // namespace ya

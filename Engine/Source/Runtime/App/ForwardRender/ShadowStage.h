#pragma once

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Stage/IRenderStage.h"

#include "Shadow.CombinedShadowMappingGenerate.glsl.h"

#include <algorithm>

namespace ya
{

/// Shadow map generation stage — renders depth from each light's perspective.
///
/// Supports directional light (single 2D depth) + point lights (cubemap depth).
/// Uses per-flight ring buffer for frame UBO.
struct ShadowStage : public IRenderStage
{
    using FrameUBO          = glsl_types::Shadow::CombinedShadowMappingGenerate::FrameData;
    using ModelPushConstant = glsl_types::Shadow::CombinedShadowMappingGenerate::PushConstant;

    IRender* _render = nullptr;

    stdptr<IRenderTarget> _shadowMapRT;
    Extent2D              _shadowExtent = {.width = 1024, .height = 1024};

    bool     _bAutoBindViewportScissor    = true;
    bool     _bEnablePointLightShadow     = true;
    uint32_t _maxPointLightShadowCount    = 1;
    float    _bias                        = 0.005f;
    float    _normalBias                  = 0.01f;
    uint32_t _lastPreparedPointLightCount = 0;

    // Pipeline (shared)
    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _frameDSL;
    GraphicsPipelineCreateInfo   _pipelineCI{};

    // Per-flight
    stdptr<IDescriptorPool>                                _dsp;
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _frameDS{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>     _frameUBO{};

    ShadowStage() : IRenderStage("Shadow") {}

    void setRenderTarget(const stdptr<IRenderTarget>& rt);
    void setPointLightShadowEnabled(bool enabled) { _bEnablePointLightShadow = enabled; }
    void setMaxPointLightShadowCount(uint32_t count) { _maxPointLightShadowCount = std::min(count, static_cast<uint32_t>(MAX_POINT_LIGHTS)); }

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void renderGUI() override;
    void refreshPipelineFromRenderTarget();

    IRenderTarget* getRenderTarget() const { return _shadowMapRT.get(); }
};

} // namespace ya

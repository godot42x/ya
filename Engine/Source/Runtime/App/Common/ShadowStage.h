#pragma once

#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/Stage/IRenderStage.h"

#include "CombineShadowMappingGenerate.slang.h"

#include <algorithm>

namespace ya
{

struct ShadowStage : public IRenderStage
{
    static constexpr uint32_t SHADOW_LAYER_COUNT = 1 + MAX_POINT_LIGHTS * 6;

    using FrameUBO          = slang_types::CombineShadowMappingGenerate::FrameData;
    using ModelPushConstant = slang_types::CombineShadowMappingGenerate::PushConstants;

    struct ShadowPipelineVariant
    {
        stdptr<IGraphicsPipeline> pipeline;
        stdptr<IPipelineLayout>   pipelineLayout;
    };

    IRender* _render = nullptr;

    stdptr<IRenderTarget> _shadowMapRT;
    Extent2D              _shadowExtent = {.width = 1024, .height = 1024};

    bool     _bAutoBindViewportScissor    = true;
    bool     _bEnablePointLightShadow     = true;
    uint32_t _maxPointLightShadowCount    = 1;
    float    _bias                        = 0.005f;
    float    _normalBias                  = 0.01f;
    uint32_t _lastPreparedPointLightCount = 0;

    ShadowPipelineVariant                                     _staticVariant;
    ShadowPipelineVariant                                     _skinnedVariant;
    stdptr<IDescriptorSetLayout>                              _frameDSL;
    stdptr<IDescriptorSetLayout>                              _skinningDSL;
    GraphicsPipelineCreateInfo                                _pipelineCI{};
    stdptr<IDescriptorPool>                                                        _dsp;
    std::array<std::array<DescriptorSetHandle, SHADOW_LAYER_COUNT>, MAX_FLIGHTS_IN_FLIGHT> _frameDS{};
    std::array<std::array<stdptr<IBuffer>, SHADOW_LAYER_COUNT>, MAX_FLIGHTS_IN_FLIGHT>     _frameUBO{};
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT>                                      _skinningSSBO{};
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT>                                  _skinningDS{};
    stdptr<IImageView>                                                                       _directionalDepthView;
    stdptr<Texture>                                                                          _directionalDepthTexture;
    std::array<std::array<stdptr<IImageView>, 6>, MAX_POINT_LIGHTS>                         _pointFaceDepthViews{};
    std::array<std::array<stdptr<Texture>, 6>, MAX_POINT_LIGHTS>                            _pointFaceDepthTextures{};
    uint32_t                                                                                 _skinningCapacity = 0;

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
    void rebuildShadowLayerTextures();
    void ensureSkinningCapacity(uint32_t paletteCount);
    void recreatePipelines();

    [[nodiscard]] IRenderTarget* getRenderTarget() const { return _shadowMapRT.get(); }
};

} // namespace ya

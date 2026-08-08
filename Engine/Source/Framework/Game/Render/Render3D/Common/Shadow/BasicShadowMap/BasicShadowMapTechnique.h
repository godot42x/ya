#pragma once

#include "BasicShadowPayload.h"
#include "DirectionalShadowPass.h"
#include "PointShadowPass.h"
#include "Framework/Game/Render/Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Framework/Game/Render/Render3D/Common/Shadow/ShadowFrameResources.h"
#include "Framework/Game/Render/Render3D/Common/Shadow/ShadowTypes.h"

#include "Foundation/RHI/Core/Image.h"
#include "Framework/Game/Render/Render3D/Shadow/IShadowTechnique.h"

#include "CombineShadowMappingGenerate.slang.h"

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// BasicShadowMapTechnique
// Standard depth-only shadow mapping: one map for directional, cubemap faces
// for point lights. Supports GPU frustum culling + indirect draw.
// ═══════════════════════════════════════════════════════════════════════════

class BasicShadowMapTechnique : public IShadowTechnique
{
  public:
    using FrameUBO = slang_types::CombineShadowMappingGenerate::FrameData;

    void init(IRender* render, const ShadowSettings& settings) override;
    void destroy() override;
    void applySettings(const ShadowSettings& settings) override;
    void prepare(uint32_t flightIndex, const RenderFrameData& frameData) override;
    [[nodiscard]] DirectionalShadowPass& getDirectionalPass() { return _directionalPass; }
    [[nodiscard]] PointShadowPass&       getPointPass() { return _pointPass; }
    [[nodiscard]] const DirectionalShadowPass& getDirectionalPass() const { return _directionalPass; }
    [[nodiscard]] const PointShadowPass&       getPointPass() const { return _pointPass; }
    [[nodiscard]] const ShadowSettings&        getSettings() const { return _settings; }
    [[nodiscard]] uint32_t                     getLastPreparedPointLightCount() const { return _lastPreparedPointLightCount; }

    void refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent) override;
    [[nodiscard]] ShadowGraphOutputs appendGraphPasses(
        RenderGraph& graph,
        uint32_t flightIndex,
        const RenderFrameData& frameData);

  private:
    void                    rebuildLayerTextures(const std::shared_ptr<IImage>& shadowImage);
    BasicShadowFramePayload buildFramePayload(uint32_t flightIndex, const RenderFrameData& frameData) const;
    void                    populatePointShadowMatrices(const RenderFrameData& frameData, FrameUBO& ubo, uint32_t count) const;

    IRender* _render       = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};
    stdptr<IImage> _depthImage;
    stdptr<IImageView> _shadowDepthArrayView;

    ShadowSettings _settings;
    uint32_t       _lastPreparedPointLightCount = 0;
    ShadowFrameResources _frameResources;
    DirectionalShadowPass _directionalPass;
    PointShadowPass       _pointPass;
};

} // namespace ya

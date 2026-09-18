#pragma once

#include "Render3D/Common/Shadow/BasicShadowMap/BasicShadowPayload.h"
#include "Render3D/Common/Shadow/BasicShadowMap/DirectionalShadowPass.h"
#include "Render3D/Common/Shadow/BasicShadowMap/PointShadowPass.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"
#include "Render3D/Common/Shadow/ShadowFrameResources.h"
#include "Render3D/Common/Shadow/ShadowTypes.h"

#include "RHI/Core/Image.h"
#include "RHI/Core/ImageResource.h"
#include "Render3D/Shadow/IShadowTechnique.h"

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
    [[nodiscard]] ShadowPreparedView prepare(RenderSubmission&           submission,
                                             RenderViewRecordingContext& view) override;
    [[nodiscard]] DirectionalShadowPass& getDirectionalPass() { return _directionalPass; }
    [[nodiscard]] PointShadowPass&       getPointPass() { return _pointPass; }
    [[nodiscard]] const DirectionalShadowPass& getDirectionalPass() const { return _directionalPass; }
    [[nodiscard]] const PointShadowPass&       getPointPass() const { return _pointPass; }
    [[nodiscard]] const ShadowSettings&        getSettings() const { return _settings; }

    void refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent) override;
    /// Append this technique's graph passes for the View a prepare() produced.
    [[nodiscard]] ShadowGraphOutputs appendGraphPasses(
        RenderGraph& graph,
        uint32_t flightIndex,
        const RenderFrameData& frameData,
        const ShadowPreparedView& prepared,
        std::optional<RGPassHandle> dependency = std::nullopt);

  private:
    void                    rebuildLayerTextures(const std::shared_ptr<IImage>& shadowImage);
    BasicShadowFramePayload buildFramePayload(uint32_t flightIndex, const RenderFrameData& frameData) const;
    void                    populatePointShadowMatrices(const RenderFrameData& frameData, FrameUBO& ubo, uint32_t count) const;

    IRender* _render       = nullptr;
    Extent2D _shadowExtent = {.width = 1024, .height = 1024};
    stdptr<ImageResource> _depthResource;
    stdptr<IImageView> _shadowDepthArrayView;

    ShadowSettings _settings;
    ShadowFrameResources _frameResources;
    DirectionalShadowPass _directionalPass;
    PointShadowPass       _pointPass;
};

} // namespace ya

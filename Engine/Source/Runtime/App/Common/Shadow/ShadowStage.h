#pragma once

#include "BasicShadowMap/BasicShadowMapTechnique.h"

#include "Render/Core/IRenderTarget.h"
#include "Render/Shadow/IShadowTechnique.h"
#include "Render/Shadow/ShadowSettings.h"
#include "Render/Stage/IRenderStage.h"

#include <memory>

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// ShadowStage (Facade)
//
// Thin IRenderStage wrapper that delegates to an IShadowTechnique.
// The pipeline calls applySettings() each frame with the current
// ShadowSettings from the App layer before prepare/execute.
// ═══════════════════════════════════════════════════════════════════════════

struct ShadowStage : public IRenderStage
{
    ShadowStage() : IRenderStage("Shadow") {}

    // ─── IRenderStage interface ──────────────────────────────────────
    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void renderGUI() override;

    // ─── Public API ──────────────────────────────────────────────────
    [[nodiscard]] IRenderTarget* getRenderTarget() const { return _shadowMapRT.get(); }
    void setRenderTarget(const stdptr<IRenderTarget>& rt);
    void refreshPipelineFromRenderTarget();

    /// Apply shadow settings from App layer. Call each frame before prepare/execute.
    void applySettings(const ShadowSettings& settings);

    /// Legacy setters (kept for backward compat during migration, delegate to settings)
    void setPointLightShadowEnabled(bool enabled);
    void setMaxPointLightShadowCount(uint32_t count);

    /// Access the active technique's output textures (for LightStage sampling)
    [[nodiscard]] Texture* getDirectionalDepthTexture() const;
    [[nodiscard]] Texture* getPointFaceDepthTexture(uint32_t lightIndex, uint32_t faceIndex) const;

  private:
    IRender* _render = nullptr;

    stdptr<IRenderTarget> _shadowMapRT;
    ShadowSettings        _settings;

    std::unique_ptr<BasicShadowMapTechnique> _technique;
};

} // namespace ya

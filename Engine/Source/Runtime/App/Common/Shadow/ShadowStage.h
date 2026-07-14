#pragma once

#include "Render/Core/Image.h"
#include "Render/Core/RenderGraph.h"
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
    void renderTechnicalGUI() override;

    [[nodiscard]] IShadowTechnique* getTechnique() const { return _technique.get(); }

    // ─── Public API ──────────────────────────────────────────────────
    void refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent);
    [[nodiscard]] std::optional<RGPassHandle> appendGraphPasses(
        RenderGraph& graph,
        const RenderStageContext& ctx);

    /// Apply shadow settings from App layer. Call each frame before prepare/execute.
    void applySettings(const ShadowSettings& settings);

  private:
    IRender* _render = nullptr;
    ShadowSettings        _settings;

    std::unique_ptr<IShadowTechnique> _technique;
};

} // namespace ya

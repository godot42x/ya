#pragma once

#include "RHI/Core/Image.h"
#include "Graph/RenderGraph.h"
#include "Render3D/Shadow/IShadowTechnique.h"
#include "Render3D/Common/ShadowSettings.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Common/Shadow/ShadowGraphOutputs.h"

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

    [[nodiscard]] IShadowTechnique* getTechnique() const { return _technique.get(); }

    // ─── Public API ──────────────────────────────────────────────────
    void refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent);
    [[nodiscard]] ShadowGraphOutputs appendGraphPasses(
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

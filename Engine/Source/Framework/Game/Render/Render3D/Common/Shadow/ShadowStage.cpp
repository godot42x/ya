#include "ShadowStage.h"

#include "BasicShadowMap/BasicShadowMapTechnique.h"

#include "Foundation/Core/Profiling/Instrumentor.h"

#include "Foundation/RHI/Core/CommandBuffer.h"
#include "Foundation/RHI/Render.h"
#include "Framework/Game/Render/Render3D/RenderFrameData.h"

namespace ya
{

// ═══════════════════════════════════════════════════════════════════════════
// IRenderStage interface
// ═══════════════════════════════════════════════════════════════════════════

void ShadowStage::init(IRender* render)
{
    _render = render;
    _technique = std::make_unique<BasicShadowMapTechnique>();
    _technique->init(render, _settings);
}

void ShadowStage::destroy()
{
    if (_technique) {
        _technique->destroy();
        _technique.reset();
    }
    _render = nullptr;
}

void ShadowStage::prepare(const RenderStageContext& ctx)
{
    YA_PROFILE_FUNCTION();
    if (!ctx.frameData || !_technique || !_settings.isEnabled()) return;
    _technique->prepare(ctx.flightIndex, *ctx.frameData);
}

void ShadowStage::execute(const RenderStageContext& ctx)
{
    (void)ctx;
    // Shadow commands are recorded exclusively by the frame-graph orchestrator.
}

ShadowGraphOutputs ShadowStage::appendGraphPasses(
    RenderGraph& graph,
    const RenderStageContext& ctx)
{
    if (!ctx.frameData || !_technique || !_settings.isEnabled()) return {};
    auto* basicShadowMapTechnique = dynamic_cast<BasicShadowMapTechnique*>(_technique.get());
    if (!basicShadowMapTechnique) return {};
    return basicShadowMapTechnique->appendGraphPasses(graph, ctx.flightIndex, *ctx.frameData);
}

// ═══════════════════════════════════════════════════════════════════════════
// Settings
// ═══════════════════════════════════════════════════════════════════════════

void ShadowStage::applySettings(const ShadowSettings& settings)
{
    _settings = settings;
    if (_technique) {
        _technique->applySettings(settings);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Render target management
// ═══════════════════════════════════════════════════════════════════════════

void ShadowStage::refreshShadowResources(const std::shared_ptr<IImage>& depthImage, EFormat::T depthFormat, Extent2D shadowExtent)
{
    YA_PROFILE_FUNCTION();
    auto* basicShadowMapTechnique = dynamic_cast<BasicShadowMapTechnique*>(_technique.get());
    if (basicShadowMapTechnique) {
        basicShadowMapTechnique->refreshShadowResources(depthImage, depthFormat, shadowExtent);
    }
}

} // namespace ya

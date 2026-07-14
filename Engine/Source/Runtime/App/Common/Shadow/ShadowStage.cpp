#include "ShadowStage.h"

#include "BasicShadowMap/BasicShadowMapTechnique.h"

#include "Core/Profiling/Instrumentor.h"

#include "Render/Core/CommandBuffer.h"
#include "Render/Render.h"
#include "Render/RenderFrameData.h"

#include "imgui.h"

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
    YA_PROFILE_FUNCTION();
    if (!ctx.cmdBuf || !ctx.frameData || !_technique || !_settings.isEnabled()) return;
    _technique->execute(ctx.cmdBuf, ctx.flightIndex, *ctx.frameData);
}

void ShadowStage::renderGUI()
{
    if (!ImGui::TreeNode("Shadow Maps")) {
        return;
    }

    renderTechnicalGUI();

    ImGui::TreePop();
}

void ShadowStage::renderTechnicalGUI()
{
    if (_technique) {
        _technique->renderGUI();
    }
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
    auto* basicShadowMapTechnique = dynamic_cast<BasicShadowMapTechnique*>(_technique.get());
    if (basicShadowMapTechnique) {
        basicShadowMapTechnique->refreshShadowResources(depthImage, depthFormat, shadowExtent);
    }
}

} // namespace ya

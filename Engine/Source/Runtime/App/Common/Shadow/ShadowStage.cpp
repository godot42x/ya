#include "ShadowStage.h"

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

    // If setRenderTarget was called before init, rebuild textures now
    if (_shadowMapRT) {
        _technique->setRenderTarget(_shadowMapRT.get());
        _technique->refreshFromRenderTarget();
    }
}

void ShadowStage::destroy()
{
    if (_technique) {
        _technique->destroy();
        _technique.reset();
    }
    _shadowMapRT.reset();
    _render = nullptr;
}

void ShadowStage::prepare(const RenderStageContext& ctx)
{
    if (!ctx.frameData || !_technique || !_settings.isEnabled()) return;
    _technique->prepare(ctx.flightIndex, *ctx.frameData);
}

void ShadowStage::execute(const RenderStageContext& ctx)
{
    if (!ctx.cmdBuf || !ctx.frameData || !_technique || !_settings.isEnabled()) return;
    _technique->execute(ctx.cmdBuf, ctx.flightIndex, *ctx.frameData);
}

void ShadowStage::renderGUI()
{
    if (!ImGui::TreeNode("ShadowStage")) {
        return;
    }

    if (_technique) {
        _technique->renderGUI();
    }

    ImGui::TreePop();
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

void ShadowStage::setRenderTarget(const stdptr<IRenderTarget>& rt)
{
    _shadowMapRT = rt;
    if (_technique && _shadowMapRT) {
        _technique->setRenderTarget(_shadowMapRT.get());
    }
}

void ShadowStage::refreshPipelineFromRenderTarget()
{
    if (_technique) {
        _technique->refreshFromRenderTarget();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Output accessors
// ═══════════════════════════════════════════════════════════════════════════

Texture* ShadowStage::getDirectionalDepthTexture() const
{
    return _technique ? _technique->getDirectionalDepthTexture() : nullptr;
}

Texture* ShadowStage::getPointFaceDepthTexture(uint32_t lightIndex, uint32_t faceIndex) const
{
    return _technique ? _technique->getPointFaceDepthTexture(lightIndex, faceIndex) : nullptr;
}

} // namespace ya

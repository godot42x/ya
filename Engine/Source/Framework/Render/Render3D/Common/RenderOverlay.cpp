#include "Render3D/Common/RenderOverlay.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "RHI/RenderDefines.h"
#include "RHI/Core/CommandBuffer.h"
#include "Render2D/Render2D.h"
#include "Render/Resources/FontManager.h"

namespace ya
{

namespace
{

Render2DPassSlot viewportOverlayPassSlot()
{
    static const Render2DPassSlot sSlot = Render2D::acquirePassSlot();
    return sSlot;
}

} // namespace

void prepareRenderViewportOverlayPipeline(EFormat::T colorFormat, EFormat::T depthFormat)
{
    Render2D::preparePassPipeline(viewportOverlayPassSlot(), colorFormat, depthFormat);
}

void recordRenderViewportOverlayPass(const FrameContext& frameCtx,
                                     const std::shared_ptr<const RenderViewportOverlaySnapshot>& overlaySnapshot,
                                     ICommandBuffer* cmdBuf)
{
    YA_PROFILE_FUNCTION();

    YA_PROFILE_SCOPE("Render2D");
    YA_PERF_SCOPE(perf::sample::renderViewportOverlay(), perf::metric::cpuTimeMs(), perf::domain::render());

    const Extent2D viewportExtent{
        .width  = frameCtx.extent.width,
        .height = frameCtx.extent.height,
    };
    FRender2dContext render2dCtx{
        .cmdBuf       = cmdBuf,
        .windowWidth  = viewportExtent.width,
        .windowHeight = viewportExtent.height,
        .passSlot     = viewportOverlayPassSlot(),
        .view         = frameCtx.view,
        .viewProjection = frameCtx.projection * frameCtx.view,
    };

    Render2D::begin(render2dCtx);

    if (overlaySnapshot) {
        for (const auto& sprite : overlaySnapshot->screenSprites) {
            Render2D::makeSprite(glm::vec3(sprite.viewportPos, 0.0f), sprite.size, sprite.texture, sprite.tint);
        }

        for (const auto& sprite : overlaySnapshot->worldSprites) {
            Render2D::makeWorldSprite(sprite.worldCenter,
                                      sprite.worldDirection,
                                      sprite.worldSize,
                                      sprite.texture,
                                      sprite.tint);
        }

        for (const auto& text : overlaySnapshot->screenTexts) {
            if (text.text.empty()) {
                continue;
            }

            auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, text.fontSize);
            if (!font) {
                continue;
            }

            Render2D::makeText(text.text,
                               glm::vec3(text.viewportPos, text.depth),
                               text.color,
                               font.get());
        }
    }

    Render2D::onRender();
    Render2D::end();
}

} // namespace ya

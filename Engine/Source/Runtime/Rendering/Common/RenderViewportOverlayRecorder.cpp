#include "Runtime/Rendering/Common/RenderViewportOverlayRecorder.h"

#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Render/2D/Render2D.h"
#include "Resource/Font/FontManager.h"
#include "Runtime/Rendering/Common/UISceneRenderer.h"
#include "Scene/Node2D.h"

namespace ya
{

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
        .cam          = {
            .position       = frameCtx.cameraPos,
            .view           = frameCtx.view,
            .projection     = frameCtx.projection,
            .viewProjection = frameCtx.projection * frameCtx.view,
        },
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

void recordRenderUIPass(Node* uiSceneRoot, ICommandBuffer* cmdBuf, const Extent2D& viewportExtent)
{
    if (!uiSceneRoot || !cmdBuf || viewportExtent.width == 0 || viewportExtent.height == 0) {
        return;
    }

    FRender2dContext render2dCtx{
        .cmdBuf       = cmdBuf,
        .windowWidth  = viewportExtent.width,
        .windowHeight = viewportExtent.height,
        .cam          = {
            .position       = glm::vec3(0.0f),
            .view           = glm::mat4(1.0f),
            .projection     = glm::mat4(1.0f),
            .viewProjection = glm::mat4(1.0f),
        },
    };

    Render2D::begin(render2dCtx);
    UISceneRenderer::render(uiSceneRoot);
    Render2D::end();
}

} // namespace ya

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

void recordUICompositorPass(ICommandBuffer* cmdBuf, RenderImage& target, const Extent2D& logicalViewportExtent, Node* uiSceneRoot)
{
    if (!uiSceneRoot || !cmdBuf) {
        return;
    }
    const Extent2D rtExtent = target.getExtent();
    if (rtExtent.width == 0 || rtExtent.height == 0) {
        return;
    }

    // UI is authored in logical viewport pixels; map to render-target pixels
    // (viewport frame buffer scale).
    const glm::vec2 uiScale{
        static_cast<float>(rtExtent.width) / static_cast<float>(std::max(logicalViewportExtent.width, 1u)),
        static_cast<float>(rtExtent.height) / static_cast<float>(std::max(logicalViewportExtent.height, 1u)),
    };

    cmdBuf->retainResource(target.getImageShared());
    cmdBuf->retainResource(target.getImageViewShared());
    cmdBuf->transitionImageLayoutAuto(target.getImage(), EImageLayout::ColorAttachmentOptimal);

    cmdBuf->beginRendering(RenderingInfo{
        .label                         = "UI Compositor",
        .bExternalTransitionManagement = true,
        .attachments                   = RenderAttachmentSet{
            .renderArea = Rect2D{
                .pos    = {0.0f, 0.0f},
                .extent = {static_cast<float>(rtExtent.width), static_cast<float>(rtExtent.height)},
            },
            .layerCount = 1,
            .colors     = {
                RenderAttachment{
                    .image         = target.getImage(),
                    .imageView     = target.getImageView(),
                    .loadOp        = EAttachmentLoadOp::Load,
                    .storeOp       = EAttachmentStoreOp::Store,
                    .clearValue    = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ColorAttachmentOptimal,
                },
            },
            .depth = std::nullopt,
        },
    });

    FRender2dContext render2dCtx{
        .cmdBuf       = cmdBuf,
        .windowWidth  = rtExtent.width,
        .windowHeight = rtExtent.height,
        .bUICompositorMode = true,
        .cam          = {
            .position       = glm::vec3(0.0f),
            .view           = glm::mat4(1.0f),
            .projection     = glm::mat4(1.0f),
            .viewProjection = glm::mat4(1.0f),
        },
    };

    Render2D::begin(render2dCtx);
    UISceneRenderer::render(uiSceneRoot, uiScale);
    Render2D::end();

    cmdBuf->endRendering();
    cmdBuf->transitionImageLayoutAuto(target.getImage(), EImageLayout::ShaderReadOnlyOptimal);
}

} // namespace ya

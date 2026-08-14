#include "GUI/Compose/Render2DComposePass.h"

#include "GUI/Draw2D/Render2D.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Backend/TextureLibrary.h"

#include <array>

namespace ya
{

namespace
{

void logSnapshotItemsOnce(const UIFrameSnapshot* uiFrameSnapshot)
{
    static int sLoggedFrames = 0;
    if (sLoggedFrames >= 3 || !uiFrameSnapshot) {
        return;
    }

    ++sLoggedFrames;
    YA_CORE_INFO("Render2DCompose snapshot item count: {}", uiFrameSnapshot->items.size());
    const size_t itemCount = std::min<size_t>(uiFrameSnapshot->items.size(), 200);
    for (size_t i = 0; i < itemCount; ++i) {
        const auto& item = uiFrameSnapshot->items[i];
        YA_CORE_INFO("  [{}] kind={} pos=({}, {}) size=({}, {}) clipped={} clip=({}, {}, {}, {}) text='{}'",
                     i,
                     item.kind == UIFrameDrawItem::EKind::Sprite ? "Sprite" : "Text",
                     item.pos.x,
                     item.pos.y,
                     item.size.x,
                     item.size.y,
                     item.bClipped,
                     item.clip.pos.x,
                     item.clip.pos.y,
                     item.clip.extent.x,
                     item.clip.extent.y,
                     item.kind == UIFrameDrawItem::EKind::Text ? item.text : "");
    }
}

/// One Render2D pass slot per compose kind. Each kind gets its own slot so
/// that multiple compose passes recorded into the same command buffer (e.g.
/// editor viewport + canvas preview + tool surface) never share the per-slot
/// vertex buffers / descriptor sets within one frame.
Render2DPassSlot composePassSlot(ERender2DComposePassKind kind)
{
    static const std::array<Render2DPassSlot, 5> sSlots = []() {
        std::array<Render2DPassSlot, 5> out{};
        for (auto& slot : out) {
            slot = Render2D::acquirePassSlot();
        }
        return out;
    }();
    return sSlots[static_cast<size_t>(kind)];
}

bool shouldClearComposeTarget(ERender2DComposePassKind kind)
{
    return kind != ERender2DComposePassKind::RuntimeUIComposite;
}

ClearValue composeClearValue(ERender2DComposePassKind kind)
{
    if (kind == ERender2DComposePassKind::EditorCanvasPreview) {
        return ClearValue(0.055f, 0.06f, 0.07f, 1.0f);
    }
    if (kind == ERender2DComposePassKind::RuntimeUIOffscreen) {
        return ClearValue(0.05f, 0.06f, 0.07f, 1.0f);
    }
    if (kind == ERender2DComposePassKind::EditorToolSurface) {
        return ClearValue(0.075f, 0.082f, 0.10f, 1.0f);
    }
    return ClearValue(0.0f, 0.0f, 0.0f, 0.0f);
}

const char* composePassLabel(ERender2DComposePassKind kind)
{
    switch (kind) {
        case ERender2DComposePassKind::RuntimeUIComposite: return "UI Compositor";
        case ERender2DComposePassKind::RuntimeUIOffscreen: return "UI Offscreen Mirror";
        case ERender2DComposePassKind::EditorCanvasPreview: return "Editor Canvas Preview";
        case ERender2DComposePassKind::EditorViewportCompose: return "EditorViewportComposition";
        case ERender2DComposePassKind::EditorToolSurface: return "EditorToolSurface";
    }
    return "Render2D Compose";
}

void drawEditorCanvasGrid(const Extent2D& rtExtent, const glm::vec2& uiScale, const glm::vec2& canvasPan, float canvasZoom)
{
    // Canvas grid is authored in logical pixels and transformed by the
    // same pan/zoom as the UI nodes. This keeps right-drag panning and
    // wheel zoom coherent instead of stretching a precomposed texture.
    constexpr float kGridStepLogical = 32.0f;
    const glm::vec4 gridColor(0.16f, 0.17f, 0.19f, 1.0f);
    const float     safeZoom = std::max(canvasZoom, 0.01f);
    const float     gridStepX = kGridStepLogical * safeZoom * uiScale.x;
    const float     gridStepY = kGridStepLogical * safeZoom * uiScale.y;
    const float     panPxX = canvasPan.x * uiScale.x;
    const float     panPxY = canvasPan.y * uiScale.y;
    auto*           white      = TextureLibrary::get().getWhiteTexture().get();
    if (!white) {
        return;
    }

    const int32_t firstX = static_cast<int32_t>(std::floor(-panPxX / std::max(gridStepX, 1.0f))) - 1;
    const int32_t firstY = static_cast<int32_t>(std::floor(-panPxY / std::max(gridStepY, 1.0f))) - 1;
    for (int32_t i = firstX; i * gridStepX + panPxX < static_cast<float>(rtExtent.width); ++i) {
        const float x = i * gridStepX + panPxX;
        if (x < 0.0f) {
            continue;
        }
        Render2D::makeSprite(glm::vec3(x, 0.0f, 0.0f),
                             glm::vec2(1.0f, static_cast<float>(rtExtent.height)),
                             white,
                             gridColor);
    }
    for (int32_t i = firstY; i * gridStepY + panPxY < static_cast<float>(rtExtent.height); ++i) {
        const float y = i * gridStepY + panPxY;
        if (y < 0.0f) {
            continue;
        }
        Render2D::makeSprite(glm::vec3(0.0f, y, 0.0f),
                             glm::vec2(static_cast<float>(rtExtent.width), 1.0f),
                             white,
                             gridColor);
    }
}

} // namespace

void prepareRender2DComposePassPipeline(const FRender2DComposePassDesc& passDesc,
                                        EFormat::T                      colorFormat,
                                        EFormat::T                      depthFormat)
{
    Render2D::preparePassPipeline(composePassSlot(passDesc.kind), colorFormat, depthFormat);
}

void recordRender2DComposePass(ICommandBuffer*                 cmdBuf,
                               RenderImage&                    target,
                               RenderImage*                    depthTarget,
                               const UIFrameSnapshot*          uiFrameSnapshot,
                               const FRender2DComposePassDesc& passDesc,
                               const std::function<void()>&    extraContent)
{
    if (!cmdBuf) {
        return;
    }
    const Extent2D rtExtent = target.getExtent();
    if (rtExtent.width == 0 || rtExtent.height == 0) {
        return;
    }

    // UI is authored in logical viewport pixels; map to render-target pixels
    // (viewport frame buffer scale).
    const glm::vec2 uiScale{
        static_cast<float>(rtExtent.width) / static_cast<float>(std::max(passDesc.logicalViewportExtent.width, 1u)),
        static_cast<float>(rtExtent.height) / static_cast<float>(std::max(passDesc.logicalViewportExtent.height, 1u)),
    };

    cmdBuf->retainResource(target.getImageShared());
    cmdBuf->retainResource(target.getImageViewShared());
    cmdBuf->transitionImageLayoutAuto(target.getImage(), EImageLayout::ColorAttachmentOptimal);

    if (depthTarget) {
        cmdBuf->retainResource(depthTarget->getImageShared());
        cmdBuf->retainResource(depthTarget->getImageViewShared());
        cmdBuf->retainResources(depthTarget->getRetainedResources());
        cmdBuf->transitionImageLayoutAuto(depthTarget->getImage(), EImageLayout::DepthStencilAttachmentOptimal);
    }

    cmdBuf->beginRendering(RenderingInfo{
        .label                         = composePassLabel(passDesc.kind),
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
                    .loadOp        = shouldClearComposeTarget(passDesc.kind) ? EAttachmentLoadOp::Clear : EAttachmentLoadOp::Load,
                    .storeOp       = EAttachmentStoreOp::Store,
                    .clearValue    = composeClearValue(passDesc.kind),
                    .initialLayout = EImageLayout::ColorAttachmentOptimal,
                    .finalLayout   = EImageLayout::ColorAttachmentOptimal,
                },
            },
            .depth = depthTarget
                         ? std::optional<RenderAttachment>{RenderAttachment{
                               .image         = depthTarget->getImage(),
                               .imageView     = depthTarget->getImageView(),
                               .loadOp        = EAttachmentLoadOp::Load,
                               .storeOp       = EAttachmentStoreOp::Store,
                               .initialLayout = EImageLayout::DepthStencilAttachmentOptimal,
                               .finalLayout   = EImageLayout::DepthStencilAttachmentOptimal,
                           }}
                         : std::nullopt,
        },
    });

    FRender2dContext render2dCtx{
        .cmdBuf       = cmdBuf,
        .windowWidth  = rtExtent.width,
        .windowHeight = rtExtent.height,
        .passSlot     = composePassSlot(passDesc.kind),
        .view         = passDesc.camera.view,
        .viewProjection = passDesc.camera.viewProjection,
    };

    Render2D::begin(render2dCtx);
    if (passDesc.kind == ERender2DComposePassKind::EditorViewportCompose) {
        if (passDesc.sceneSourceTexture) {
            Render2D::makeSprite(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec2(static_cast<float>(rtExtent.width), static_cast<float>(rtExtent.height)),
                                 passDesc.sceneSourceTexture.get(),
                                 glm::vec4(1.0f));
        }
    }
    if (passDesc.kind == ERender2DComposePassKind::EditorCanvasPreview) {
        drawEditorCanvasGrid(rtExtent, uiScale, passDesc.canvasPan, passDesc.canvasZoom);
    }
    if (uiFrameSnapshot) {
        logSnapshotItemsOnce(uiFrameSnapshot);
        for (const auto& item : uiFrameSnapshot->items) {
            if (item.bClipped) {
                Render2D::pushClipRect(item.clip);
            }
            if (item.kind == UIFrameDrawItem::EKind::Sprite) {
                Render2D::makeSprite(glm::vec3(item.pos, 0.0f),
                                     item.size,
                                     item.texture,
                                     item.color);
            }
            else {
                Render2D::makeText(item.text,
                                   glm::vec3(item.pos, 0.0f),
                                   item.color,
                                   item.font.get(),
                                   item.textScale);
            }
            if (item.bClipped) {
                Render2D::popClipRect();
            }
        }
    }
    if (extraContent) {
        extraContent();
    }
    Render2D::end();

    cmdBuf->endRendering();
    cmdBuf->transitionImageLayoutAuto(target.getImage(), passDesc.finalLayout);
    if (depthTarget) {
        cmdBuf->transitionImageLayoutAuto(depthTarget->getImage(), EImageLayout::ShaderReadOnlyOptimal);
    }
}

} // namespace ya

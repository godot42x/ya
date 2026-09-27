#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Compose/UIFrameComposeReplay.h"

#include "Render2D/ScreenDraw.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Backend/TextureLibrary.h"

#include <array>

namespace ya
{

namespace
{

void logSnapshotItemsOnce(const UIFrameSnapshot* uiFrameSnapshot)
{
    static int sLoggedFrames = 0;
    if (!screenDrawDiagnostics().bLogSessionLifecycle ||
        sLoggedFrames >= 3 || !uiFrameSnapshot) {
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
        case ERender2DComposePassKind::EditorToolSurface: return "EditorToolSurface";
    }
    return "Render2D Compose";
}

void emitSnapshotItem(ScreenDrawList& list, const UIFrameDrawItem& item)
{
    if (item.kind == UIFrameDrawItem::EKind::Sprite) {
        if (item.bPerVertexColor) {
            list.makeRectFilledMultiColor(glm::vec3(item.pos, 0.0f),
                                          item.size,
                                          item.vertexColors,
                                          item.texture);
        }
        else if (item.cornerRadius > 0.0f && !item.texture) {
            list.drawRoundedRect(glm::vec3(item.pos, 0.0f),
                                 item.size,
                                 item.color,
                                 item.cornerRadius);
        }
        else {
            list.makeSprite(glm::vec3(item.pos, 0.0f),
                            item.size,
                            item.texture,
                            item.color,
                            item.uvScale,
                            item.uvOffset,
                            item.bOpaqueSample);
        }
    }
    else if (item.kind == UIFrameDrawItem::EKind::Line) {
        list.strokeLine(item.lineFrom, item.lineTo, item.color, item.lineThickness);
    }
    else {
        list.makeText(item.text,
                      glm::vec3(item.pos, 0.0f),
                      item.color,
                      item.font.get(),
                      item.textScale);
    }
}

void replaySnapshotItems(ScreenDrawList& list, const UIFrameSnapshot& snapshot)
{
    walkComposeClipRuns(snapshot,
                        [&list](const UIFrameDrawItem& item) { emitSnapshotItem(list, item); },
                        nullptr,
                        FComposeClipRunSink{
                            .pushClip = [&list](const Rect2D& rect) { list.pushClipRect(rect); },
                            .popClip  = [&list]() { list.popClipRect(); },
                        });
}

void drawEditorCanvasGrid(ScreenDrawList& list, const Extent2D& rtExtent, const glm::vec2& uiScale, const glm::vec2& canvasPan, float canvasZoom)
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
        list.makeSprite(glm::vec3(x, 0.0f, 0.0f),
                        glm::vec2(1.0f, static_cast<float>(rtExtent.height)),
                        white,
                        gridColor);
    }
    for (int32_t i = firstY; i * gridStepY + panPxY < static_cast<float>(rtExtent.height); ++i) {
        const float y = i * gridStepY + panPxY;
        if (y < 0.0f) {
            continue;
        }
        list.makeSprite(glm::vec3(0.0f, y, 0.0f),
                        glm::vec2(static_cast<float>(rtExtent.width), 1.0f),
                        white,
                        gridColor);
    }
}

} // namespace

void prepareRender2DComposePassPipeline(ScreenDrawRecorder& recorder,
                                        EFormat::T          colorFormat,
                                        EFormat::T          depthFormat)
{
    recorder.prepare(colorFormat, depthFormat);
}

void recordRender2DComposePass(ICommandBuffer*                 cmdBuf,
                               RenderTexture&                  target,
                               const UIFrameSnapshot*          uiFrameSnapshot,
                               const FRender2DComposePassDesc& passDesc,
                               ScreenDrawRecorder&             recorder,
                               const std::function<void(ScreenDrawList&)>& extraContent)
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
        static_cast<float>(rtExtent.width) / static_cast<float>(std::max(passDesc.logicalExtent.width, 1u)),
        static_cast<float>(rtExtent.height) / static_cast<float>(std::max(passDesc.logicalExtent.height, 1u)),
    };

    cmdBuf->retireResource(target.getImageShared());
    cmdBuf->retireResource(target.getImageViewShared());
    cmdBuf->transitionImageLayoutAuto(target.getImage(), EImageLayout::ColorAttachmentOptimal);

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
        },
    });

    ScreenDrawList list;
    if (passDesc.kind == ERender2DComposePassKind::EditorCanvasPreview) {
        drawEditorCanvasGrid(list, rtExtent, uiScale, passDesc.canvasPan, passDesc.canvasZoom);
    }
    if (uiFrameSnapshot) {
        logSnapshotItemsOnce(uiFrameSnapshot);
        replaySnapshotItems(list, *uiFrameSnapshot);
    }
    if (extraContent) {
        extraContent(list);
    }
    recorder.record(list, ScreenDrawTarget{
        .cmd         = cmdBuf,
        .width       = rtExtent.width,
        .height      = rtExtent.height,
        .colorFormat = target.getFormat(),
    });

    cmdBuf->endRendering();
    cmdBuf->transitionImageLayoutAuto(target.getImage(), passDesc.finalLayout);
}

void replayUIFrameSnapshot(ICommandBuffer*          cmdBuf,
                           const UIFrameSnapshot&   snapshot,
                           Extent2D                 targetExtent,
                           EFormat::T               colorFormat,
                           ScreenDrawRecorder&      recorder,
                           const std::function<void(ScreenDrawList&)>& extraContent)
{
    if (!cmdBuf || targetExtent.width == 0 || targetExtent.height == 0) {
        return;
    }

    for (const auto& item : snapshot.items) {
        if (!item.texture) {
            continue;
        }
        if (auto image = item.texture->getImageShared()) {
            cmdBuf->retireResource(std::move(image));
        }
        if (auto view = item.texture->getImageViewShared()) {
            cmdBuf->retireResource(std::move(view));
        }
    }

    ScreenDrawList list;
    logSnapshotItemsOnce(&snapshot);
    replaySnapshotItems(list, snapshot);
    if (extraContent) {
        extraContent(list);
    }
    recorder.record(list, ScreenDrawTarget{
        .cmd         = cmdBuf,
        .width       = targetExtent.width,
        .height      = targetExtent.height,
        .colorFormat = colorFormat,
    });
}

} // namespace ya

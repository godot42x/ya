#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Compose/UIFrameComposeReplay.h"

#include "Render2D/Render2D.h"
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
    if (!Render2D::debugState().bLogSessionLifecycle ||
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

/// One Render2D pass slot per compose kind for the process-wide kind pool.
/// Multi-window hosts pass `FRender2DComposePassDesc::passSlot` instead so
/// two windows composing the same kind in one CPU frame do not share UBO.
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

Render2DPassSlot resolveComposePassSlot(const FRender2DComposePassDesc& desc)
{
    if (desc.passSlot != kInvalidRender2DPassSlot) {
        return desc.passSlot;
    }
    return composePassSlot(desc.kind);
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
    if (kind == ERender2DComposePassKind::EditorViewportCompose) {
        return ClearValue(0.07f, 0.075f, 0.09f, 1.0f);
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

void emitSnapshotItem(Render2DList& list, const UIFrameDrawItem& item)
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
        const glm::vec2 delta = item.lineTo - item.lineFrom;
        const float     len   = glm::length(delta);
        if (len <= 1e-4f) {
            const glm::vec2 t = glm::vec2(item.lineThickness);
            list.makeSprite(glm::vec3(item.lineFrom - t * 0.5f, 0.0f),
                            t, nullptr, item.color);
        }
        else {
            const glm::vec2 dir = delta / len;
            const glm::vec2 nrm = glm::vec2(-dir.y, dir.x);
            const glm::mat4 transform(
                glm::vec4(dir.x * len, dir.y * len, 0.0f, 0.0f),
                glm::vec4(nrm.x * item.lineThickness, nrm.y * item.lineThickness, 0.0f, 0.0f),
                glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
                glm::vec4(item.lineFrom.x, item.lineFrom.y, 0.0f, 1.0f));
            list.makeSprite(transform, nullptr, item.color);
        }
    }
    else {
        list.makeText(item.text,
                      glm::vec3(item.pos, 0.0f),
                      item.color,
                      item.font.get(),
                      item.textScale);
    }
}

void replaySnapshotItems(Render2DList& list, const UIFrameSnapshot& snapshot)
{
    walkComposeClipRuns(snapshot,
                        [&list](const UIFrameDrawItem& item) { emitSnapshotItem(list, item); },
                        nullptr,
                        FComposeClipRunSink{
                            .pushClip = [&list](const Rect2D& rect) { list.pushClipRect(rect); },
                            .popClip  = [&list]() { list.popClipRect(); },
                        });
}

void drawEditorCanvasGrid(Render2DList& list, const Extent2D& rtExtent, const glm::vec2& uiScale, const glm::vec2& canvasPan, float canvasZoom)
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

void prepareRender2DComposePassPipeline(const FRender2DComposePassDesc& passDesc,
                                        EFormat::T                      colorFormat,
                                        EFormat::T                      depthFormat)
{
    Render2D::preparePassPipeline(resolveComposePassSlot(passDesc), colorFormat, depthFormat);
}

void recordRender2DComposePass(ICommandBuffer*                 cmdBuf,
                               RenderTexture&                  target,
                               RenderTexture*                  depthTarget,
                               const UIFrameSnapshot*          uiFrameSnapshot,
                               const FRender2DComposePassDesc& passDesc,
                               const std::function<void(Render2DList&)>& extraContent)
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

    if (depthTarget) {
        cmdBuf->retireResource(depthTarget->getImageShared());
        cmdBuf->retireResource(depthTarget->getImageViewShared());
        cmdBuf->retireResources(depthTarget->getRetainedResources());
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
        .passSlot     = resolveComposePassSlot(passDesc),
        .view         = passDesc.camera.view,
        .viewProjection = passDesc.camera.viewProjection,
    };

    // The 2D content is built as a pure CPU value first, then turned into GPU
    // work in one record step -- the same build/record split the 3D graph uses.
    Render2DList list;
    if (passDesc.kind == ERender2DComposePassKind::EditorViewportCompose) {
        if (passDesc.sceneSourceTexture) {
            list.makeSprite(glm::vec3(0.0f, 0.0f, 0.0f),
                            glm::vec2(static_cast<float>(rtExtent.width), static_cast<float>(rtExtent.height)),
                            passDesc.sceneSourceTexture.get(),
                            glm::vec4(1.0f),
                            {1.0f, 1.0f},
                            {0.0f, 0.0f},
                            true);
        }
    }
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
    Render2D::recordRender2DList(list, render2dCtx);

    cmdBuf->endRendering();
    cmdBuf->transitionImageLayoutAuto(target.getImage(), passDesc.finalLayout);
    if (depthTarget) {
        cmdBuf->transitionImageLayoutAuto(depthTarget->getImage(), EImageLayout::ShaderReadOnlyOptimal);
    }
}

void replayUIFrameSnapshot(ICommandBuffer*          cmdBuf,
                           const UIFrameSnapshot&   snapshot,
                           Extent2D                 targetExtent,
                           ERender2DComposePassKind kind,
                           const std::function<void(Render2DList&)>& extraContent)
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

    FRender2dContext render2dCtx{
        .cmdBuf         = cmdBuf,
        .windowWidth    = targetExtent.width,
        .windowHeight   = targetExtent.height,
        .passSlot       = composePassSlot(kind),
        .view           = glm::mat4(1.0f),
        .viewProjection = glm::mat4(1.0f),
    };

    Render2DList list;
    logSnapshotItemsOnce(&snapshot);
    replaySnapshotItems(list, snapshot);
    if (extraContent) {
        extraContent(list);
    }
    Render2D::recordRender2DList(list, render2dCtx);
}

} // namespace ya

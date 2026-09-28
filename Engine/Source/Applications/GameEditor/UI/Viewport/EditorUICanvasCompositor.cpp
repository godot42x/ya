#include "GameEditor/UI/Viewport/EditorUICanvasCompositor.h"

#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/UI/Viewport/EditorViewportOverlayRecord.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Render.h"

#include <algorithm>

namespace ya
{

void EditorUICanvasCompositor::bindDraw(ScreenDrawPipelines& screen)
{
    if (_bBound) {
        return;
    }
    _screen.init(screen);
    _bBound = true;
}

void EditorUICanvasCompositor::prepare()
{
    if (_bBound) {
        _screen.prepare(kEditorCanvasPreviewColorFormat, EFormat::Undefined);
    }
}

void EditorUICanvasCompositor::shutdown()
{
    _screen.destroy();
    _bBound = false;
    _target.reset();
}

void EditorUICanvasCompositor::compose(IRender& render, ICommandBuffer& commandBuffer, EditorUIDesignerSession& designer)
{
    EditorUICanvasView& view = designer.canvas();
    view.image.reset();
    if (!_bBound || !view.isShown()) {
        return;
    }

    const Extent2D logicalExtent = Extent2D::fromVec2(glm::max(view.extent, glm::vec2(0.0f)));
    if (logicalExtent.width == 0 || logicalExtent.height == 0) {
        return;
    }
    ensureTarget(render, logicalExtent);
    if (!_target || !_target->isValid()) {
        return;
    }

    const glm::vec2 targetScale{
        static_cast<float>(_target->getExtent().width) / std::max(static_cast<float>(logicalExtent.width), 1.0f),
        static_cast<float>(_target->getExtent().height) / std::max(static_cast<float>(logicalExtent.height), 1.0f),
    };
    // Preview tree logical px -> canvas target px: framebuffer scale, then the
    // canvas pan/zoom. EditorUICanvasView::viewToCanvas is the inverse.
    const glm::vec2 uiScale = targetScale * view.zoom;
    const glm::vec2 offset  = view.pan * targetScale;

    UIFrameSnapshot        previewSnapshot;
    const UIFrameSnapshot* pPreviewSnapshot = nullptr;
    const Rect2D*          pSelectionRect   = nullptr;
    if (designer.hasDocument()) {
        previewSnapshot  = designer.buildPreviewSnapshot(uiScale, offset);
        pPreviewSnapshot = &previewSnapshot;
        pSelectionRect   = designer.getSelectedLayoutRect();
    }

    recordRender2DComposePass(&commandBuffer,
                              *_target,
                              pPreviewSnapshot,
                              FRender2DComposePassDesc{
                                  .kind          = ERender2DComposePassKind::EditorCanvasPreview,
                                  .logicalExtent = logicalExtent,
                                  .canvasPan     = view.pan,
                                  .canvasZoom    = view.zoom,
                              },
                              _screen,
                              [&pSelectionRect, &uiScale, &offset](ScreenDrawList& composeList) {
                                  if (pSelectionRect) {
                                      recordEditorCanvasSelectionOverlay(composeList, *pSelectionRect, uiScale, offset);
                                  }
                              });
    view.image = _target;
}

void EditorUICanvasCompositor::ensureTarget(IRender& render, const Extent2D& extent)
{
    if (_target && _target->getWidth() == extent.width && _target->getHeight() == extent.height) {
        return;
    }
    _target = RenderTexture::create(*render.getResourceFactory(),
                                    RenderTextureCreateInfo{
                                        .label   = "EditorUICanvas",
                                        .width   = extent.width,
                                        .height  = extent.height,
                                        .format  = kEditorCanvasPreviewColorFormat,
                                        .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                                        .samples = ESampleCount::Sample_1,
                                    });
}

} // namespace ya

#include "GameEditor/UI/Tabs/EditorViewportTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/Texture.h"

#include <algorithm>

namespace ya
{

namespace
{

/// The camera preview is viewport chrome: a filled panel with a hairline ring,
/// stacked on the world image. Composing it as chrome (instead of having the
/// runtime blit the View onto the world render target) is what keeps the world
/// overlays - x-z grid, manipulator, frustum wireframe - under it: they are
/// world-space content of the image below, not siblings of the panel.
constexpr float kPreviewFrameInset = 1.0f;

constexpr glm::vec4 kPreviewFrameFill        = {0.02f, 0.03f, 0.04f, 0.92f};
constexpr glm::vec4 kPreviewFrameBorderColor = {0.42f, 0.46f, 0.54f, 1.0f};

} // namespace

EditorViewportTab::EditorViewportTab(IEditorViewportHostSink* sink)
    : UICompoundWidget("ViewportBody", "panel.canvas")
    , _sink(sink)
{
}

EditorViewportTab::~EditorViewportTab()
{
    clearHostRegistration();
}

void EditorViewportTab::construct()
{
    // Stacked composition: the world image fills the panel, the preview panel is
    // a later sibling, so paint order and hit order both put chrome above world.
    auto stack = ui::overlay("ViewportStack");
    _stack     = stack.share();

    auto image = ui::image("ViewportImage");
    _image     = image.share();
    _image->_hitFilter   = EWidgetHitFilter::Stop;
    _image->_focusPolicy = EWidgetFocusPolicy::Focusable;
    _image->setOpaqueSample(true);
    stack.child(std::move(image), FOverlaySlotArgs{});

    auto previewFrame = ui::border("CameraPreviewPanel");
    _previewFrame     = previewFrame.share();
    _previewFrame->setStyleField(
        "fillColor",
        FBrush::solid(kPreviewFrameFill, /*cornerRadius=*/0.0f, kPreviewFrameBorderColor));
    _previewFrame->setPadding(FMargin::all(kPreviewFrameInset));
    _previewFrame->setVisibility(EWidgetVisibility::Collapsed);

    auto previewImage = ui::image("CameraPreviewImage");
    _previewImage     = previewImage.share();
    // Hit-testable chrome: a press inside the preview belongs to the panel and
    // does not fall through to the world interaction underneath.
    _previewImage->_hitFilter = EWidgetHitFilter::Stop;
    previewFrame.child(std::move(previewImage), FContentSlotArgs{});

    // The panel's rect is the preview View's own rect, pushed every frame; only
    // its corner is fixed.
    stack.child(std::move(previewFrame),
                FOverlaySlotArgs{
                    .hAlign = EUIOverlayAlignment::End,
                    .vAlign = EUIOverlayAlignment::End,
                });

    addDetachedChild(stack.release());
}

void EditorViewportTab::onAttached()
{
    if (!_sink) {
        return;
    }
    _sink->setViewportHost(this);
    _bHostRegistered = true;
}

void EditorViewportTab::onDetached()
{
    clearHostRegistration();
}

void EditorViewportTab::clearHostRegistration()
{
    if (!_sink || !_bHostRegistered) {
        return;
    }
    _sink->setViewportHost(nullptr);
    _bHostRegistered = false;
}

void EditorViewportTab::setDisplayImage(const std::shared_ptr<Texture>& texture, bool missing)
{
    _image->setTexture(texture);
    _image->setResourceMissing(missing);
    // Live RT contents change every frame even when the wrap pointer does not.
    _image->markPaintDirty();
}

void EditorViewportTab::setPreviewImage(const std::shared_ptr<Texture>& texture, const Rect2D& localRect)
{
    if (!texture || localRect.extent.x <= 0.0f || localRect.extent.y <= 0.0f) {
        _previewFrame->setVisibility(EWidgetVisibility::Collapsed);
        _previewImage->setTexture(nullptr);
        _previewLocalRect = {};
        return;
    }

    _previewFrame->setVisibility(EWidgetVisibility::Visible);
    if (localRect.pos != _previewLocalRect.pos || localRect.extent != _previewLocalRect.extent) {
        _previewLocalRect = localRect;
        placePreviewPanel(localRect);
    }
    _previewImage->setTexture(texture);
    _previewImage->setResourceMissing(false);
    // Live RT contents change every frame even when the wrap pointer does not.
    _previewImage->markPaintDirty();
    _previewFrame->markPaintDirty();
}

void EditorViewportTab::placePreviewPanel(const Rect2D& localRect)
{
    if (!_stack || !_previewFrame) {
        return;
    }
    UIOverlaySlot* slot = _stack->getOverlaySlot(*_previewFrame);
    if (!slot) {
        return;
    }

    // End/End with the panel rect expressed as padding from the far edges: the
    // slot owns placement (and its own layout invalidation), so nothing here
    // writes a rect by hand.
    const glm::vec2 hostExtent = _image->_layoutRect.extent;
    const Rect2D    panel{
        .pos    = localRect.pos - glm::vec2(kPreviewFrameInset),
        .extent = localRect.extent + glm::vec2(kPreviewFrameInset * 2.0f),
    };
    slot->setHAlign(EUIOverlayAlignment::End);
    slot->setVAlign(EUIOverlayAlignment::End);
    slot->setPreferredSize(panel.extent);
    slot->setPadding(FMargin{0.0f,
                             0.0f,
                             std::max(0.0f, hostExtent.x - panel.pos.x - panel.extent.x),
                             std::max(0.0f, hostExtent.y - panel.pos.y - panel.extent.y)});
}

bool EditorViewportTab::isWorldPoint(const glm::vec2& logicalPoint) const
{
    const WidgetTree* tree = getTree();
    if (!tree || !_image) {
        return false;
    }
    // The point is world only if the router's hit test resolves it to the world
    // image: chrome stacked above the image owns its own area.
    return tree->pickAt(logicalPoint) == _image.get();
}

Rect2D EditorViewportTab::imageRect() const
{
    return _image->_layoutRect;
}

bool EditorViewportTab::isHovered() const
{
    const WidgetTree* tree = getTree();
    return tree && containsTreeNode(tree->getHovered());
}

bool EditorViewportTab::isFocused() const
{
    const WidgetTree* tree = getTree();
    return tree && containsTreeNode(tree->getFocused());
}

void EditorViewportTab::takeKeyboardFocus()
{
    WidgetTree* tree = getTree();
    if (!tree || !_image) {
        return;
    }
    tree->setFocus(_image.get());
}

bool EditorViewportTab::containsTreeNode(const UIElement* node) const
{
    for (const UIElement* cursor = node; cursor; cursor = cursor->getParent()) {
        if (cursor == this) {
            return true;
        }
    }
    return false;
}

} // namespace ya

#include "GameEditor/UI/EditorViewportTab.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/Texture.h"

namespace ya
{

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
    auto image = ui::image("ViewportImage");
    _image     = image.share();
    _image->_hitFilter   = EWidgetHitFilter::Stop;
    _image->_focusPolicy = EWidgetFocusPolicy::Focusable;
    _image->setOpaqueSample(true);
    addDetachedChild(image.release());
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

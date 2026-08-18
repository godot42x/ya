#include "GUI/Widgets/Controls/DragDrop.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

// === UIDragSource ===

void UIDragSource::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _bPressed ? _draggingColor : _backgroundColor, nullptr);
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font) {
        builder.addText(_layoutRect, _label, _textColor, font,
                        EWidgetAlignH::Center, EWidgetAlignV::Center);
    }
}

bool UIDragSource::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        if (hitTestLayoutRect(ctx.logicalPoint)) {
            _bPressed   = true;
            _pressPoint = ctx.logicalPoint;
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        return false;
    case EEvent::MouseMoved:
        if (_bPressed && getTree() && !getTree()->isDragging()) {
            const float dist = glm::length(ctx.logicalPoint - _pressPoint);
            if (dist > 6.0f) {
                WidgetTree* tree = getTree();
                tree->releasePointerCapture(this);
                tree->beginDrag(this, _makePayload ? _makePayload() : std::string{}, _label);
                _bPressed = false;
            }
        }
        return _bPressed;
    case EEvent::MouseButtonReleased:
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        return true;
    default:
        return false;
    }
}

// === UIDropTarget ===

void UIDropTarget::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _backgroundColor, nullptr);
    if (_bHighlighted) {
        // Accepted-hover highlight: rectangle outline (vector primitives).
        builder.addRectOutline(_layoutRect, _highlightColor, 2.0f);
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font) {
        builder.addText(_layoutRect, _bHighlighted ? "DROP HERE" : "Drop target",
                        _textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    }
}

} // namespace ya

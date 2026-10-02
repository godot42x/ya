#include "GUI/Widgets/Controls/ComboBox.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UIComboBox::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const
{
    node["control"] = {
        {"type", "comboBox"},
        {"selectedIndex", _selectedIndex},
        {"mixed", _bMixed},
        {"label", currentLabel()},
    };
}

void UIComboBox::setSelectedIndex(int index, bool bNotify)
{
    if (index < -1 || index >= static_cast<int>(_items.size())) {
        return;
    }
    if (index == _selectedIndex && !_bMixed) {
        return;
    }
    _selectedIndex = index;
    _bMixed        = false;
    markPaintDirty();
    if (bNotify && index >= 0 && _onSelectionChanged) {
        _onSelectionChanged(index);
    }
}

void UIComboBox::setMixed(bool mixed)
{
    if (_bMixed == mixed) {
        return;
    }
    _bMixed = mixed;
    if (mixed) {
        _selectedIndex = -1;
    }
    markPaintDirty();
}

void UIComboBox::select(int index)
{
    setSelectedIndex(index);
}

void UIComboBox::paintSelf(UIFrameBuilder& builder)
{
    const FComboBoxStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect,
                     resolveVisualFill(visualChrome(style),
                                       composeVisualFlags(_bHovered, false, false, !isEnabled(), false, false, false)));

    auto font = builder.getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    if (font) {
        Rect2D textRect = _layoutRect;
        textRect.pos.x += 10.0f;
        textRect.extent.x = std::max(0.0f, textRect.extent.x - 24.0f);
        builder.pushClip(textRect);
        builder.addText(textRect, currentLabel(), style.textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
        builder.popClip();

        // Dropdown arrow: two stacked triangles approximated with two rows of
        // squares, drawn at the right edge.
        const float arrowW = 10.0f;
        const float arrowX = _layoutRect.pos.x + _layoutRect.extent.x - arrowW - 8.0f;
        const float centerY = _layoutRect.pos.y + _layoutRect.extent.y * 0.5f;
        const float s = 2.2f;
        builder.addSprite(Rect2D{.pos = {arrowX + s, centerY - s}, .extent = {s * 3.0f, s}}, style.arrowColor, nullptr);
        builder.addSprite(Rect2D{.pos = {arrowX + s * 2.0f, centerY}, .extent = {s, s}}, style.arrowColor, nullptr);
    }
}

void UIComboBox::openDropdown()
{
    WidgetTree* tree = getTree();
    if (!tree || _items.empty()) {
        return;
    }

    std::vector<UIMenu::FItem> menuItems;
    for (size_t i = 0; i < _items.size(); ++i) {
        menuItems.push_back(UIMenu::FItem{
            .label  = _items[i],
            .action = [this, i]() { select(static_cast<int>(i)); },
        });
    }
    auto menu = UIMenu::create(menuItems);
    menu->openAt(*tree, _layoutRect);
}

bool UIComboBox::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat &&
            (keyEvent._keyCode == EKey::Enter || keyEvent._keyCode == EKey::Space ||
             keyEvent._keyCode == EKey::Down)) {
            openDropdown();
            return true;
        }
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        openDropdown();
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        return true;
    default:
        return false;
    }
}

} // namespace ya

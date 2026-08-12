#include "GUI/Widgets/Controls/ComboBox.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UIComboBox::select(int index)
{
    if (index < 0 || index >= static_cast<int>(_items.size()) || index == _selectedIndex) {
        return;
    }
    _selectedIndex = index;
    if (_onSelectionChanged) {
        _onSelectionChanged(index);
    }
}

void UIComboBox::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _bHovered ? _hoveredColor : _fieldColor, nullptr);

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font) {
        Rect2D textRect = _layoutRect;
        textRect.pos.x += 10.0f;
        textRect.extent.x = std::max(0.0f, textRect.extent.x - 24.0f);
        builder.addText(textRect, currentLabel(), _textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);

        // Dropdown arrow: two stacked triangles approximated with two rows of
        // squares, drawn at the right edge.
        const float arrowW = 10.0f;
        const float arrowX = _layoutRect.pos.x + _layoutRect.extent.x - arrowW - 8.0f;
        const float centerY = _layoutRect.pos.y + _layoutRect.extent.y * 0.5f;
        const float s = 2.2f;
        const glm::vec4 arrowColor{0.68f, 0.72f, 0.78f, 1.0f};
        builder.addSprite(Rect2D{.pos = {arrowX + s, centerY - s}, .extent = {s * 3.0f, s}}, arrowColor, nullptr);
        builder.addSprite(Rect2D{.pos = {arrowX + s * 2.0f, centerY}, .extent = {s, s}}, arrowColor, nullptr);
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
    const glm::vec2 pos{_layoutRect.pos.x, _layoutRect.pos.y + _layoutRect.extent.y};
    menu->openAt(*tree, pos);
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

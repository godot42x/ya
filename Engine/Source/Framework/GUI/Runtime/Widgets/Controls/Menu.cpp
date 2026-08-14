#include "GUI/Widgets/Controls/Menu.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ya
{

namespace
{

std::shared_ptr<Font> runtimeFont(uint32_t fontSize)
{
    return FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, fontSize);
}

} // namespace

void UIMenuItem::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _bHighlighted ? _hoveredColor : _normalColor, nullptr);
    auto font = runtimeFont(_fontSize);
    if (font) {
        Rect2D textRect = _layoutRect;
        // Symmetric horizontal padding: the menu sizes items from the same
        // measureText(), so label + 20px fits exactly with 10px each side.
        textRect.pos.x += 10.0f;
        textRect.extent.x = std::max(0.0f, textRect.extent.x - 20.0f);
        builder.addText(textRect, _label, _textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
    }
}

bool UIMenuItem::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();
    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseMoved:
        if (bPointInside && !_bHighlighted && _onHovered) {
            _onHovered(this);
        }
        return true;
    case EEvent::MouseButtonPressed:
        if (_onAction) {
            const auto action = _onAction;
            action();
        }
        return true;
    default:
        return false;
    }
}

std::shared_ptr<UIMenu> UIMenu::create(const std::vector<FItem>& items)
{
    auto menu = std::make_shared<UIMenu>();
    menu->rebuildContent(items);
    return menu;
}

void UIMenu::rebuildContent(const std::vector<FItem>& items)
{
    // Content panel: colored backdrop sized to the items.
    auto panel = std::make_shared<UIPanel>("MenuPanel");
    panel->_color = _panelColor;
    addDetachedChild(panel);

    auto list = std::make_shared<UIContainer>("MenuList");
    list->setDirection(EWidgetBoxLayout::Vertical);
    list->setSpacing(0.0f);
    list->setPadding(glm::vec2(_panelPadding));
    // Fill the panel rect: the panel is assigned the menu's own content
    // size by layoutAssigned(), so the list must span it (not keep its
    // default fixed size) for rows to receive the full menu width.
    list->_anchorMin = {0.0f, 0.0f};
    list->_anchorMax = {1.0f, 1.0f};
    list->_size      = {0.0f, 0.0f};
    panel->addDetachedChild(list);

    float maxLabelWidth = 0.0f;
    auto  font = runtimeFont(_fontSize);
    for (const FItem& item : items) {
        const float w = font ? font->measureText(item.label) : 0.0f;
        maxLabelWidth = std::max(maxLabelWidth, w);
    }
    // Menu content size: rows are maxLabelWidth + 20 (10px each side) and
    // _itemHeight tall, inside the panel padding. Kept as state so
    // layoutAssigned() sizes the panel from the real row metrics instead of
    // the panel's default fixed size.
    _contentExtent = {
        maxLabelWidth + 20.0f + _panelPadding * 2.0f,
        static_cast<float>(items.size()) * _itemHeight + _panelPadding * 2.0f,
    };

    for (size_t i = 0; i < items.size(); ++i) {
        auto menuItem = std::make_shared<UIMenuItem>(std::format("Menu{}", i));
        menuItem->_label    = items[i].label;
        menuItem->_fontSize = _fontSize;
        menuItem->_size     = {maxLabelWidth + 20.0f, _itemHeight};
        // Raw `this` capture is safe: the items are owned by the menu
        // (subtree), so the menu outlives every item lambda.
        menuItem->_onAction = [item = items[i].action, menu = this]()
        {
            if (item) {
                item();
            }
            menu->close();
        };
        menuItem->_onHovered = [menu = this](UIMenuItem* hovered)
        {
            const auto& entries = menu->menuItems();
            const auto  it      = std::find(entries.begin(), entries.end(), hovered);
            if (it != entries.end()) {
                menu->setHighlight(static_cast<int>(std::distance(entries.begin(), it)));
            }
        };
        list->addDetachedChild(menuItem);
        _items.push_back(menuItem.get());
    }
}

void UIMenu::openAt(WidgetTree& tree, const glm::vec2& pos)
{
    setRole(EOverlayRole::Popup);
    _contentPos = pos;
    open(tree);
}

void UIMenu::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect; // full screen

    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        // The panel is the menu's content: assign it the size derived from
        // the rows (computed in rebuildContent), anchored at _contentPos.
        // Never trust the panel's default fixed size here.
        child->layoutAssigned(Rect2D{
            .pos    = _contentPos,
            .extent = _contentExtent,
        });
        break;
    }
}

std::vector<UIMenuItem*> UIMenu::menuItems() const
{
    return _items;
}

void UIMenu::setHighlight(int index)
{
    if (_items.empty()) {
        _highlightIndex = -1;
        return;
    }
    _highlightIndex = (index < 0) ? static_cast<int>(_items.size()) - 1
                                  : (index >= static_cast<int>(_items.size()) ? 0 : index);
    for (int i = 0; i < static_cast<int>(_items.size()); ++i) {
        _items[static_cast<size_t>(i)]->_bHighlighted = (i == _highlightIndex);
    }
}

void UIMenu::activateHighlighted()
{
    if (_highlightIndex < 0 || _highlightIndex >= static_cast<int>(_items.size())) {
        return;
    }
    UIMenuItem* item = _items[static_cast<size_t>(_highlightIndex)];
    if (item && item->_onAction) {
        const auto action = item->_onAction;
        action();
    }
}

bool UIMenu::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();
    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent.bRepeat) {
            return true;
        }
        switch (keyEvent._keyCode) {
        case EKey::Escape:
            close();
            return true;
        case EKey::Down:
            setHighlight(_highlightIndex + 1);
            return true;
        case EKey::Up:
            setHighlight(_highlightIndex - 1);
            return true;
        case EKey::Enter:
        case EKey::Space:
            activateHighlighted();
            return true;
        default:
            return false;
        }
    }
    // Shield click (content did not consume) and modal dimming handled by
    // the base popup overlay.
    return UIPopupOverlay::handleInputEvent(event, ctx);
}

} // namespace ya

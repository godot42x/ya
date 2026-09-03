#include "GUI/Widgets/Controls/Menu.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "Render/Resources/FontManager.h"
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

bool pointInMenuRect(const glm::vec2& point, const Rect2D& rect)
{
    return point.x >= rect.pos.x && point.x <= rect.pos.x + rect.extent.x &&
           point.y >= rect.pos.y && point.y <= rect.pos.y + rect.extent.y;
}

bool isSeparatorItem(const UIMenu::FItem& item)
{
    return item.bSeparator;
}

int nextSelectableMenuIndex(const std::vector<UIMenuItem*>& items, int currentIndex, int delta)
{
    if (items.empty()) {
        return -1;
    }

    const int count = static_cast<int>(items.size());
    int       index = currentIndex;
    for (int step = 0; step < count; ++step) {
        index += delta;
        if (index < 0) {
            index = count - 1;
        }
        else if (index >= count) {
            index = 0;
        }
        if (items[static_cast<size_t>(index)] && !items[static_cast<size_t>(index)]->_bSeparator &&
            items[static_cast<size_t>(index)]->_bEnabled) {
            return index;
        }
    }
    return -1;
}

} // namespace

void UIMenuItem::paintSelf(UIFrameBuilder& builder)
{
    const FMenuStyle& style = resolvedStyle();
    if (_bSeparator) {
        const float y = _layoutRect.pos.y + _layoutRect.extent.y * 0.5f;
        builder.addLine({_layoutRect.pos.x + UIMenu::kItemHorizontalPadding, y},
                        {_layoutRect.pos.x + _layoutRect.extent.x - UIMenu::kItemHorizontalPadding, y},
                        style.separatorColor,
                        1.0f);
        return;
    }
    builder.addBrush(_layoutRect,
                     resolveVisualFill(visualChrome(style),
                                       composeVisualFlags(_bHighlighted, false, false, !_bEnabled, false, false, false)));
    auto font = runtimeFont(_fontSize);
    if (font) {
        Rect2D textRect = _layoutRect;
        textRect.pos.x += UIMenu::kItemHorizontalPadding;
        textRect.extent.x = std::max(0.0f, textRect.extent.x - UIMenu::kItemHorizontalPadding * 2.0f);
        const glm::vec4 textColor = _bEnabled ? style.textColor : style.disabledTextColor;
        const glm::vec4 iconColor = _bEnabled ? style.iconColor : style.disabledIconColor;
        if (_bChecked) {
            Rect2D checkRect{
                .pos = {textRect.pos.x, _layoutRect.pos.y},
                .extent = {UIMenu::kCheckmarkColumnWidth, _layoutRect.extent.y},
            };
            builder.addText(checkRect, "v", style.checkmarkColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
            textRect.pos.x += _reservedCheckmarkExtent;
            textRect.extent.x = std::max(0.0f, textRect.extent.x - _reservedCheckmarkExtent);
        }
        else if (_reservedCheckmarkExtent > 0.0f) {
            textRect.pos.x += _reservedCheckmarkExtent;
            textRect.extent.x = std::max(0.0f, textRect.extent.x - _reservedCheckmarkExtent);
        }
        if (!_icon.empty()) {
            Rect2D iconRect{
                .pos = {textRect.pos.x, _layoutRect.pos.y},
                .extent = {UIMenu::kIconColumnWidth, _layoutRect.extent.y},
            };
            builder.addText(iconRect, _icon, iconColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
            textRect.pos.x += _reservedIconExtent;
            textRect.extent.x = std::max(0.0f, textRect.extent.x - _reservedIconExtent);
        }
        else if (_reservedIconExtent > 0.0f) {
            textRect.pos.x += _reservedIconExtent;
            textRect.extent.x = std::max(0.0f, textRect.extent.x - _reservedIconExtent);
        }
        if (_bShowsSubmenu) {
            Rect2D arrowRect{
                .pos = {
                    _layoutRect.pos.x + _layoutRect.extent.x -
                        (UIMenu::kItemHorizontalPadding + UIMenu::kSubmenuColumnWidth),
                    _layoutRect.pos.y,
                },
                .extent = {UIMenu::kSubmenuColumnWidth, _layoutRect.extent.y},
            };
            builder.addText(arrowRect, ">", _bEnabled ? style.submenuArrowColor : style.disabledIconColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
            textRect.extent.x = std::max(0.0f, arrowRect.pos.x - UIMenu::kSubmenuColumnGap - textRect.pos.x);
        }
        float rightLimit = _layoutRect.pos.x + _layoutRect.extent.x - UIMenu::kItemHorizontalPadding - _reservedSubmenuExtent;
        if (!_shortcut.empty()) {
            const float shortcutWidth = font->measureText(_shortcut);
            Rect2D shortcutRect{
                .pos = {rightLimit - shortcutWidth, _layoutRect.pos.y},
                .extent = {shortcutWidth, _layoutRect.extent.y},
            };
            builder.addText(shortcutRect, _shortcut, _bEnabled ? style.shortcutColor : style.disabledTextColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
            rightLimit = shortcutRect.pos.x - (_reservedShortcutExtent - shortcutWidth);
        }
        textRect.extent.x = std::max(0.0f, rightLimit - textRect.pos.x);
        builder.addText(textRect, _label, textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
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
        if (_bSeparator) {
            return false;
        }
        if (!_bEnabled) {
            return true;
        }
        if (bPointInside && !_bHighlighted && _onHovered) {
            _onHovered(this);
        }
        return true;
    case EEvent::MouseButtonPressed:
        if (_bSeparator) {
            return false;
        }
        if (!_bEnabled) {
            return true;
        }
        if (_bShowsSubmenu) {
            if (_onHovered) {
                _onHovered(this);
            }
            return true;
        }
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
    _items.clear();
    _highlightIndex = -1;
    _openSubmenu.reset();
    _openSubmenuItem = nullptr;

    // Content panel: colored backdrop sized to the items.
    auto panel = std::make_shared<UIPanel>("MenuPanel");
    panel->_styleKey = "menu.panel";
    addDetachedChild(panel);

    auto list = std::make_shared<UIContainer>("MenuList");
    list->setDirection(EWidgetBoxLayout::Vertical);
    list->setSpacing(0.0f);
    list->setPadding(glm::vec2(_panelPadding));
    panel->addDetachedChild(list);
    // Fill the panel rect: the panel is assigned the menu's own content size by
    // layoutAssigned(), so the list must span it (not keep its default fixed
    // size) for rows to receive the full menu width. Stretch intent lives on
    // the parent->child slot edge, never on the child.
    if (UISlot* edge = panel->getSlotForChild(*list); edge && edge->as<UICanvasSlot>()) {
        auto* slot = edge->as<UICanvasSlot>();
        FCanvasSlotArgs fillArgs;
        fillArgs.anchorMin = {0.0f, 0.0f};
        fillArgs.anchorMax = {1.0f, 1.0f};
        slot->apply(fillArgs);
    }

    float maxLabelWidth = 0.0f;
    bool  bAnyIcon = false;
    bool  bAnyCheckmark = false;
    bool  bAnySubmenu = false;
    bool  bAnyShortcut = false;
    float totalHeight = 0.0f;
    float maxShortcutWidth = 0.0f;
    auto  font = runtimeFont(_fontSize);
    for (const FItem& item : items) {
        totalHeight += isSeparatorItem(item) ? kSeparatorHeight : _itemHeight;
        if (isSeparatorItem(item)) {
            continue;
        }
        const float w = font ? font->measureText(item.label) : 0.0f;
        maxLabelWidth = std::max(maxLabelWidth, w);
        bAnyIcon = bAnyIcon || !item.icon.empty();
        bAnyCheckmark = bAnyCheckmark || item.bChecked;
        bAnySubmenu = bAnySubmenu || static_cast<bool>(item.submenuFactory);
        bAnyShortcut = bAnyShortcut || !item.shortcut.empty();
        maxShortcutWidth = std::max(maxShortcutWidth, font ? font->measureText(item.shortcut) : 0.0f);
    }
    _checkmarkColumnExtent = bAnyCheckmark ? (kCheckmarkColumnWidth + kCheckmarkColumnGap) : 0.0f;
    _iconColumnExtent = bAnyIcon ? (kIconColumnWidth + kIconColumnGap) : 0.0f;
    _shortcutColumnExtent = bAnyShortcut ? (kShortcutColumnGap + maxShortcutWidth) : 0.0f;
    _submenuColumnExtent = bAnySubmenu ? (kSubmenuColumnGap + kSubmenuColumnWidth) : 0.0f;
    const float rowWidth = kItemHorizontalPadding * 2.0f + _checkmarkColumnExtent + _iconColumnExtent +
                           maxLabelWidth + _shortcutColumnExtent + _submenuColumnExtent;
    // Menu content size: rows are maxLabelWidth + 20 (10px each side) and
    // _itemHeight tall, inside the panel padding. Kept as state so
    // layoutAssigned() sizes the panel from the real row metrics instead of
    // the panel's default fixed size.
    _contentExtent = {
        rowWidth + _panelPadding * 2.0f,
        totalHeight + _panelPadding * 2.0f,
    };

    for (size_t i = 0; i < items.size(); ++i) {
        auto menuItem = std::make_shared<UIMenuItem>(std::format("Menu{}", i));
        const bool bSeparator = isSeparatorItem(items[i]);
        menuItem->_label    = items[i].label;
        menuItem->_icon     = items[i].icon;
        menuItem->_shortcut = items[i].shortcut;
        menuItem->_fontSize = _fontSize;
        menuItem->_styleKey = _styleKey;
        menuItem->_bSeparator = bSeparator;
        menuItem->_bChecked = items[i].bChecked;
        menuItem->_bEnabled = items[i].bEnabled;
        menuItem->_bShowsSubmenu = static_cast<bool>(items[i].submenuFactory);
        menuItem->_reservedCheckmarkExtent = _checkmarkColumnExtent;
        menuItem->_reservedIconExtent = _iconColumnExtent;
        menuItem->_reservedShortcutExtent = _shortcutColumnExtent;
        menuItem->_reservedSubmenuExtent = _submenuColumnExtent;
        // Raw `this` capture is safe: the items are owned by the menu
        // (subtree), so the menu outlives every item lambda.
        menuItem->_onAction = [item = items[i].action, menu = this]()
        {
            if (item) {
                item();
            }
            menu->closeMenuChain();
        };
        menuItem->_onHovered = [menu = this, submenuFactory = items[i].submenuFactory](UIMenuItem* hovered)
        {
            const auto& entries = menu->menuItems();
            const auto  it      = std::find(entries.begin(), entries.end(), hovered);
            if (it != entries.end()) {
                menu->setHighlight(static_cast<int>(std::distance(entries.begin(), it)));
            }
            if (hovered->_bSeparator) {
                return;
            }
            if (!hovered->_bEnabled) {
                menu->closeOpenSubmenu();
                return;
            }
            if (submenuFactory) {
                menu->openSubmenuFor(hovered, submenuFactory);
            }
            else {
                menu->closeOpenSubmenu();
            }
        };
        list->addDetachedChild(menuItem);
        if (UISlot* edge = list->getSlotForChild(*menuItem); edge && edge->as<UIBoxSlot>()) {
            auto* slot = edge->as<UIBoxSlot>();
            slot->setPreferredSize({rowWidth, bSeparator ? kSeparatorHeight : _itemHeight});
        }
        _items.push_back(menuItem.get());
    }
}

void UIMenu::openAt(WidgetTree& tree, const glm::vec2& pos)
{
    setRole(EOverlayRole::Popup);
    _contentPos = pos;
    open(tree);
}

FCanvasSlotArgs UIMenu::resolveContentSlotArgs(const UIElement& child) const
{
    (void)child;
    FCanvasSlotArgs args;
    args.offset    = _contentPos;
    args.fixedSize = _contentExtent;
    return args;
}

std::vector<UIMenuItem*> UIMenu::menuItems() const
{
    return _items;
}

void UIMenu::setHighlight(int index)
{
    if (_items.empty()) {
        _highlightIndex = -1;
        closeOpenSubmenu();
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
    if (item && !item->_bEnabled) {
        return;
    }
    if (item && item->_bShowsSubmenu) {
        if (item->_onHovered) {
            item->_onHovered(item);
        }
        return;
    }
    if (item && item->_onAction) {
        const auto action = item->_onAction;
        action();
    }
}

void UIMenu::openSubmenuFor(UIMenuItem* item, std::function<std::shared_ptr<UIMenu>()> submenuFactory)
{
    if (!item || !submenuFactory) {
        closeOpenSubmenu();
        return;
    }
    if (_openSubmenu && _openSubmenuItem == item) {
        return;
    }

    closeOpenSubmenu();
    WidgetTree* tree = getTree();
    if (!tree) {
        return;
    }

    auto submenu = submenuFactory();
    if (!submenu) {
        return;
    }

    submenu->_parentMenu = this;
    _openSubmenuItem = item;
    _openSubmenu = submenu;
    submenu->_onDismiss = [this, item]()
    {
        if (_openSubmenuItem == item) {
            _openSubmenu.reset();
            _openSubmenuItem = nullptr;
        }
    };

    const glm::vec2 pos{
        item->_layoutRect.pos.x + item->_layoutRect.extent.x - _panelPadding,
        item->_layoutRect.pos.y - _panelPadding,
    };
    submenu->openAt(*tree, pos);
}

void UIMenu::closeOpenSubmenu()
{
    if (_openSubmenu) {
        const auto submenu = _openSubmenu;
        _openSubmenu.reset();
        _openSubmenuItem = nullptr;
        submenu->closeOpenSubmenu();
        submenu->close();
    }
}

void UIMenu::closeMenuChain()
{
    UIMenu* parent = _parentMenu;
    closeOpenSubmenu();
    close();
    if (parent) {
        parent->closeMenuChain();
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
            closeMenuChain();
            return true;
        case EKey::Down:
            setHighlight(nextSelectableMenuIndex(_items, _highlightIndex, +1));
            return true;
        case EKey::Up:
            setHighlight(nextSelectableMenuIndex(_items, _highlightIndex, -1));
            return true;
        case EKey::Right:
            if (_highlightIndex >= 0 && _highlightIndex < static_cast<int>(_items.size())) {
                UIMenuItem* item = _items[static_cast<size_t>(_highlightIndex)];
                if (item && item->_bShowsSubmenu && item->_onHovered) {
                    item->_onHovered(item);
                    return true;
                }
            }
            return false;
        case EKey::Left:
            if (_parentMenu) {
                if (WidgetTree* tree = getTree()) {
                    tree->setFocus(_parentMenu);
                }
                closeOpenSubmenu();
                close();
                return true;
            }
            return false;
        case EKey::Enter:
        case EKey::Space:
            activateHighlighted();
            return true;
        default:
            return false;
        }
    }
    if (eventType == EEvent::MouseButtonPressed) {
        const Rect2D* content = contentLayoutRect();
        const bool bInsideSelf = content && pointInMenuRect(ctx.logicalPoint, *content);
        if (!bInsideSelf) {
            if (_parentMenu) {
                const Rect2D* parentContent = _parentMenu->contentLayoutRect();
                if (parentContent && pointInMenuRect(ctx.logicalPoint, *parentContent)) {
                    close();
                    return false;
                }
            }
            closeMenuChain();
            return true;
        }
    }
    // Shield click (content did not consume) and modal dimming handled by
    // the base popup overlay.
    return UIPopupOverlay::handleInputEvent(event, ctx);
}

} // namespace ya

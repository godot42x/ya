#include "GUI/Widgets/Controls/MenuBar.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>

namespace ya
{

UIMenuBarItem::UIMenuBarItem(std::string name)
    : UIElement(std::move(name), "menubar")
{
    _hitFilter   = EWidgetHitFilter::Stop;
    _focusPolicy = EWidgetFocusPolicy::Focusable;
}

void UIMenuBarItem::paintSelf(UIFrameBuilder& builder)
{
    // Resolve the (possibly reactive) label first so the dependency is
    // recorded even when the font is unavailable and the text is skipped
    // (mirrors UIText::resolvedText ordering).
    const std::string& label = resolvedLabel(ReactiveBase::EDirtyLevel::Paint);

    // Theme resolution (style-system Phase 3): the item draws its normal /
    // hovered fill brushes + the label color from FMenuBarItemStyle. Row
    // extent comes from the parent-owned box slot, not child geometry.
    const FMenuBarItemStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect,
                     resolveVisualFill(visualChrome(style),
                                       composeVisualFlags(_bHovered, false, false, !isEnabled(), false, false, false)));
    const float separatorY = _layoutRect.pos.y + _layoutRect.extent.y - 0.5f;
    builder.addLine({_layoutRect.pos.x, separatorY},
                    {_layoutRect.pos.x + _layoutRect.extent.x, separatorY},
                    style.separatorColor,
                    1.0f);
    auto font = builder.getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font) {
        builder.addText(_layoutRect, label, style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    }
}

void UIMenuBarItem::onPointerEnter()
{
    _bHovered = true;
    if (_onHoveredEnter) {
        _onHoveredEnter(this);
    }
}

bool UIMenuBarItem::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Enter || keyEvent._keyCode == EKey::Space)) {
            if (_onActivate) {
                _onActivate();
            }
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
        if (_onActivate) {
            _onActivate();
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        return true;
    default:
        return false;
    }
}

UIMenuBarItem* UIMenuBar::addItem(const std::string& label, std::function<std::shared_ptr<UIMenu>()> menuFactory)
{
    auto item = std::make_shared<UIMenuBarItem>(std::format("MenuBar_{}", label));
    item->_label     = label;
    _entries.push_back({label, menuFactory});
    // Size from the same font measurement the menu items use, so bar labels
    // never overflow their button and stay consistent with the menus.
    const auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, item->_fontSize);
    const glm::vec2 itemSize{
        (font ? font->measureText(label) : static_cast<float>(label.size()) * 7.0f) + 20.0f,
        26.0f,
    };
    item->_onActivate = [this, item, menuFactory]()
    {
        // A press on a menu-bar item while a menu is open is dismissed by
        // the popup shield first (standard menu-bar behavior), so this path
        // always opens fresh: close whatever is open, then open this one.
        closeOpenMenu();
        if (!menuFactory) {
            return;
        }
        auto menu = menuFactory();
        if (!menu) {
            return;
        }
        WidgetTree* tree = getTree();
        if (!tree) {
            return;
        }
        _openMenu = menu;
        _openMenuItem = item.get();
        menu->_onDismiss = [this]() {
            _openMenu.reset();
            _openMenuItem = nullptr;
        };
        // Anchor the menu below the bar item.
        menu->openAt(*tree, item->_layoutRect);
    };
    // While a menu is open, moving onto another entry switches to that
    // entry's menu (classic menu-bar hover behavior). Moving onto the entry
    // that owns the open menu keeps it open.
    item->_onHoveredEnter = [this, item, menuFactory](UIMenuBarItem* /*entered*/)
    {
        if (!_openMenu || _openMenuItem == item.get()) {
            return;
        }
        if (!menuFactory) {
            closeOpenMenu();
            return;
        }
        if (item->_onActivate) {
            item->_onActivate();
        }
    };
    addDetachedChild(item);
    if (UISlot* edge = getSlotForChild(*item); edge && edge->as<UIBoxSlot>()) {
        auto* slot = edge->as<UIBoxSlot>();
        slot->setPreferredSize(itemSize);
    }
    return item.get();
}

void UIMenuBar::closeOpenMenu()
{
    if (_openMenu) {
        const auto menu = _openMenu;
        _openMenu.reset();
        menu->close();
    }
}

} // namespace ya

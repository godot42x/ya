#include "GUI/Widgets/Controls/SearchComboBox.h"

#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/StringMatch.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <string>
#include <vector>

namespace ya
{

std::vector<int> UISearchComboBox::filteredIndices() const
{
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(_items.size()); ++i) {
        if (_filter.empty() ||
            stringContains(_items[i],
                           _filter,
                           _bCaseSensitive ? EStringMatchCase::Sensitive : EStringMatchCase::Ignore)) {
            indices.push_back(i);
        }
    }
    return indices;
}

void UISearchComboBox::openFilteredMenu()
{
    // Close the OLD menu as a refresh: its dismiss must not clear the
    // filter that was just typed.
    _bRefreshingMenu = true;
    closeMenu();
    _bRefreshingMenu = false;

    const auto indices = filteredIndices();
    if (indices.empty()) {
        return;
    }
    std::vector<UIMenu::FItem> entries;
    entries.reserve(indices.size());
    for (int index : indices) {
        entries.push_back(UIMenu::FItem{.label = _items[index],
                                        .action = [this, index]
                                        {
                                            _selectedIndex = index;
                                            _filter.clear();
                                            invalidateProperty(EUIPropertyImpact::Paint);
                                            if (_onSelectionChanged) {
                                                _onSelectionChanged(index);
                                            }
                                            closeMenu();
                                        }});
    }
    auto menu = UIMenu::create(std::move(entries));
    menu->_onDismiss = [this]()
    {
        _openMenu.reset();
        if (!_bRefreshingMenu) {
            // Real close (outside click / Esc / pick): stop showing the
            // filter text and fall back to the selected label, and release
            // focus so the control does not keep painting '(type to filter)'.
            _filter.clear();
            invalidateProperty(EUIPropertyImpact::Paint);
            if (WidgetTree* tree = getTree()) {
                if (tree->getFocused() == this) {
                    tree->setFocus(nullptr);
                }
            }
        }
    };
    _openMenu = menu;
    if (WidgetTree* tree = getTree()) {
        menu->openAt(*tree, {_layoutRect.pos.x, _layoutRect.pos.y + _layoutRect.extent.y});
        // The popup steals focus for its own keyboard navigation; take it
        // back so typed characters keep filtering (menu navigation stays
        // mouse-driven for this control, Esc is handled here).
        tree->setFocus(this);
    }
}

void UISearchComboBox::closeMenu()
{
    if (_openMenu) {
        const auto menu = _openMenu;
        _openMenu.reset();
        menu->close();
    }
}

void UISearchComboBox::clearTransientInputState()
{
    _bHovered = false;
    _bFocused = false;
    _filter.clear();
    closeMenu();
}

void UISearchComboBox::paintSelf(UIFrameBuilder& builder)
{
    const FSearchComboStyle& style = resolvedStyle();
    builder.addBrush(_layoutRect, _bHovered ? style.hoveredFill : style.backgroundFill);
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }
    const std::string shown = _bFocused ? (_filter.empty() ? "(type to filter)" : _filter)
                                        : currentLabel();
    builder.addText(_layoutRect, shown, style.textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
    if (_bFocused) {
        const float caretX = _layoutRect.pos.x + 4.0f + font->measureText(shown);
        const float caretY = _layoutRect.pos.y + (_layoutRect.extent.y - font->lineHeight) * 0.5f;
        builder.addSprite(Rect2D{.pos = {caretX, caretY}, .extent = {1.0f, font->lineHeight}},
                          style.caretColor, nullptr);
    }
}

bool UISearchComboBox::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyTyped) {
        if (_bFocused) {
            _filter += static_cast<const KeyTypedEvent&>(event).getText();
            invalidateProperty(EUIPropertyImpact::Paint);
            openFilteredMenu();
            return true;
        }
        return false;
    }

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (_bFocused && !keyEvent.bRepeat && keyEvent._keyCode == EKey::Escape) {
            // The menu shield cannot see Esc (focus sits on this control),
            // so close the menu here.
            closeMenu();
            return true;
        }
        if (_bFocused && !keyEvent.bRepeat && keyEvent._keyCode == EKey::Backspace) {
            if (!_filter.empty()) {
                _filter.pop_back();
                invalidateProperty(EUIPropertyImpact::Paint);
                openFilteredMenu();
            }
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        if (hitTestLayoutRect(ctx.logicalPoint)) {
            if (WidgetTree* tree = getTree()) {
                tree->setFocus(this);
            }
            openFilteredMenu();
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseMoved) {
        const bool bInside = hitTestLayoutRect(ctx.logicalPoint);
        _bHovered = bInside;
        return bInside;
    }

    return false;
}

} // namespace ya

#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/WidgetTree.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <algorithm>

namespace ya
{

void UITabButton::paintSelf(UIFrameBuilder& builder)
{
    if (_bSelected) {
        // Selected tab reads as "connected to the content below": the fill is
        // the editor-chrome base, with a thin accent bar along the top edge.
        // Drawing a slightly taller rect than our own (the bar's padding is
        // above us) would look connected, but the bar clips children, so keep
        // the fill inside this button and let the bar's bottom rule separate
        // the strip from content.
        builder.addSprite(_layoutRect, _selectedColor, nullptr);
        const Rect2D accent{_layoutRect.pos, {_layoutRect.extent.x, 2.0f}};
        builder.addSprite(accent, _accentColor, nullptr);
    }
    else {
        const glm::vec4 color = _bHovered ? _hoveredColor : _normalColor;
        builder.addSprite(_layoutRect, color, nullptr);
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font) {
        builder.addText(_layoutRect, _label, _textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    }
}

glm::vec2 UITabButton::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    const float textWidth = font ? font->measureText(_label) : static_cast<float>(_label.size()) * 7.0f;
    return {textWidth + _padding.x * 2.0f, _padding.y * 2.0f + (font ? font->lineHeight : 14.0f)};
}

bool UITabButton::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent.bRepeat) {
            return true;
        }
        switch (keyEvent._keyCode) {
        case EKey::Left:
            if (_onNavigate) {
                _onNavigate(-1);
            }
            return true;
        case EKey::Right:
            if (_onNavigate) {
                _onNavigate(1);
            }
            return true;
        case EKey::Enter:
        case EKey::Space:
            if (_onActivated) {
                _onActivated();
            }
            return true;
        default:
            return false;
        }
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    if (eventType == EEvent::MouseButtonPressed && _onDragArmed) {
        _bPressed   = true;
        _pressPoint = ctx.logicalPoint;
        if (WidgetTree* tree = getTree()) {
            tree->setPointerCapture(this);
        }
        return true;
    }
    if (eventType == EEvent::MouseMoved && _bPressed && _onDragArmed && getTree() && !getTree()->isDragging()) {
        if (glm::length(ctx.logicalPoint - _pressPoint) > 6.0f) {
            _bPressed = false;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            _onDragArmed();
        }
        return true;
    }
    if (eventType == EEvent::MouseButtonReleased && _bPressed) {
        _bPressed = false;
        if (WidgetTree* tree = getTree()) {
            tree->releasePointerCapture(this);
        }
        // A release without crossing the threshold is a normal click.
        if (_onActivated) {
            _onActivated();
        }
        return true;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed:
        if (_onActivated) {
            _onActivated();
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = bPointInside;
        return true;
    default:
        return false;
    }
}

UITabButton* UITabBar::addTab(const std::string& label)
{
    auto button = std::make_shared<UITabButton>(std::format("Tab_{}", label));
    button->_label     = label;
    button->_bAutoSize = true;

    const int index = static_cast<int>(_tabs.size());
    button->_onActivated = [this, index]() { selectTab(index); };
    button->_onNavigate  = [this](int delta) { navigate(delta); };

    if (_bDraggableTabs) {
        // DockSpace tab drag: a press on the tab arms a drag; crossing the
        // threshold starts a tree session carrying dock-tab:<label>.
        button->_onDragArmed = [this, index]()
        {
            if (index >= 0 && index < static_cast<int>(_tabs.size())) {
                _onTabDragBegin(index, _tabs[static_cast<size_t>(index)]->_label);
            }
        };
    }

    addDetachedChild(button);
    _tabs.push_back(button.get());
    return button.get();
}

std::string UITabBar::removeTab(int index)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size())) {
        return {};
    }
    UITabButton* button = _tabs[static_cast<size_t>(index)];
    const std::string label = button->_label;
    if (WidgetTree* tree = getTree()) {
        tree->detach(*button);
    }
    _tabs.erase(_tabs.begin() + index);
    // Re-point the remaining buttons' indices (activation lambdas captured
    // the old index).
    for (size_t i = 0; i < _tabs.size(); ++i) {
        const size_t newIndex = i;
        _tabs[i]->_onActivated = [this, newIndex]() { selectTab(static_cast<int>(newIndex)); };
    }
    if (_selectedIndex >= static_cast<int>(_tabs.size())) {
        _selectedIndex = static_cast<int>(_tabs.size()) - 1;
    }
    else if (index < _selectedIndex) {
        --_selectedIndex;
    }
    syncSelectedTab(_selectedIndex);
    markLayoutDirty();
    return label;
}

void UITabBar::selectTab(int index)
{
    syncSelectedTab(index);
    if (_selectedIndex >= 0 && _onTabSelected) {
        _onTabSelected(_selectedIndex);
    }
}

void UITabBar::syncSelectedTab(int index)
{
    if (_tabs.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(_tabs.size()) - 1);
    if (index == _selectedIndex) {
        return;
    }
    _selectedIndex = index;
    for (int i = 0; i < static_cast<int>(_tabs.size()); ++i) {
        _tabs[static_cast<size_t>(i)]->_bSelected = (i == index);
        // _bSelected is a reflect-ed bool, not a VisualFlag: mark paint-dirty
        // manually so the tab highlight re-paints.
        _tabs[static_cast<size_t>(i)]->markPaintDirty();
    }
}

void UITabBar::navigate(int delta)
{
    if (_tabs.empty()) {
        return;
    }
    int next = _selectedIndex < 0 ? 0 : (_selectedIndex + delta) % static_cast<int>(_tabs.size());
    if (next < 0) {
        next += static_cast<int>(_tabs.size());
    }
    selectTab(next);
    if (WidgetTree* tree = getTree()) {
        tree->setFocus(_tabs[static_cast<size_t>(next)], /*bFromKeyboard=*/true);
    }
}

glm::vec2 UITabBar::computeDesiredSize() const
{
    const glm::vec2 base = UIContainer::computeDesiredSize();
    if (!_tabs.empty() || _emptyPlaceholder.empty()) {
        return base;
    }
    // Empty bar: keep a header-sized height so the zone stays a visible
    // drop target instead of collapsing to a 0-height sliver.
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 13);
    // Match the constructor's padding {4,4}: keep a header-sized height when
    // the bar is empty so the zone stays a visible drop target.
    const float headerH = 4.0f * 2.0f + (font ? font->lineHeight : 14.0f);
    return {base.x, std::max(base.y, headerH)};
}

void UITabBar::paintSelf(UIFrameBuilder& builder)
{
    if (!_tabs.empty()) {
        // Bottom rule separating the strip from the content host below it.
        const float y = _layoutRect.pos.y + _layoutRect.extent.y - 1.0f;
        builder.addLine({_layoutRect.pos.x, y},
                        {_layoutRect.pos.x + _layoutRect.extent.x, y},
                        {0.28f, 0.30f, 0.36f, 1.0f},
                        1.0f);
    }
    if (_tabs.empty() && !_emptyPlaceholder.empty()) {
        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 13);
        if (font) {
            builder.addText(_layoutRect, _emptyPlaceholder, {0.45f, 0.48f, 0.55f, 1.0f},
                            font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        }
    }
}

} // namespace ya

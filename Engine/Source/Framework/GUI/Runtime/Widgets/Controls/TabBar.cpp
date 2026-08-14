#include "GUI/Widgets/Controls/TabBar.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

void UITabButton::paintSelf(UIFrameBuilder& builder)
{
    const glm::vec4 color = _bSelected ? _selectedColor : (_bHovered ? _hoveredColor : _normalColor);
    builder.addSprite(_layoutRect, color, nullptr);
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

    addDetachedChild(button);
    _tabs.push_back(button.get());
    return button.get();
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

} // namespace ya

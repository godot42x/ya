#include "GUI/Widgets/Controls/InputExtras.h"

#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <algorithm>

namespace ya
{

namespace
{

float clampValue(float value, float minValue, float maxValue)
{
    return std::clamp(value, std::min(minValue, maxValue), std::max(minValue, maxValue));
}

} // namespace

// === UIDragFloat ===

void UIDragFloat::setValue(float value)
{
    value = clampValue(value, _min, _max);
    if (_value == value) {
        return;
    }
    _value = value;
    invalidateProperty(EUIPropertyImpact::Paint);
    if (_onValueChanged) {
        _onValueChanged(_value);
    }
}

void UIDragFloat::adjustValue(float delta)
{
    setValue(_value + delta * _speed);
}

void UIDragFloat::beginEdit()
{
    _bEditing    = true;
    _editBuffer  = std::format("{:.{}f}", _value, _decimals);
    if (WidgetTree* tree = getTree()) {
        tree->setFocus(this);
    }
}

void UIDragFloat::commitEdit()
{
    if (!_bEditing) {
        return;
    }
    _bEditing = false;
    try {
        const float parsed = std::stof(_editBuffer);
        setValue(parsed);
    }
    catch (...) {
        // Invalid text: keep the previous value.
    }
    _editBuffer.clear();
}

void UIDragFloat::cancelEdit()
{
    _bEditing = false;
    _editBuffer.clear();
}

void UIDragFloat::onFocusLost()
{
    commitEdit();
}

void UIDragFloat::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _bDragging ? _draggingColor : _backgroundColor, nullptr);
    builder.addRectOutline(_layoutRect, _borderColor, 1.0f);
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }
    const std::string shown = _bEditing ? _editBuffer : std::format("{:.{}f}", _value, _decimals);
    builder.addText(_layoutRect, shown, _textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    if (_bEditing) {
        const float textW  = font->measureText(shown);
        const float caretX = _layoutRect.pos.x + (_layoutRect.extent.x + textW) * 0.5f + 1.0f;
        const float caretY = _layoutRect.pos.y + (_layoutRect.extent.y - font->lineHeight) * 0.5f;
        builder.addSprite(Rect2D{.pos = {caretX, caretY}, .extent = {1.0f, font->lineHeight}},
                          _textColor, nullptr);
    }
}

bool UIDragFloat::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (_bEditing) {
        if (eventType == EEvent::KeyTyped) {
            _editBuffer += static_cast<const KeyTypedEvent&>(event).getText();
            invalidateProperty(EUIPropertyImpact::Paint);
            return true;
        }
        if (eventType == EEvent::KeyPressed) {
            const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Backspace) {
                if (!_editBuffer.empty()) {
                    _editBuffer.pop_back();
                    invalidateProperty(EUIPropertyImpact::Paint);
                }
                return true;
            }
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Enter) {
                commitEdit();
                return true;
            }
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Escape) {
                cancelEdit();
                return true;
            }
        }
        return false;
    }

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (keyEvent._keyCode == EKey::Left) {
            adjustValue(-10.0f * (keyEvent._mod & 0x1 ? 10.0f : 1.0f));
            return true;
        }
        if (keyEvent._keyCode == EKey::Right) {
            adjustValue(10.0f * (keyEvent._mod & 0x1 ? 10.0f : 1.0f));
            return true;
        }
        return false;
    }

    const bool bPointInside = hitTestLayoutRect(ctx.logicalPoint);
    if (!ctx.bViaCapture && !bPointInside) {
        return false;
    }

    switch (eventType) {
    case EEvent::MouseButtonPressed: {
        // Double-click (no event timestamps: a press near the previous press)
        // enters text edit mode; a single press starts the capture drag.
        const bool bDouble = _bHasLastPress && glm::length(ctx.logicalPoint - _lastPressPos) < 6.0f;
        _lastPressPos   = ctx.logicalPoint;
        _bHasLastPress  = true;
        if (bDouble) {
            _bHasLastPress = false;
            beginEdit();
            return true;
        }
        _bDragging = true;
        _dragStart = ctx.logicalPoint;
        if (WidgetTree* tree = getTree()) {
            tree->setPointerCapture(this);
        }
        return true;
    }
    case EEvent::MouseMoved:
        if (_bDragging) {
            adjustValue((ctx.logicalPoint.x - _dragStart.x) * 0.1f);
            _dragStart = ctx.logicalPoint;
        }
        return true;
    case EEvent::MouseButtonReleased:
        if (_bDragging) {
            _bDragging = false;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
        }
        return true;
    default:
        return false;
    }
}

// === UISpinBox ===

void UISpinBox::setValue(float value)
{
    value = clampValue(value, _min, _max);
    if (_value == value) {
        return;
    }
    _value = value;
    invalidateProperty(EUIPropertyImpact::Paint);
    if (_onValueChanged) {
        _onValueChanged(_value);
    }
}

void UISpinBox::stepBy(float multiplier)
{
    setValue(_value + _step * multiplier);
}

void UISpinBox::beginEdit()
{
    _bEditing   = true;
    _editBuffer = std::format("{:.2f}", _value);
    if (WidgetTree* tree = getTree()) {
        tree->setFocus(this);
    }
}

void UISpinBox::commitEdit()
{
    if (!_bEditing) {
        return;
    }
    _bEditing = false;
    try {
        setValue(std::stof(_editBuffer));
    }
    catch (...) {
        // Invalid text: keep the previous value.
    }
    _editBuffer.clear();
}

void UISpinBox::cancelEdit()
{
    _bEditing = false;
    _editBuffer.clear();
}

void UISpinBox::onFocusLost()
{
    commitEdit();
}

int UISpinBox::zoneFromPointer(float localX) const
{
    const float zoneWidth = 26.0f;
    if (localX < zoneWidth) {
        return 0; // minus
    }
    if (localX > _layoutRect.extent.x - zoneWidth) {
        return 1; // plus
    }
    return -1;
}

void UISpinBox::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _backgroundColor, nullptr);
    builder.addRectOutline(_layoutRect, _borderColor, 1.0f);
    const float zoneWidth = 26.0f;
    const Rect2D minusRect{.pos = _layoutRect.pos, .extent = {zoneWidth, _layoutRect.extent.y}};
    const Rect2D plusRect{.pos = {_layoutRect.pos.x + _layoutRect.extent.x - zoneWidth, _layoutRect.pos.y},
                          .extent = {zoneWidth, _layoutRect.extent.y}};
    builder.addSprite(minusRect, _hoveredZone == 0 ? _buttonHoverColor : _buttonColor, nullptr);
    builder.addSprite(plusRect, _hoveredZone == 1 ? _buttonHoverColor : _buttonColor, nullptr);

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }
    builder.addText(minusRect, "-", _textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    builder.addText(plusRect, "+", _textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    const std::string shown = _bEditing ? _editBuffer : std::format("{:.2f}", _value);
    builder.addText(_layoutRect, shown, _textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
    if (_bEditing) {
        const float textW  = font->measureText(shown);
        const float caretX = _layoutRect.pos.x + (_layoutRect.extent.x + textW) * 0.5f + 1.0f;
        const float caretY = _layoutRect.pos.y + (_layoutRect.extent.y - font->lineHeight) * 0.5f;
        builder.addSprite(Rect2D{.pos = {caretX, caretY}, .extent = {1.0f, font->lineHeight}},
                          _textColor, nullptr);
    }
}

bool UISpinBox::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (_bEditing) {
        if (eventType == EEvent::KeyTyped) {
            _editBuffer += static_cast<const KeyTypedEvent&>(event).getText();
            invalidateProperty(EUIPropertyImpact::Paint);
            return true;
        }
        if (eventType == EEvent::KeyPressed) {
            const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Backspace) {
                if (!_editBuffer.empty()) {
                    _editBuffer.pop_back();
                    invalidateProperty(EUIPropertyImpact::Paint);
                }
                return true;
            }
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Enter) {
                commitEdit();
                return true;
            }
            if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Escape) {
                cancelEdit();
                return true;
            }
        }
        return false;
    }

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Left) {
            stepBy(-1.0f);
            return true;
        }
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Right) {
            stepBy(1.0f);
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseMoved) {
        const bool bInside = hitTestLayoutRect(ctx.logicalPoint);
        const int  zone    = bInside ? zoneFromPointer(ctx.logicalPoint.x - _layoutRect.pos.x) : -1;
        if (zone != _hoveredZone) {
            _hoveredZone = zone;
            markPaintDirty();
        }
        return bInside;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        // Double-click (no event timestamps: a press near the previous press)
        // enters text edit mode; a single press steps the pressed zone.
        const bool bDouble = _bHasLastPress && glm::length(ctx.logicalPoint - _lastPressPos) < 6.0f;
        _lastPressPos  = ctx.logicalPoint;
        _bHasLastPress = true;
        if (bDouble) {
            _bHasLastPress = false;
            beginEdit();
            return true;
        }
        const int zone = zoneFromPointer(ctx.logicalPoint.x - _layoutRect.pos.x);
        if (zone == 0) {
            stepBy(-1.0f);
            return true;
        }
        if (zone == 1) {
            stepBy(1.0f);
            return true;
        }
        return false;
    }

    return false;
}

// === UIRadioButton ===

void UIRadioButton::paintSelf(UIFrameBuilder& builder)
{
    if (_bHovered) {
        builder.addSprite(_layoutRect, _hoveredColor, nullptr);
    }

    const float dotSize = 14.0f;
    const Rect2D dotRect{
        .pos    = {_layoutRect.pos.x + 4.0f, _layoutRect.pos.y + (_layoutRect.extent.y - dotSize) * 0.5f},
        .extent = {dotSize, dotSize},
    };
    builder.addSprite(dotRect, _dotColor, nullptr);
    if (_bChecked) {
        const float inset = 4.0f;
        builder.addSprite(Rect2D{.pos = dotRect.pos + glm::vec2(inset), .extent = dotRect.extent - glm::vec2(inset * 2.0f)},
                          _dotFillColor, nullptr);
    }

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font && !_label.empty()) {
        const Rect2D labelRect{
            .pos    = {_layoutRect.pos.x + 24.0f, _layoutRect.pos.y},
            .extent = {_layoutRect.extent.x - 24.0f, _layoutRect.extent.y},
        };
        builder.addText(labelRect, _label, _textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
    }
}

bool UIRadioButton::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && (keyEvent._keyCode == EKey::Enter || keyEvent._keyCode == EKey::Space)) {
            if (_onSelect) {
                _onSelect(this);
            }
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        if (_onSelect) {
            _onSelect(this);
        }
        return true;
    }

    return false;
}

// === UIColorEdit ===

Rect2D UIColorEdit::swatchRect() const
{
    return Rect2D{
        .pos    = {_layoutRect.pos.x + 4.0f, _layoutRect.pos.y + 4.0f},
        .extent = {_swatchSize, _layoutRect.extent.y - 8.0f},
    };
}

void UIColorEdit::setColor(const glm::vec4& value)
{
    if (_color == value) {
        return;
    }
    _color = value;
    invalidateProperty(EUIPropertyImpact::Paint);
    if (_onColorChanged) {
        _onColorChanged(_color);
    }
}

void UIColorEdit::adjustActiveChannel(float delta)
{
    glm::vec4 next = _color;
    next[_activeChannel] = clampValue(next[_activeChannel] + delta, 0.0f, 1.0f);
    setColor(next);
}

void UIColorEdit::paintSelf(UIFrameBuilder& builder)
{
    builder.addSprite(_layoutRect, _backgroundColor, nullptr);
    builder.addSprite(swatchRect(), _color, nullptr);

    // Channel strip: four cells, the active one highlighted.
    const float stripX = swatchRect().pos.x + _swatchSize + 8.0f;
    const float cellW  = 26.0f;
    const float cellH  = 12.0f;
    const float cellY  = _layoutRect.pos.y + (_layoutRect.extent.y - cellH) * 0.5f;
    static const char* kNames[4] = {"R", "G", "B", "A"};
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 11);
    for (int ch = 0; ch < 4; ++ch) {
        const Rect2D cell{
            .pos    = {stripX + static_cast<float>(ch) * (cellW + 2.0f), cellY},
            .extent = {cellW, cellH},
        };
        if (ch == _activeChannel) {
            builder.addSprite(cell, _channelHighlight, nullptr);
        }
        else {
            builder.addSprite(cell, _color * glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), nullptr);
        }
        if (font) {
            builder.addText(cell, kNames[ch], _textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
        }
    }
}

bool UIColorEdit::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    const EEvent::T eventType = event.getEventType();

    if (eventType == EEvent::KeyPressed) {
        const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Left) {
            adjustActiveChannel(-0.05f);
            return true;
        }
        if (!keyEvent.bRepeat && keyEvent._keyCode == EKey::Right) {
            adjustActiveChannel(0.05f);
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseButtonPressed) {
        // Channel strip click: select the active channel (no drag).
        const float stripX = swatchRect().pos.x + _swatchSize + 8.0f;
        const float cellW  = 26.0f;
        const float cellH  = 12.0f;
        const float cellY  = _layoutRect.pos.y + (_layoutRect.extent.y - cellH) * 0.5f;
        for (int ch = 0; ch < 4; ++ch) {
            const Rect2D cell{
                .pos    = {stripX + static_cast<float>(ch) * (cellW + 2.0f), cellY},
                .extent = {cellW, cellH},
            };
            const bool bInside = ctx.logicalPoint.x >= cell.pos.x &&
                                 ctx.logicalPoint.x <= cell.pos.x + cell.extent.x &&
                                 ctx.logicalPoint.y >= cell.pos.y &&
                                 ctx.logicalPoint.y <= cell.pos.y + cell.extent.y;
            if (bInside) {
                if (_activeChannel != ch) {
                    _activeChannel = ch;
                    markPaintDirty();
                }
                return true;
            }
        }
        // Swatch or anywhere else in the control: drag adjusts the active channel.
        if (hitTestLayoutRect(ctx.logicalPoint)) {
            _bDragging = true;
            _dragStart = ctx.logicalPoint;
            if (WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        }
        return false;
    }

    if (eventType == EEvent::MouseMoved) {
        if (_bDragging) {
            adjustActiveChannel((ctx.logicalPoint.x - _dragStart.x) * 0.01f);
            _dragStart = ctx.logicalPoint;
        }
        return true;
    }

    if (eventType == EEvent::MouseButtonReleased) {
        if (_bDragging) {
            _bDragging = false;
            if (WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
        }
        return true;
    }

    return false;
}

// === UISearchComboBox ===

std::vector<int> UISearchComboBox::filteredIndices() const
{
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(_items.size()); ++i) {
        if (_filter.empty() || _items[i].find(_filter) != std::string::npos) {
            indices.push_back(i);
        }
    }
    return indices;
}

void UISearchComboBox::openFilteredMenu()
{
    closeMenu();
    const auto indices = filteredIndices();
    if (indices.empty()) {
        return;
    }
    std::vector<UIMenu::FItem> entries;
    entries.reserve(indices.size());
    for (int index : indices) {
        entries.push_back({_items[index], [this, index]
                           {
                               _selectedIndex = index;
                               invalidateProperty(EUIPropertyImpact::Paint);
                               if (_onSelectionChanged) {
                                   _onSelectionChanged(index);
                               }
                               closeMenu();
                           }});
    }
    auto menu = UIMenu::create(std::move(entries));
    menu->_onDismiss = [this]() { _openMenu.reset(); };
    _openMenu = menu;
    if (WidgetTree* tree = getTree()) {
        menu->openAt(*tree, {_layoutRect.pos.x, _layoutRect.pos.y + _layoutRect.extent.y});
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
    builder.addSprite(_layoutRect, _bHovered ? _hoveredColor : _backgroundColor, nullptr);
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }
    const std::string shown = _bFocused ? (_filter.empty() ? "(type to filter)" : _filter)
                                        : currentLabel();
    builder.addText(_layoutRect, shown, _textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
    if (_bFocused) {
        const float caretX = _layoutRect.pos.x + 4.0f + font->measureText(shown);
        const float caretY = _layoutRect.pos.y + (_layoutRect.extent.y - font->lineHeight) * 0.5f;
        builder.addSprite(Rect2D{.pos = {caretX, caretY}, .extent = {1.0f, font->lineHeight}},
                          _caretColor, nullptr);
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

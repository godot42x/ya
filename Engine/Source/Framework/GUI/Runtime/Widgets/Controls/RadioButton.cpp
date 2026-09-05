#include "GUI/Widgets/Controls/RadioButton.h"

#include "Core/KeyCode.h"
#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

namespace ya
{

void UIRadioButton::paintSelf(UIFrameBuilder& builder)
{
    const FRadioButtonStyle& style = resolvedStyle();
    if (_bHovered) {
        builder.addBrush(_layoutRect, style.hoveredFill);
    }

    const float dotSize = 14.0f;
    const Rect2D dotRect{
        .pos    = {_layoutRect.pos.x + 4.0f, _layoutRect.pos.y + (_layoutRect.extent.y - dotSize) * 0.5f},
        .extent = {dotSize, dotSize},
    };
    builder.addSprite(dotRect, style.dotColor, nullptr);
    if (_bChecked) {
        const float inset = 4.0f;
        builder.addSprite(Rect2D{.pos = dotRect.pos + glm::vec2(inset), .extent = dotRect.extent - glm::vec2(inset * 2.0f)},
                          style.dotFillColor, nullptr);
    }

    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (font && !_label.empty()) {
        const Rect2D labelRect{
            .pos    = {_layoutRect.pos.x + 24.0f, _layoutRect.pos.y},
            .extent = {_layoutRect.extent.x - 24.0f, _layoutRect.extent.y},
        };
        builder.addText(labelRect, _label, style.textColor, font, EWidgetAlignH::Left, EWidgetAlignV::Center);
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

} // namespace ya

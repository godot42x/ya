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
    // The dot is a circle whose radius is derived from its own size, so the
    // control computes it (a theme cannot know the widget's dot size); the fill
    // and edge come from the style brush.
    FBrush ring       = style.dotColor;
    ring.cornerRadius = dotRect.extent.y * 0.5f;
    builder.addBrush(dotRect, ring);
    if (_bChecked) {
        const float inset = 4.0f;
        const Rect2D core{.pos = dotRect.pos + glm::vec2(inset),
                          .extent = dotRect.extent - glm::vec2(inset * 2.0f)};
        builder.addBrush(core, FBrush::solid(style.dotFillColor, core.extent.y * 0.5f));
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

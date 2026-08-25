#include "GUI/Declarative/DeclarativeNodeAdapter.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"

namespace ya::ui
{

UIElementRef UIDeclarativeNodeAdapter::create(const UIDescription& node)
{
    const std::string name = node.common.displayName.value_or(
        node.displayName.empty() ? node.key : node.displayName);
    switch (node.kind) {
    case EWidgetKind::Column:
    case EWidgetKind::Row:
        return std::make_shared<UIContainer>(name);
    case EWidgetKind::Panel:
        return std::make_shared<UIPanel>(name);
    case EWidgetKind::Text:
        return std::make_shared<UIText>(name);
    case EWidgetKind::Button:
        return std::make_shared<UIButton>(name);
    case EWidgetKind::TextField:
        return std::make_shared<UITextField>(name);
    }
    return nullptr;
}

void UIDeclarativeNodeAdapter::apply(UIElement& widget, const UIDescription& node)
{
    widget._stableKey = node.common.key.value_or(!node.key.empty() ? node.key : node.displayName);
    widget._name = node.common.displayName.value_or(
        node.displayName.empty() ? widget._stableKey : node.displayName);

    if (node.common.position) {
        widget.setPosition(*node.common.position);
    }
    else if (node._bHasPosition) {
        widget.setPosition(node._position);
    }
    if (node.common.size) {
        widget.setSize(*node.common.size);
    }
    else if (node._bHasSize) {
        widget.setSize(node._size);
    }
    if (node.common.enabled) {
        widget.setEnabled(*node.common.enabled);
    }
    else if (node._bHasEnabled) {
        widget.setEnabled(node._bEnabled);
    }
    if (node.common.focusPolicy) {
        widget._focusPolicy = *node.common.focusPolicy;
    }
    else if (node._bHasFocusPolicy) {
        widget._focusPolicy = node._focusPolicy;
    }

    if (auto* container = dynamic_cast<UIContainer*>(&widget)) {
        container->setDirection(node.kind == EWidgetKind::Row ? EWidgetBoxLayout::Horizontal : EWidgetBoxLayout::Vertical);
        if (node._bHasSpacing) {
            container->setSpacing(node._spacing);
        }
        if (node._bHasPadding) {
            container->setPadding(node._padding);
        }
        if (node._bHasClipChildren) {
            container->setClipChildren(node._bClipChildren);
        }
        if (node._bHasStretchLastChild) {
            container->setStretchLastChild(node._bStretchLastChild);
        }
    }

    if (auto* panel = dynamic_cast<UIPanel*>(&widget); panel && node._bHasColor) {
        if (node._panel && node._panel->color) {
            panel->setColor(*node._panel->color);
        }
        else {
            panel->setColor(node._color);
        }
    }

    if (auto* panel = dynamic_cast<UIPanel*>(&widget); panel && node._panel && node._panel->cornerRadius) {
        panel->setCornerRadius(*node._panel->cornerRadius);
    }

    if (auto* text = dynamic_cast<UIText*>(&widget)) {
        const auto& payload = node._textDescription;
        if (payload && payload->color) {
            text->_color = *payload->color;
        }
        else if (node._bHasColor) {
            text->_color = node._color;
        }
        if (payload && payload->fontSize) {
            text->_fontSize = *payload->fontSize;
        }
        else if (node._bHasFontSize) {
            text->_fontSize = node._fontSize;
        }
        if (payload && payload->text) {
            text->setText(*payload->text);
        }
        else if (node._bHasText) {
            text->setText(node._text);
        }
    }

    if (auto* button = dynamic_cast<UIButton*>(&widget)) {
        const auto& payload = node._button;
        if (payload && payload->onClick) {
            button->_onClick = *payload->onClick;
        }
        else if (node._bHasOnClick) {
            button->_onClick = node._onClick;
        }
    }

    if (auto* textField = dynamic_cast<UITextField*>(&widget)) {
        const auto& payload = node._textField;
        if (payload && payload->fontSize) {
            textField->_fontSize = *payload->fontSize;
        }
        else if (node._bHasFontSize) {
            textField->_fontSize = node._fontSize;
        }
        if (payload && payload->text) {
            textField->setText(*payload->text);
        }
        else if (node._bHasText) {
            textField->setText(node._text);
        }
    }
}

bool UIDeclarativeNodeAdapter::sameKind(const UIElement& widget, EWidgetKind kind)
{
    switch (kind) {
    case EWidgetKind::Column:
    case EWidgetKind::Row: return dynamic_cast<const UIContainer*>(&widget) != nullptr;
    case EWidgetKind::Panel: return dynamic_cast<const UIPanel*>(&widget) != nullptr;
    case EWidgetKind::Text: return dynamic_cast<const UIText*>(&widget) != nullptr;
    case EWidgetKind::Button: return dynamic_cast<const UIButton*>(&widget) != nullptr;
    case EWidgetKind::TextField: return dynamic_cast<const UITextField*>(&widget) != nullptr;
    }
    return false;
}

const char* UIDeclarativeNodeAdapter::kindName(EWidgetKind kind)
{
    switch (kind) {
    case EWidgetKind::Column: return "Column";
    case EWidgetKind::Row: return "Row";
    case EWidgetKind::Panel: return "Panel";
    case EWidgetKind::Text: return "Text";
    case EWidgetKind::Button: return "Button";
    case EWidgetKind::TextField: return "TextField";
    }
    return "Unknown";
}

} // namespace ya::ui

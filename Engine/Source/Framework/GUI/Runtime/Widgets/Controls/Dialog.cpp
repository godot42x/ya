#include "GUI/Widgets/Controls/Dialog.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"

namespace ya
{

std::shared_ptr<UIDialog> UIDialog::create(std::string title, std::shared_ptr<UIElement> content)
{
    auto dialog = std::make_shared<UIDialog>("Dialog");
    dialog->_bModal     = true; // dimming shield + focus ownership + Esc
    dialog->_contentPos = {0.0f, 0.0f}; // centered by the layout below

    // Panel: title bar + content + button row. UIPanel does not aggregate
    // child desired sizes, so measure the content by hand.
    const float titleH   = 18.0f;
    const float buttonH  = 26.0f;
    const float contentH = content ? std::max(content->computeDesiredSize().y, 0.0f) : 0.0f;
    const float panelH   = 14.0f + titleH + 12.0f + contentH + 12.0f + buttonH + 14.0f;
    auto panel = std::make_shared<UIPanel>("DialogPanel");
    panel->setSize({360.0f, panelH});
    panel->setColor({0.16f, 0.18f, 0.22f, 1.0f});

    auto stack = std::make_shared<UIContainer>("DialogStack");
    stack->_anchorMin = {0.0f, 0.0f};
    stack->_anchorMax = {1.0f, 1.0f};
    stack->setSize({0.0f, 0.0f});
    stack->setDirection(EWidgetBoxLayout::Vertical);
    stack->setSpacing(12.0f);
    stack->setPadding({16.0f, 14.0f});
    panel->addDetachedChild(stack);

    auto titleText = std::make_shared<UIText>("DialogTitle");
    titleText->_bAutoSize = true;
    titleText->_fontSize  = 14;
    titleText->_color     = {0.88f, 0.90f, 0.94f, 1.0f};
    titleText->setText(std::move(title));
    stack->addDetachedChild(titleText);

    if (content) {
        stack->addDetachedChild(content);
    }

    auto buttons = std::make_shared<UIContainer>("DialogButtons");
    buttons->setDirection(EWidgetBoxLayout::Horizontal);
    buttons->setSpacing(8.0f);
    buttons->getBoxLayout().setMainAxisAlignment(EWidgetMainAxisAlignment::End);
    stack->addDetachedChild(buttons);

    const auto makeButton = [](const std::string& name, const std::string& label)
    {
        auto button = std::make_shared<UIButton>(name);
        button->_bAutoSize = true;
        button->setContentPadding({12.0f, 4.0f});
        button->_normalColor  = {0.20f, 0.22f, 0.27f, 1.0f};
        button->_hoveredColor = {0.27f, 0.30f, 0.37f, 1.0f};
        button->_pressedColor = {0.14f, 0.16f, 0.20f, 1.0f};
        auto text = std::make_shared<UIText>(name + "_Label");
        text->_bAutoSize = true;
        text->_fontSize  = 13;
        text->_color     = {0.92f, 0.94f, 0.97f, 1.0f};
        text->_hAlign    = EWidgetAlignH::Center;
        text->_vAlign    = EWidgetAlignV::Center;
        text->setText(label);
        button->addDetachedChild(text);
        return button;
    };

    auto okButton = makeButton("DialogOK", "OK");
    okButton->_onClick = [dialog]() { dialog->closeWithResult(true); };
    buttons->addDetachedChild(okButton);

    auto cancelButton = makeButton("DialogCancel", "Cancel");
    cancelButton->_onClick = [dialog]() { dialog->closeWithResult(false); };
    buttons->addDetachedChild(cancelButton);

    dialog->addDetachedChild(panel);

    // Esc / shield click: report a cancel through the same callback. The
    // lambda is moved and run inside close(), so capturing the raw pointer
    // is safe (no ownership cycle).
    dialog->_onDismiss = [dialogRaw = dialog.get()]()
    {
        if (dialogRaw->_onClosed) {
            dialogRaw->_onClosed(false);
        }
    };
    return dialog;
}

void UIDialog::layoutAssigned(const Rect2D& rect)
{
    setLayoutRect(rect); // full screen (shield)

    for (UIElement* child : getChildrenInPaintOrder()) {
        if (!child->participatesInLayout()) {
            continue;
        }
        const glm::vec2 desired = child->computeDesiredSize();
        const glm::vec2 pos     = rect.pos + (rect.extent - desired) * 0.5f;
        child->layoutAssigned(Rect2D{.pos = pos, .extent = desired});
        break;
    }
}

void UIDialog::closeWithResult(bool bConfirmed)
{
    auto onClosed = _onClosed;
    _onClosed     = nullptr;
    close();
    if (onClosed) {
        onClosed(bConfirmed);
    }
}

} // namespace ya

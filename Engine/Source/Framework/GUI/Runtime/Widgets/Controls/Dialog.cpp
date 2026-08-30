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
    dialog->_bModal = true; // dimming shield + focus ownership + Esc

    // Panel: title bar + content + button row. UIPanel does not aggregate
    // child desired sizes, so measure the content by hand.
    const float titleH   = 18.0f;
    const float buttonH  = 26.0f;
    const float contentH = content
                               ? std::max(content->hasAuthoredSize() ? content->getSize().y
                                                                     : content->computeDesiredSize().y,
                                          0.0f)
                               : 0.0f;
    const float panelH   = 14.0f + titleH + 12.0f + contentH + 12.0f + buttonH + 14.0f;
    auto panel = std::make_shared<UIPanel>("DialogPanel");
    panel->setStyleKey("panel");
    dialog->_contentExtent = {360.0f, panelH};

    auto stack = std::make_shared<UIContainer>("DialogStack");
    stack->setDirection(EWidgetBoxLayout::Vertical);
    stack->setSpacing(12.0f);
    stack->setPadding({16.0f, 14.0f});
    panel->addDetachedChild(stack);
    // The panel is a canvas host: fill is expressed on the parent->child slot
    // edge, not by authoring anchors on the child.
    if (auto* slot = dynamic_cast<UICanvasSlot*>(panel->getSlotForChild(*stack))) {
        FCanvasSlotArgs fillArgs;
        fillArgs.anchorMin = {0.0f, 0.0f};
        fillArgs.anchorMax = {1.0f, 1.0f};
        slot->apply(fillArgs);
    }

    auto titleText = std::make_shared<UIText>("DialogTitle");
    titleText->_bAutoSize = true;
    titleText->_fontSize  = 14;
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
        // Dialog buttons resolve the "button"/"text" style keys from the
        // mounted tree theme (style-system Phase 3 cleanup: no bare fields).
        auto text = std::make_shared<UIText>(name + "_Label");
        text->_bAutoSize = true;
        text->_fontSize  = 13;
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

FCanvasSlotArgs UIDialog::resolveContentSlotArgs(const UIElement& child) const
{
    (void)child;
    FCanvasSlotArgs args;
    args.anchorMin      = {0.5f, 0.5f};
    args.anchorMax      = {0.5f, 0.5f};
    args.pivot          = {0.5f, 0.5f};
    args.widthSizeMode  = EWidgetSizeMode::Auto;
    args.heightSizeMode = EWidgetSizeMode::Auto;
    args.preferredSize   = _contentExtent;
    return args;
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

#include "GUI/Widgets/Controls/Dialog.h"

#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Brush.h"

#include <algorithm>

namespace ya
{

std::shared_ptr<UIDialog> UIDialog::create(std::string title, std::shared_ptr<UIElement> content)
{
    auto dialog = std::make_shared<UIDialog>("Dialog");
    dialog->_bModal = true;

    // Display: title bar + content + button row. Measure the content from
    // its parent-owned content slot after attaching it to the stack.
    const float kDialogW = 360.0f;
    const float titleH   = 18.0f;
    const float buttonH  = 26.0f;
    auto panel = std::make_shared<UIBorder>("DialogPanel");
    panel->setStyleKey("panel");
    panel->setStyleField("fillColor",
                         FBrush::solid({0.14f, 0.15f, 0.19f, 1.0f},
                                       8.0f,
                                       {0.48f, 0.52f, 0.60f, 1.0f}));

    auto stack = std::make_shared<UIContainer>("DialogStack");
    stack->setDirection(EWidgetBoxLayout::Vertical);
    stack->setSpacing(12.0f);
    stack->setPadding({16.0f, 14.0f});
    panel->addDetachedChild(stack);

    auto titleText = std::make_shared<UIText>("DialogTitle");
    titleText->_fontSize  = 14;
    titleText->setText(std::move(title));
    stack->addDetachedChild(titleText);

    if (content) {
        if (auto* text = dynamic_cast<UIText*>(content.get())) {
            text->_bWrap = true;
            if (text->_maxWrapWidth <= 0.0f) {
                text->_maxWrapWidth = kDialogW - 32.0f;
            }
        }
        stack->addDetachedChild(content);
    }

    float contentH = 0.0f;
    if (content) {
        if (const UISlot* edge = stack->getSlotForChild(*content); edge && edge->as<UIBoxSlot>()) {
            const auto* contentSlot = edge->as<UIBoxSlot>();
            contentH = contentSlot->getPreferredSize().y;
        }
        if (contentH <= 0.0f) {
            contentH = content->computeDesiredSize().y;
        }
    }
    const float panelH = 14.0f + titleH + 12.0f + std::max(contentH, 0.0f) + 12.0f + buttonH + 14.0f;
    dialog->_contentExtent = {kDialogW, panelH};

    auto buttons = std::make_shared<UIContainer>("DialogButtons");
    buttons->setDirection(EWidgetBoxLayout::Horizontal);
    buttons->setSpacing(8.0f);
    buttons->getBoxLayout().setMainAxisAlignment(EWidgetMainAxisAlignment::End);
    stack->addDetachedChild(buttons);

    const auto makeButton = [](const std::string& name, const std::string& label)
    {
        auto button = std::make_shared<UIButton>(name);
        button->setContentPadding({12.0f, 4.0f});
        // Dialog buttons resolve the "button"/"text" style keys from the
        // mounted tree theme (style-system Phase 3 cleanup: no bare fields).
        auto text = std::make_shared<UIText>(name + "_Label");
        text->_fontSize  = 13;
        text->_hAlign    = EWidgetAlignH::Center;
        text->_vAlign    = EWidgetAlignV::Center;
        text->setText(label);
        button->addDetachedChild(text);
        return button;
    };

    auto okButton = makeButton("DialogOK", "OK");
    okButton->onClicked.addLambda([dialog]() { dialog->closeWithResult(true); });
    buttons->addDetachedChild(okButton);

    auto cancelButton = makeButton("DialogCancel", "Cancel");
    cancelButton->onClicked.addLambda([dialog]() { dialog->closeWithResult(false); });
    buttons->addDetachedChild(cancelButton);

    dialog->addDetachedChild(panel);

    // Esc: report a cancel through the same callback. The lambda is moved
    // and run inside close(), so capturing the raw pointer is safe.
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

#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Dialog.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>

namespace guiworkbench
{

void buildMenusDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
    };

    auto popupBtn = ya::ui::button("PopupButton")
                        .child(ya::ui::text("PopupButton_Label")
                                   .setText("Open popup menu...")
                                   .setFontSize(13)
                                   .setHAlign(ya::EWidgetAlignH::Center)
                                   .setVAlign(ya::EWidgetAlignV::Center));
    auto popupBtnRef = popupBtn.share();
    popupBtn.setOnClick([popupBtnRef, &tree, &state, log]
                        {
                            auto menu = ya::UIMenu::create({
                                ya::UIMenu::FItem{.label = "New Document",
                                                 .action = [&state, log]
                                                 {
                                                     state.menuLog = "Menu: New Document";
                                                     log(state.menuLog);
                                                 }},
                                ya::UIMenu::FItem{.label = "Open File...",
                                                 .action = [&state, log]
                                                 {
                                                     state.menuLog = "Menu: Open File...";
                                                     log(state.menuLog);
                                                 }},
                                ya::UIMenu::FItem{.label = "Save",
                                                 .action = [&state, log]
                                                 {
                                                     state.menuLog = "Menu: Save";
                                                     log(state.menuLog);
                                                 }},
                                ya::UIMenu::FItem::separator(),
                                ya::UIMenu::FItem{.label = "Quit",
                                                 .action = [&state, log]
                                                 {
                                                     state.menuLog = "Menu: Quit";
                                                     log(state.menuLog);
                                                 }},
                            });
                            const auto& rect = popupBtnRef->_layoutRect;
                            menu->openAt(tree, {rect.pos.x, rect.pos.y + rect.extent.y});
                        });

    auto form = ya::ui::column("MenusForm")
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .child(header("MenusTitle",
                               "Menus & popups — the menu bar above opens popup menus"))
                    .child(body("MenusHint", "Click a menu-bar entry, hover to switch, Esc or outside click closes."))
                    .child(std::move(popupBtn), ya::ui::boxSlot().preferredSize({180.0f, 26.0f}))
                    .child(body("MenusKeys", "Keyboard: Up/Down move, Enter activates, Esc closes."));
    auto page = ya::ui::border("MenusDemo").setStyleKey(std::string(ya::StyleKey::Panel)).child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());
}

void buildDialogDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
    };
    auto demoButton = [](std::string name, const std::string& label)
    {
        auto button = ya::ui::button(name).child(
            ya::ui::text(name + "_Label")
                .setText(label)
                .setFontSize(13)
                .setHAlign(ya::EWidgetAlignH::Center)
                .setVAlign(ya::EWidgetAlignV::Center));
        return std::move(button).setContentPadding({12.0f, 4.0f});
    };

    auto openModal = ya::ui::button("OpenModal")
                         .child(ya::ui::text("OpenModal_Label")
                                    .setText("Open modeless dialog...")
                                    .setFontSize(13)
                                    .setHAlign(ya::EWidgetAlignH::Center)
                                    .setVAlign(ya::EWidgetAlignV::Center))
                         .setOnClick([&tree, &state, log]
                         {
                             if (state.bModalOpen) {
                                 return;
                             }
                             state.bModalOpen = true;

                             auto overlay         = std::make_shared<ya::UIPopupOverlay>("ModalOverlay");
                             overlay->_bModal     = false;
                             overlay->_contentPos = {440.0f, 300.0f};

                             auto dialog = std::make_shared<ya::UIBorder>("ModalDialog");
                             dialog->setStyleKey("panel");
                             overlay->addDetachedChild(dialog, [](ya::UIElement&, ya::UISlot& edge) {
                                 if (auto* slot = edge.as<ya::UICanvasSlot>()) {
                                     slot->setFixedSize({360.0f, 170.0f});
                                     slot->setWidthSizeMode(ya::EWidgetSizeMode::Fixed);
                                     slot->setHeightSizeMode(ya::EWidgetSizeMode::Fixed);
                                 }
                             });

                             auto stack = std::make_shared<ya::UIContainer>("ModalStack");
                             stack->setPadding({16.0f, 14.0f});
                             stack->setDirection(ya::EWidgetBoxLayout::Vertical);
                             stack->setSpacing(12.0f);
                             stack->setClipChildren(true);
                            dialog->addDetachedChild(stack);
                            ya::ui::resetSlot(*dialog, *stack, ya::ui::contentSlot().fill());

                             auto title = makeLabel("About / New Project", 14.0f);
                             stack->addDetachedChild(title);

                             auto nameField       = std::make_shared<ya::UITextField>("ModalName");
                             nameField->_fontSize = 13;
                             nameField->setText(state.modalName);
                             stack->addDetachedChild(nameField, [](ya::UIElement&, ya::UISlot& edge) {
                                 if (auto* slot = edge.as<ya::UIBoxSlot>()) {
                                     slot->setPreferredSize({320.0f, 26.0f});
                                 }
                             });
                             if (auto* slot = stack->getBoxSlot(*nameField)) {
                                 slot->setCrossAlignment(ya::EUIBoxSlotCrossAlignment::Start);
                             }

                             auto buttons = std::make_shared<ya::UIContainer>("ModalButtons");
                             buttons->setDirection(ya::EWidgetBoxLayout::Horizontal);
                             buttons->setSpacing(8.0f);
                             stack->addDetachedChild(buttons);

                             auto okButton      = makeDemoButton("ModalOK", "OK", 80.0f);
                             okButton->onClicked.addLambda([&state, overlay, nameField, log]
                             {
                                 state.modalName = nameField->_text;
                                 log(std::format("Modal OK: '{}'", state.modalName));
                                 overlay->close();
                             });
                             buttons->addDetachedChild(okButton, [](ya::UIElement&, ya::UISlot& edge) {
                                 if (auto* slot = edge.as<ya::UIBoxSlot>()) {
                                     slot->setPreferredSize({80.0f, 26.0f});
                                 }
                             });

                             auto cancelButton      = makeDemoButton("ModalCancel", "Cancel", 80.0f);
                             cancelButton->onClicked.addLambda([&state, overlay, log]
                             {
                                 log("Modal cancelled");
                                 overlay->close();
                             });
                             buttons->addDetachedChild(cancelButton, [](ya::UIElement&, ya::UISlot& edge) {
                                 if (auto* slot = edge.as<ya::UIBoxSlot>()) {
                                     slot->setPreferredSize({80.0f, 26.0f});
                                 }
                             });

                             overlay->_onDismiss = [&state]() { state.bModalOpen = false; };
                             overlay->open(tree);
                         });
    state.openModalButton = openModal.share();

    auto form = ya::ui::column("DialogForm")
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(12.0f)
                    .child(header("ModalTitle",
                               "Modeless dialog — outside click / Esc dismisses; no dimming"))
                    .child(std::move(openModal), ya::ui::boxSlot().preferredSize({220.0f, 26.0f}))
                    .child(body("ModalHint", "Modeless: click outside or Esc closes. Modal (below) keeps input until OK/Cancel/Esc."))
                    .child(header("InteractionsTooltipHeader", "Tooltip"))
                    .child(ya::ui::row("InteractionsTipRow")
                               .child(demoButton("TooltipBtn", "Hover me (tooltip)")
                                          .setTooltip("This tooltip appears after a 0.5s hover dwell."),
                                      ya::ui::boxSlot().preferredSize({200.0f, 26.0f})))
                    .child(header("InteractionsDialogHeader", "Modal dialog (UIDialog) — input exclusive"))
                    .child(demoButton("OpenDialogBtn", "Open modal...")
                               .setOnClick(
                                   [&tree, log]
                                   {
                                       auto content = ya::ui::text("DialogContent")
                                                          .setText("This is a modal dialog. Mouse and keyboard cannot "
                                                                   "reach widgets behind it until OK, Cancel, or Esc. "
                                                                   "Clicking outside does not close it. The overlay paints "
                                                                   "no dim — stack a fill Border/Image if you want one.")
                                                          .setFontSize(13)
                                                          .setColor({0.88f, 0.90f, 0.94f, 1.0f})
                                                          .setWrap(true)
                                                          .setMaxWrapWidth(380.0f)
                                                          .release();
                                       auto dialog       = ya::UIDialog::create("Confirm", std::move(content));
                                       dialog->_onClosed = [log](bool bConfirmed)
                                       {
                                           const std::string result = bConfirmed ? "confirmed" : "cancelled";
                                           log(std::format("Dialog closed: {}", result));
                                       };
                                       dialog->open(tree);
                                   }),
                           ya::ui::boxSlot().preferredSize({180.0f, 26.0f}))
                    .child(demoButton("OpenDimDialogBtn", "Open modal with composed dim...")
                               .setOnClick(
                                   [&tree, log]
                                   {
                                       auto content = ya::ui::text("DimDialogContent")
                                                          .setText("Dim is a HitTestInvisible fill Border stacked under "
                                                                   "the dialog chrome — not a popup flag.")
                                                          .setFontSize(13)
                                                          .setColor({0.88f, 0.90f, 0.94f, 1.0f})
                                                          .setWrap(true)
                                                          .setMaxWrapWidth(380.0f)
                                                          .release();
                                       auto dialog = ya::UIDialog::create("Confirm", std::move(content));
                                       auto dim    = std::make_shared<ya::UIBorder>("ModalDim");
                                       dim->setVisibility(ya::EWidgetVisibility::HitTestInvisible);
                                       dim->setStyleField("fillColor",
                                                          ya::FBrush::solid({0.0f, 0.0f, 0.0f, 0.45f}));
                                       dim->_zOrder = -1;
                                       dialog->addDetachedChild(dim, [](ya::UIElement&, ya::UISlot& slot)
                                       {
                                           if (auto* canvas = slot.as<ya::UICanvasSlot>()) {
                                               ya::FCanvasSlotArgs fill;
                                               fill.anchorMin = {0.0f, 0.0f};
                                               fill.anchorMax = {1.0f, 1.0f};
                                               canvas->apply(fill);
                                           }
                                       });
                                       dialog->_onClosed = [log](bool bConfirmed)
                                       {
                                           log(std::format("Dimmed dialog closed: {}",
                                                           bConfirmed ? "confirmed" : "cancelled"));
                                       };
                                       dialog->open(tree);
                                   }),
                           ya::ui::boxSlot().preferredSize({260.0f, 26.0f}));
    auto page = ya::ui::border("DialogDemo")
                    .setStyleKey(std::string(ya::StyleKey::Panel))
                    .child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());
}

} // namespace guiworkbench

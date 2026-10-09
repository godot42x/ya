#include "GameEditor/UI/Dialogs/EditorNamePrompt.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Shell/EditorListRows.h"
#include "GameEditor/UI/Shell/EditorTheme.h"

#include <algorithm>
#include <utility>

namespace ya
{

void EditorNamePrompt::resolve(std::string name)
{
    if (_bResolved) {
        return;
    }
    _bResolved = true;
    std::function<void(std::string)> fn = std::move(_request.onConfirm);
    close();
    if (fn && !name.empty()) {
        fn(std::move(name));
    }
}

void EditorNamePrompt::open(WidgetTree& tree, FEditorNamePrompt request)
{
    if (_overlay && _overlay->isAttached()) {
        close();
    }
    reset();
    _request   = std::move(request);
    _bResolved = false;

    auto field = ui::textField("EditorNamePromptField")
                     .setStyleKey(editorStyle(StyleKey::TextField))
                     .setText(_request.initial);
    _field = field.share();
    _field->_onCommit = [this](const std::string&) { confirm(); };

    auto create = labeledButton("EditorNamePromptCreate", "Create")
                      .setOnClick([this]() { confirm(); });
    auto cancel = labeledButton("EditorNamePromptCancel", "Cancel")
                      .setOnClick([this]() { this->cancel(); });

    auto panel = ui::border("EditorNamePromptPanel")
                     .setStyleKey("panel.window")
                     .setPadding(FMargin::all(16.0f))
                     .child(ui::column("EditorNamePromptColumn")
                                .setSpacing(12.0f)
                                .child(ui::text("EditorNamePromptTitle")
                                           .setText(_request.title)
                                           .setStyleKey("text.header"))
                                .child(ui::text("EditorNamePromptLabel")
                                           .setText(_request.label)
                                           .setStyleKey("text.muted"))
                                .child(std::move(field), ui::boxSlot().preferredSize({0.0f, 24.0f}))
                                .child(ui::row("EditorNamePromptActions")
                                           .setSpacing(8.0f)
                                           .child(std::move(create), ui::boxSlot().preferredSize({90.0f, 26.0f}))
                                           .child(std::move(cancel), ui::boxSlot().preferredSize({90.0f, 26.0f}))));
    _panel = panel.share();

    _overlay = ui::popupOverlay("EditorNamePromptOverlay")
                   .setRole(UIPopupOverlay::EOverlayRole::Modal)
                   .setOnDismiss([this]() {
                       if (!_bResolved) {
                           _bResolved = true;
                           reset();
                           return;
                       }
                       reset();
                   })
                   .child(std::move(panel))
                   .share();

    auto tick = std::make_shared<UITickBehavior>();
    tick->onTick = [this](UIElement& owner, float) {
        WidgetTree* ownerTree = owner.getTree();
        if (!ownerTree || !_overlay) {
            return;
        }
        const Extent2D logicalExtent = ownerTree->getLogicalExtent();
        const glm::vec2 extent = {static_cast<float>(logicalExtent.width),
                                  static_cast<float>(logicalExtent.height)};
        const glm::vec2 size = {
            std::min(420.0f, std::max(1.0f, extent.x - 32.0f)),
            std::min(200.0f, std::max(1.0f, extent.y - 32.0f)),
        };
        _overlay->_contentExtent = size;
        _overlay->_contentPos    = {
            std::max(0.0f, (extent.x - size.x) * 0.5f),
            std::max(0.0f, (extent.y - size.y) * 0.5f),
        };
    };
    _overlay->addBehavior(std::move(tick));
    _overlay->open(tree);
    if (WidgetTree* fieldTree = _field ? _field->getTree() : nullptr) {
        fieldTree->setFocus(_field.get());
    }
}

void EditorNamePrompt::close()
{
    if (_overlay) {
        _overlay->close();
    }
}

void EditorNamePrompt::reset()
{
    _overlay.reset();
    _panel.reset();
    _field.reset();
    _request = {};
}

bool EditorNamePrompt::isOpen() const
{
    return _overlay && _overlay->isAttached();
}

void EditorNamePrompt::confirm()
{
    const std::string name = _field ? _field->getText() : std::string{};
    resolve(name);
}

void EditorNamePrompt::cancel()
{
    if (_bResolved) {
        return;
    }
    _bResolved = true;
    close();
}

} // namespace ya

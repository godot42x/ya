#include "GameEditor/UI/Dialogs/EditorConfirmDialog.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Shell/EditorListRows.h"

#include <algorithm>
#include <utility>

namespace ya
{

void EditorConfirmDialog::resolve(std::function<void()> callback)
{
    if (_bResolved) {
        return;
    }
    _bResolved = true;
    std::function<void()> fn = std::move(callback);
    close();
    if (fn) {
        fn();
    }
}

void EditorConfirmDialog::open(WidgetTree& tree, FEditorConfirmRequest request)
{
    if (_overlay && _overlay->isAttached()) {
        close();
    }
    reset();
    _request = std::move(request);
    _bResolved = false;

    auto message = ui::text("EditorConfirmMessage")
                       .setText(_request.message)
                       .setStyleKey("text")
                       .setWrap(true);
    _messageText = message.share();

    auto primary = labeledButton("EditorConfirmPrimary", _request.primaryLabel)
                       .setOnClick([this]() { resolve(_request.onPrimary); });
    auto secondary = labeledButton("EditorConfirmSecondary", _request.secondaryLabel)
                         .setOnClick([this]() { resolve(_request.onSecondary); });
    auto cancel = labeledButton("EditorConfirmCancel", _request.cancelLabel)
                      .setOnClick([this]() { resolve(_request.onCancel); });

    auto panel = ui::border("EditorConfirmPanel")
                     .setStyleKey("panel.window")
                     .setPadding(FMargin::all(16.0f))
                     .child(ui::column("EditorConfirmColumn")
                                .setSpacing(12.0f)
                                .child(ui::text("EditorConfirmTitle")
                                           .setText(_request.title)
                                           .setStyleKey("text.header"))
                                .child(std::move(message))
                                .child(ui::row("EditorConfirmActions")
                                           .setSpacing(8.0f)
                                           .child(std::move(primary),
                                                  ui::boxSlot().preferredSize({110.0f, 26.0f}))
                                           .child(std::move(secondary),
                                                  ui::boxSlot().preferredSize({110.0f, 26.0f}))
                                           .child(std::move(cancel),
                                                  ui::boxSlot().preferredSize({90.0f, 26.0f}))));
    _panel = panel.share();

    _overlay = ui::popupOverlay("EditorConfirmOverlay")
                   .setRole(UIPopupOverlay::EOverlayRole::Modal)
                   .setOnDismiss([this]() {
                       if (!_bResolved) {
                           _bResolved = true;
                           std::function<void()> cancelFn = std::move(_request.onCancel);
                           reset();
                           if (cancelFn) {
                               cancelFn();
                           }
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
            std::min(180.0f, std::max(1.0f, extent.y - 32.0f)),
        };
        _overlay->_contentExtent = size;
        _overlay->_contentPos    = {
            std::max(0.0f, (extent.x - size.x) * 0.5f),
            std::max(0.0f, (extent.y - size.y) * 0.5f),
        };
    };
    _overlay->addBehavior(std::move(tick));
    _overlay->open(tree);
}

void EditorConfirmDialog::close()
{
    if (_overlay) {
        _overlay->close();
    }
}

void EditorConfirmDialog::reset()
{
    _overlay.reset();
    _panel.reset();
    _messageText.reset();
    _request = {};
}

bool EditorConfirmDialog::isOpen() const
{
    return _overlay && _overlay->isAttached();
}

void EditorConfirmDialog::choosePrimary()
{
    resolve(_request.onPrimary);
}

void EditorConfirmDialog::chooseSecondary()
{
    resolve(_request.onSecondary);
}

void EditorConfirmDialog::chooseCancel()
{
    resolve(_request.onCancel);
}

} // namespace ya

#pragma once

#include "GUI/Widgets/Controls/PopupOverlay.h"

#include <functional>
#include <memory>
#include <string>

namespace ya
{

/// Modal dialog (editor-parity P6): a thin shell over UIPopupOverlay's Modal
/// role — dimming shield, focus ownership, Esc dismiss, self-detach. Adds a
/// title bar, a content slot and OK/Cancel buttons; the result is reported
/// through _onClosed(bConfirmed).
struct YA_GUI_API UIDialog : public UIPopupOverlay
{
    explicit UIDialog(std::string name = "Dialog") : UIPopupOverlay(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDialog>; }

    /// Build a modal dialog: `title` on top, `content` in the middle, OK /
    /// Cancel at the bottom. The dialog sizes itself around the content.
    static std::shared_ptr<UIDialog> create(std::string title, std::shared_ptr<UIElement> content);

    /// Fired once when the dialog closes: true = OK, false = Cancel / Esc /
    /// shield click. Set before open().
    std::function<void(bool bConfirmed)> _onClosed;

    /// Center the content child on the full-screen overlay rect (overrides
    /// the popup's _contentPos anchoring).
    void layoutAssigned(const Rect2D& rect) override;

  protected:
    /// Dismiss paths (Cancel / Esc / shield) report false through _onClosed.
    void closeWithResult(bool bConfirmed);
};

} // namespace ya

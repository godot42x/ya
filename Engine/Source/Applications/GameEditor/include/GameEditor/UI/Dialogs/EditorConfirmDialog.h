#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ya
{

struct UIBorder;
struct UIPopupOverlay;
struct UIText;
struct WidgetTree;

struct FEditorConfirmRequest
{
    std::string           title          = "Confirm";
    std::string           message;
    std::string           primaryLabel   = "Save";
    std::string           secondaryLabel = "Don't Save";
    std::string           cancelLabel    = "Cancel";
    std::function<void()> onPrimary;
    std::function<void()> onSecondary;
    std::function<void()> onCancel;
};

/// Three-choice modal (Save / Don't Save / Cancel). EditorSurface opens it;
/// the dialog owns the overlay and ticks its own layout.
class EditorConfirmDialog
{
    FEditorConfirmRequest           _request;
    std::shared_ptr<UIPopupOverlay> _overlay;
    std::shared_ptr<UIBorder>       _panel;
    std::shared_ptr<UIText>         _messageText;
    bool                            _bResolved = false;

    void resolve(std::function<void()> callback);

  public:
    void open(WidgetTree& tree, FEditorConfirmRequest request);
    void close();
    void reset();

    [[nodiscard]] bool isOpen() const;
    void choosePrimary();
    void chooseSecondary();
    void chooseCancel();
};

} // namespace ya

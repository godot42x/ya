#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ya
{

struct UIBorder;
struct UIPopupOverlay;
struct UITextField;
struct WidgetTree;

/// One text field and Create / Cancel. EditorSurface opens it.
struct FEditorNamePrompt
{
    std::string                      title = "Name";
    std::string                      label = "Name";
    std::string                      initial;
    std::function<void(std::string)> onConfirm;
};

class EditorNamePrompt
{
    FEditorNamePrompt               _request;
    std::shared_ptr<UIPopupOverlay> _overlay;
    std::shared_ptr<UIBorder>       _panel;
    std::shared_ptr<UITextField>    _field;
    bool                            _bResolved = false;

    void resolve(std::string name);

  public:
    void open(WidgetTree& tree, FEditorNamePrompt request);
    void close();
    void reset();

    [[nodiscard]] bool isOpen() const;
    void confirm();
    void cancel();
};

} // namespace ya

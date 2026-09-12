#pragma once

#include "GameEditor/FileExplorer.h"
#include "GameEditor/UI/EditorFilePicker.h"

#include "GUI/Widgets/KeyedChildReconciler.h"

#include <memory>
#include <string>

namespace ya
{

struct UIButton;
struct UIContainer;
struct UIBorder;
struct UIPopupOverlay;
struct UIText;
struct UITextField;
struct WidgetTree;

/// Retained file/directory/save-as dialog. EditorSurface hosts it but does not
/// own the overlay, explorer, or keyed mount/entry rows.
class EditorFilePickerDialog
{
  public:
    void open(WidgetTree& tree, FEditorFilePickerRequest request);
    void sync(WidgetTree& tree);
    void close();
    void reset();

    [[nodiscard]] bool isOpen() const;
    void selectPath(const std::filesystem::path& path);
    void setSaveAsName(std::string name);
    bool confirm();

  private:
    FEditorFilePickerRequest _request;
    std::shared_ptr<FileExplorer> _explorer;
    std::shared_ptr<UIPopupOverlay> _overlay;
    std::shared_ptr<UIBorder> _panel;
    std::shared_ptr<UIText> _pathText;
    std::shared_ptr<UIText> _previewText;
    std::shared_ptr<UITextField> _nameField;
    std::shared_ptr<UIButton> _confirmButton;
    std::shared_ptr<UIContainer> _mountList;
    std::shared_ptr<UIContainer> _entryList;
    std::unique_ptr<UIKeyedChildReconciler> _mountReconciler;
    std::unique_ptr<UIKeyedChildReconciler> _entryReconciler;
    std::string _fingerprint;
    bool _bRowsDirty = true;

    [[nodiscard]] bool isSaveAs() const { return !_request.saveAsExtension.empty(); }
    void rebuildRows(WidgetTree& tree);
    void selectMount(const std::string& itemId);
    void selectItem(const std::filesystem::path& path);
    void activateItem(const std::filesystem::path& path, bool bIsDirectory);
    [[nodiscard]] std::filesystem::path targetDirectory() const;
    [[nodiscard]] bool canConfirm() const;
    [[nodiscard]] std::string composePickedPath() const;
};
} // namespace ya

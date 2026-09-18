#include "GameEditor/UI/Dialogs/EditorFilePickerDialog.h"

#include "Core/Config/ConfigManager.h"
#include "Core/System/PathUtils.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/UI/Shell/EditorListRows.h"

#include <algorithm>
#include <format>

namespace ya
{

void EditorFilePickerDialog::open(WidgetTree& tree, FEditorFilePickerRequest request)
{
    if (!request.onPicked) {
        return;
    }
    if (_overlay && _overlay->isAttached()) {
        close();
    }
    reset();
    _request = std::move(request);
    if (isSaveAs()) {
        _request.filterMode = FileExplorer::FilterMode::Directories;
        _request.selectionMode = FileExplorer::SelectionMode::Directory;
        if (_request.confirmLabel == "Select") {
            _request.confirmLabel = "Save";
        }
    }

    _explorer = std::make_shared<FileExplorer>();
    _explorer->setConfigScope(_request.configScope);
    if (!_request.mounts.empty()) {
        _explorer->init(_request.mounts, _request.extensions, _request.filterMode, _request.selectionMode);
    }
    else {
        _explorer->initFromVFS();
        _explorer->setExtensions(_request.extensions);
        _explorer->setFilterMode(_request.filterMode);
        _explorer->setSelectionMode(_request.selectionMode);
    }
    _explorer->setLeftPanelWidth(180.0f);

    if (!_request.currentPath.empty()) {
        _explorer->setSelectedPath(path_utils::pathFromUtf8String(_request.currentPath));
    }
    else if (!_request.configScope.empty()) {
        const std::string lastDirectory = ConfigManager::get().getOr<std::string>(
            "editor", _request.configScope + ".lastDirectory", "");
        if (!lastDirectory.empty()) {
            _explorer->setSelectedPath(path_utils::pathFromUtf8String(lastDirectory));
        }
    }

    auto pathText = ui::text("FilePickerPath").setStyleKey("text.muted");
    _pathText = pathText.share();
    auto previewText = ui::text("FilePickerPreview").setStyleKey("text.muted");
    _previewText = previewText.share();
    _mountList = ui::column("FilePickerMounts").setSpacing(2.0f).share();
    _entryList = ui::column("FilePickerEntries").setSpacing(2.0f).share();

    _confirmButton = labeledButton("FilePickerConfirm", _request.confirmLabel)
                         .setOnClick([this]() { (void)confirm(); })
                         .share();

    auto pickerBody = ui::row("FilePickerBody")
                          .setSpacing(6.0f)
                          .setStretchLastChild(true)
                          .child(ui::scroll("FilePickerMountScroll")
                                     .setAxis(EScrollAxis::Vertical)
                                     .child(_mountList, ui::contentSlot().fill()),
                                 ui::boxSlot().preferredSize({180.0f, 0.0f}))
                          .child(ui::scroll("FilePickerEntryScroll")
                                     .setAxis(EScrollAxis::Vertical)
                                     .child(_entryList, ui::contentSlot().fill()),
                                 ui::boxSlot().fill());
    auto actions = ui::row("FilePickerActions")
                       .setSpacing(8.0f)
                       .setMainAxisAlignment(EWidgetMainAxisAlignment::End)
                       .child(labeledButton("FilePickerBack", "Back")
                                  .setOnClick([this]() {
                                      if (_explorer && _explorer->navigateBack()) {
                                          _bRowsDirty = true;
                                      }
                                  }),
                              ui::boxSlot().preferredSize({72.0f, 26.0f}))
                       .child(_confirmButton, ui::boxSlot().preferredSize({84.0f, 26.0f}))
                       .child(labeledButton("FilePickerCancel", "Cancel")
                                  .setOnClick([this]() { close(); }),
                              ui::boxSlot().preferredSize({84.0f, 26.0f}));

    auto pickerRoot = ui::column("FilePickerRoot").setSpacing(8.0f).setPadding({12.0f, 12.0f});
    pickerRoot.child(ui::text("FilePickerTitle")
                         .setText(_request.title)
                         .setStyleKey("text.header"));
    if (isSaveAs()) {
        auto nameField = ui::textField("FilePickerName")
                             .setStyleKey(editorStyle(StyleKey::TextField))
                             .setText(_request.saveAsName);
        _nameField = nameField.share();
        pickerRoot.child(ui::row("FilePickerNameRow")
                             .setSpacing(6.0f)
                             .setStretchLastChild(true)
                             .child(ui::text("FilePickerNameLabel")
                                        .setText(_request.nameFieldLabel)
                                        .setStyleKey("text.muted")
                                        .setVAlign(EWidgetAlignV::Center),
                                    ui::boxSlot().preferredSize({90.0f, 26.0f}))
                             .child(std::move(nameField), ui::boxSlot().fill()),
                         ui::boxSlot().preferredSize({0.0f, 26.0f}));
    }
    pickerRoot.child(std::move(pathText))
        .child(ui::textField("FilePickerSearch")
                   .setStyleKey(editorStyle(StyleKey::TextField))
                   .setOnTextChanged([this](const std::string& text) {
                       if (_explorer) {
                           _explorer->setSearchText(text);
                           _bRowsDirty = true;
                       }
                   }),
               ui::boxSlot().preferredSize({0.0f, 24.0f}))
        .child(std::move(pickerBody), ui::boxSlot().preferredSize({0.0f, 360.0f}))
        .child(std::move(previewText))
        .child(std::move(actions));

    auto dialogPanel = ui::border("FilePickerPanel")
                           .setStyleKey("panel.window")
                           .child(std::move(pickerRoot), ui::contentSlot().fill());
    _panel = dialogPanel.share();
    _overlay = ui::popupOverlay("FilePickerOverlay")
                   .setRole(UIPopupOverlay::EOverlayRole::Modal)
                   .setOnDismiss([this]() { reset(); })
                   .child(std::move(dialogPanel))
                   .share();

    _fingerprint.clear();
    _bRowsDirty = true;
    _overlay->open(tree);
    if (_nameField) {
        tree.setFocus(_nameField.get());
    }
}

void EditorFilePickerDialog::sync(WidgetTree& tree)
{
    if (!_overlay || !_explorer || !_panel) {
        return;
    }

    std::string fingerprint;
    if (const FileExplorer::MountPoint* active = _explorer->getActiveMountPoint()) {
        fingerprint += active->name;
        fingerprint += '|';
    }
    fingerprint += _explorer->getCurrentDirectory().string();
    fingerprint += '|';
    fingerprint += _explorer->getSelectedPath().string();
    std::vector<FileExplorer::FEntry> entries;
    _explorer->collectEntries(entries);
    for (const auto& entry : entries) {
        fingerprint += entry.name;
        fingerprint += ';';
    }
    fingerprint += "|search:";
    fingerprint += _explorer->getSearchText();
    if (_nameField) {
        fingerprint += "|name:";
        fingerprint += _nameField->getText();
    }

    if (fingerprint != _fingerprint) {
        _fingerprint = std::move(fingerprint);
        _bRowsDirty = true;
    }

    if (_bRowsDirty && _mountList && _entryList && _mountList->isAttached() && _entryList->isAttached()) {
        rebuildRows(tree);
        _bRowsDirty = false;
    }

    const Extent2D logicalExtent = tree.getLogicalExtent();
    const glm::vec2 extent = {static_cast<float>(logicalExtent.width), static_cast<float>(logicalExtent.height)};
    const glm::vec2 desired = _panel->computeDesiredSize();
    _overlay->_contentPos = {
        std::max(0.0f, (extent.x - desired.x) * 0.5f),
        std::max(0.0f, (extent.y - desired.y) * 0.5f),
    };

    if (_pathText) {
        std::string pathText = isSaveAs() ? targetDirectory().string() : _explorer->getCurrentDirectory().string();
        if (const FileExplorer::MountPoint* active = _explorer->getActiveMountPoint()) {
            pathText = active->name + ": " + pathText;
        }
        _pathText->setText(pathText);
    }

    const bool bCanConfirm = canConfirm();
    if (_confirmButton) {
        _confirmButton->setEnabled(bCanConfirm);
    }
    if (_previewText) {
        if (bCanConfirm) {
            _previewText->setStyleKey("text.muted");
            if (isSaveAs()) {
                _previewText->setText(std::format("Will save to: {}", composePickedPath()));
            }
            else {
                _previewText->setText(std::format("Selected: {}", _explorer->getSelectedPath().filename().string()));
            }
        }
        else if (isSaveAs()) {
            _previewText->setStyleKey("text.error");
            _previewText->setText("Enter a name and choose a directory.");
        }
        else {
            _previewText->setStyleKey("text.error");
            _previewText->setText(_request.selectionMode == FileExplorer::SelectionMode::Directory
                                      ? "Select a directory to continue."
                                      : "Select a file to continue.");
        }
    }
}

void EditorFilePickerDialog::close()
{
    if (_overlay) {
        _overlay->close();
        return;
    }
    reset();
}

void EditorFilePickerDialog::reset()
{
    _overlay.reset();
    _panel.reset();
    _explorer.reset();
    _pathText.reset();
    _previewText.reset();
    _nameField.reset();
    _confirmButton.reset();
    _mountList.reset();
    _entryList.reset();
    _mountReconciler.reset();
    _entryReconciler.reset();
    _fingerprint.clear();
    _request = {};
    _bRowsDirty = true;
}

bool EditorFilePickerDialog::isOpen() const
{
    return _overlay && _overlay->isAttached();
}

void EditorFilePickerDialog::selectPath(const std::filesystem::path& path)
{
    if (!_explorer) {
        return;
    }
    _explorer->setSelectedPath(path);
    _bRowsDirty = true;
}

void EditorFilePickerDialog::setSaveAsName(std::string name)
{
    _request.saveAsName = name;
    if (_nameField) {
        _nameField->setText(std::move(name));
    }
}

bool EditorFilePickerDialog::confirm()
{
    if (!_explorer || !_request.onPicked || !canConfirm()) {
        return false;
    }

    const std::string picked = composePickedPath();
    if (picked.empty()) {
        return false;
    }

    if (!_request.configScope.empty()) {
        const std::filesystem::path pickedPath = path_utils::pathFromUtf8String(picked);
        const std::filesystem::path lastDir = isSaveAs() || _request.selectionMode == FileExplorer::SelectionMode::Directory
                                                  ? (isSaveAs() ? pickedPath.parent_path() : pickedPath)
                                                  : pickedPath.parent_path();
        ConfigManager::Editor("editor")
            .set(_request.configScope + ".lastDirectory", path_utils::pathToUtf8String(lastDir))
            .flush();
    }

    std::function<void(std::string)> callback = std::move(_request.onPicked);
    close();
    callback(picked);
    return true;
}

void EditorFilePickerDialog::rebuildRows(WidgetTree& tree)
{
    if (!_explorer || !_mountList || !_entryList) {
        return;
    }

    const FileExplorer::MountPoint* active = _explorer->getActiveMountPoint();
    const std::filesystem::path selectedPath = _explorer->getSelectedPath();
    std::vector<FileExplorer::FEntry> entries;
    _explorer->collectEntries(entries);

    if (!_mountReconciler) {
        _mountReconciler = std::make_unique<UIKeyedChildReconciler>(tree, *_mountList, makeContentRowFactory());
    }
    if (!_entryReconciler) {
        _entryReconciler = std::make_unique<UIKeyedChildReconciler>(tree, *_entryList, makeContentRowFactory());
    }

    std::vector<std::string> mountKeys;
    mountKeys.reserve(_explorer->getMountPoints().size());
    for (const auto& mp : _explorer->getMountPoints()) {
        mountKeys.push_back("FilePickerMount_" + mp.name);
    }
    _mountReconciler->reconcile(
        mountKeys,
        [this, active](UIElement& child, const std::string&, size_t index) {
            const auto& mp = _explorer->getMountPoints()[index];
            updateContentRow(child,
                             mp.name,
                             mp.name,
                             active != nullptr && active->name == mp.name,
                             [this](const std::string& itemId) { selectMount(itemId); },
                             [this](const std::string& itemId) { selectMount(itemId); },
                             true);
        },
        bindEditorListRowSlot);

    std::vector<std::string> entryKeys;
    entryKeys.reserve(entries.size());
    for (const auto& entry : entries) {
        entryKeys.push_back("FilePickerEntry_" + entry.name);
    }
    _entryReconciler->reconcile(
        entryKeys,
        [this, &entries, selectedPath](UIElement& child, const std::string&, size_t index) {
            const auto& entry = entries[index];
            const std::filesystem::path path = entry.path;
            const bool bDir = entry.bIsDirectory;
            updateContentRow(child,
                             bDir ? entry.name + "/" : entry.name,
                             entry.name,
                             selectedPath == path,
                             [this, path](const std::string&) { selectItem(path); },
                             [this, path, bDir](const std::string&) { activateItem(path, bDir); },
                             bDir);
        },
        bindEditorListRowSlot);
}

void EditorFilePickerDialog::selectMount(const std::string& itemId)
{
    if (!_explorer) {
        return;
    }
    for (const auto& candidate : _explorer->getMountPoints()) {
        if (candidate.name == itemId) {
            _explorer->selectMountPoint(candidate);
            _bRowsDirty = true;
            break;
        }
    }
}

void EditorFilePickerDialog::selectItem(const std::filesystem::path& path)
{
    selectPath(path);
}

void EditorFilePickerDialog::activateItem(const std::filesystem::path& path, bool bIsDirectory)
{
    if (!_explorer) {
        return;
    }
    if (bIsDirectory) {
        if (_explorer->navigateInto(path)) {
            _bRowsDirty = true;
        }
        return;
    }
    _explorer->setSelectedPath(path);
    _bRowsDirty = true;
    (void)confirm();
}

std::filesystem::path EditorFilePickerDialog::targetDirectory() const
{
    if (!_explorer) {
        return {};
    }
    if (!_explorer->getSelectedPath().empty()) {
        return _explorer->getSelectedPath();
    }
    return _explorer->getCurrentDirectory();
}

bool EditorFilePickerDialog::canConfirm() const
{
    if (!_explorer) {
        return false;
    }
    if (isSaveAs()) {
        const std::string name = _nameField ? _nameField->getText() : _request.saveAsName;
        return !name.empty() && !targetDirectory().empty();
    }
    std::vector<FileExplorer::FEntry> entries;
    _explorer->collectEntries(entries);
    return isRetainedPickerSelectionValid(entries, _explorer->getSelectedPath(), _request.selectionMode);
}

std::string EditorFilePickerDialog::composePickedPath() const
{
    if (!_explorer) {
        return {};
    }
    if (isSaveAs()) {
        const std::string name = _nameField ? _nameField->getText() : _request.saveAsName;
        const std::filesystem::path dir = targetDirectory();
        if (name.empty() || dir.empty()) {
            return {};
        }
        return path_utils::pathToUtf8String(dir / (name + _request.saveAsExtension));
    }
    return path_utils::pathToUtf8String(_explorer->getSelectedPath());
}

} // namespace ya

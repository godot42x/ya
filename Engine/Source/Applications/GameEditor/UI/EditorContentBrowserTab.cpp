#include "GameEditor/UI/EditorContentBrowserTab.h"
#include "GameEditor/UI/EditorListRows.h"

#include "Core/System/PathUtils.h"
#include "Core/System/VirtualFileSystem.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/KeyedChildReconciler.h"
#include "GUI/Widgets/KeyedVisibleWindow.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/FileExplorer.h"
#include "GameRuntime/App.h"

#include <string_view>

namespace ya
{

std::shared_ptr<UIElement> EditorContentBrowserTab::build(WidgetTree&)
{
    _explorer = std::make_shared<FileExplorer>();
    _explorer->setConfigScope("editorContentBrowser");
    _explorer->initFromVFS();
    _explorer->setFilterMode(FileExplorer::FilterMode::Both);
    _explorer->setSelectionMode(FileExplorer::SelectionMode::File);
    _explorer->setLeftPanelWidth(180.0f);

    auto pathText = ui::text("ContentPath").setFontSize(12).setVAlign(EWidgetAlignV::Center);
    _pathText = pathText.share();

    _mountList = ui::column("ContentMounts").setSpacing(kEditorListRowSpacing).share();
    auto entryLeading = ui::sizeBox("ContentEntryLeading");
    _entryLeading = entryLeading.share();
    auto entryRows = ui::column("ContentEntryRows").setSpacing(kEditorListRowSpacing);
    _entryRows = entryRows.share();
    auto entryTrailing = ui::sizeBox("ContentEntryTrailing");
    _entryTrailing = entryTrailing.share();
    _entryList = ui::column("ContentEntries")
                     .setSpacing(0.0f)
                     .child(std::move(entryLeading))
                     .child(std::move(entryRows))
                     .child(std::move(entryTrailing))
                     .share();
    _mountReconciler.reset();
    _entryReconciler.reset();
    _entryScrollOffset = 0.0f;
    _entryViewportHeight = 0.0f;

    auto backButton = ui::button("ContentBack")
                          .setOnClick([this]() {
                              if (_explorer) {
                                  _explorer->navigateBack();
                              }
                          })
                          .child(ui::text("ContentBack_Label")
                                     .setText("< Back")
                                     .setFontSize(12)
                                     .setHAlign(EWidgetAlignH::Center)
                                     .setVAlign(EWidgetAlignV::Center));

    auto searchField = ui::textField("ContentSearch")
                           .setOnTextChanged([this](const std::string& text) {
                               if (_explorer) {
                                   _explorer->setSearchText(text);
                                   _bRowsDirty = true;
                               }
                           });
    _searchField = searchField.share();

    auto header = ui::row("ContentBrowser.ContainerHeader", "Header")
                      .setSpacing(6.0f)
                      .child(std::move(backButton), ui::boxSlot().preferredSize({52.0f, 22.0f}))
                      .child(std::move(pathText))
                      .child(std::move(searchField), ui::boxSlot().preferredSize({140.0f, 22.0f}));

    auto mountScroll = ui::scroll("ContentMountScroll").child(_mountList, ui::overlaySlot().fill());
    auto entryScroll = ui::scroll("ContentEntryScroll").child(_entryList, ui::overlaySlot().fill());
    _entryScroll = entryScroll.share();
    auto body = ui::row("ContentBody")
                    .setSpacing(4.0f)
                    .child(std::move(mountScroll), ui::boxSlot().preferredSize({180.0f, 0.0f}))
                    .child(std::move(entryScroll), ui::boxSlot().fill());

    auto root = ui::column("ContentBrowserRoot")
                    .setSpacing(2.0f)
                    .setPadding({4.0f, 4.0f})
                    .child(std::move(header), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                    .child(std::move(body), ui::boxSlot().fill());
    return root.release();
}

void EditorContentBrowserTab::sync(WidgetTree& tree)
{
    if (!_explorer || !_pathText) {
        return;
    }

    std::string fingerprint;
    if (const FileExplorer::MountPoint* active = _explorer->getActiveMountPoint()) {
        fingerprint += active->name;
        fingerprint += '|';
    }
    fingerprint += _explorer->getCurrentDirectory().string();
    fingerprint += '|';

    std::vector<FileExplorer::FEntry> entries;
    _explorer->collectEntries(entries);
    for (const auto& entry : entries) {
        fingerprint += entry.name;
        fingerprint += entry.bIsDirectory ? "/" : ";";
    }
    fingerprint += "|search:";
    fingerprint += _explorer->getSearchText();
    fingerprint += "|selected:";
    fingerprint += _explorer->getSelectedPath().string();

    if (fingerprint != _fingerprint) {
        _fingerprint = std::move(fingerprint);
        _bRowsDirty = true;
        if (_entryScroll) {
            _entryScroll->setScrollOffset(0.0f);
            _entryScrollOffset = 0.0f;
        }
    }
    if (_entryScroll && _entryScroll->isAttached()) {
        const float offset = _entryScroll->getScrollOffset();
        const float viewportHeight = _entryScroll->getLayoutRect().extent.y;
        if (offset != _entryScrollOffset || viewportHeight != _entryViewportHeight) {
            _entryScrollOffset = offset;
            _entryViewportHeight = viewportHeight;
            _bRowsDirty = true;
        }
    }
    if (_bRowsDirty && _mountList && _entryList && _entryRows &&
        _mountList->isAttached() && _entryList->isAttached() && _entryRows->isAttached()) {
        rebuildRows(tree);
        _bRowsDirty = false;
    }
    if (_searchField && tree.getFocused() != _searchField.get()) {
        _searchField->setText(_explorer->getSearchText());
    }
}

void EditorContentBrowserTab::rebuildRows(WidgetTree& tree)
{
    if (!_explorer || !_mountList || !_entryRows) {
        return;
    }

    const FileExplorer::MountPoint* active = _explorer->getActiveMountPoint();
    const std::filesystem::path selectedPath = _explorer->getSelectedPath();
    std::vector<FileExplorer::FEntry> entries;
    _explorer->collectEntries(entries);

    if (!_mountReconciler) {
        _mountReconciler = std::make_unique<UIKeyedChildReconciler>(
            tree, *_mountList, makeContentRowFactory());
    }
    if (!_entryReconciler) {
        _entryReconciler = std::make_unique<UIKeyedChildReconciler>(
            tree, *_entryRows, makeContentRowFactory());
    }

    std::vector<std::string> mountKeys;
    mountKeys.reserve(_explorer->getMountPoints().size());
    for (const auto& mp : _explorer->getMountPoints()) {
        mountKeys.push_back("ContentMount_" + mp.name);
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
                             [this](const std::string& itemId) { selectMount(itemId); });
        },
        bindEditorListRowSlot);

    std::vector<std::string> entryKeys;
    entryKeys.reserve(entries.size());
    for (const auto& entry : entries) {
        entryKeys.push_back("ContentEntry_" + entry.name);
    }
    const FKeyedVisibleWindow window = computeKeyedVisibleWindow(entryKeys.size(),
                                                                kEditorListRowHeight,
                                                                kEditorListRowSpacing,
                                                                _entryViewportHeight,
                                                                _entryScrollOffset,
                                                                kEditorListOverscan);
    if (_entryLeading) {
        _entryLeading->setHeightOverride(window.leadingExtent);
    }
    if (_entryTrailing) {
        _entryTrailing->setHeightOverride(window.trailingExtent);
    }
    const std::vector<std::string> visibleKeys = sliceKeyedVisibleWindow(entryKeys, window);
    _entryReconciler->reconcile(
        visibleKeys,
        [this, &entries, window, selectedPath](UIElement& child, const std::string&, size_t index) {
            const size_t itemIndex = window.first + index;
            const auto& entry = entries[itemIndex];
            const std::filesystem::path path = entry.path;
            const bool bDir = entry.bIsDirectory;
            updateContentRow(child,
                             bDir ? entry.name + "/" : entry.name,
                             entry.name,
                             selectedPath == path,
                             [this, path, bDir](const std::string&) { selectItem(path, bDir); },
                             [this, path, bDir](const std::string&) { activateItem(path, bDir); });
        },
        bindEditorListRowSlot);

    if (_pathText) {
        std::string pathText = _explorer->getCurrentDirectory().string();
        if (active) {
            pathText = active->name + ": " + pathText;
        }
        _pathText->setText(pathText);
    }
}

void EditorContentBrowserTab::selectMount(const std::string& itemId)
{
    if (!_explorer) {
        return;
    }
    for (const auto& candidate : _explorer->getMountPoints()) {
        if (candidate.name == itemId) {
            _explorer->selectMountPoint(candidate);
            break;
        }
    }
}

void EditorContentBrowserTab::selectItem(const std::filesystem::path& path, bool bIsDirectory)
{
    if (!_explorer) {
        return;
    }
    _explorer->setSelectedPath(path);
    _bRowsDirty = true;
    if (!_layer || bIsDirectory) {
        return;
    }

    std::string assetPath = path_utils::pathToUtf8String(path);
    if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
        assetPath = vfs->toVfsPath(assetPath);
    }
    const auto isTexturePath = [](std::string_view value) {
        return value.ends_with(".png") || value.ends_with(".jpg") || value.ends_with(".jpeg") ||
               value.ends_with(".tga") || value.ends_with(".bmp") || value.ends_with(".hdr");
    };
    if (isTexturePath(assetPath)) {
        _layer->inspectAsset(assetPath);
    }
}

void EditorContentBrowserTab::activateItem(const std::filesystem::path& path, bool bIsDirectory)
{
    if (!_explorer) {
        return;
    }
    if (bIsDirectory) {
        _explorer->navigateInto(path);
        return;
    }
    std::string utf8Path = path_utils::pathToUtf8String(path);
    if (utf8Path.ends_with(".scene.json")) {
        if (App* app = App::get()) {
            const std::string scenePath = std::move(utf8Path);
            app->getTaskManager().registerFrameTask([scenePath]() {
                App::get()->getSceneServices().loadScene(scenePath);
            });
        }
    }
}

} // namespace ya

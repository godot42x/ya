#include "GameEditor/UI/Tabs/EditorContentBrowserTab.h"
#include "GameEditor/UI/Shell/EditorListRows.h"

#include "Core/System/PathUtils.h"
#include "Core/System/VirtualFileSystem.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/KeyedChildReconciler.h"
#include "GUI/Widgets/KeyedVisibleWindow.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/FileExplorer.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ya
{

EditorContentBrowserTab::EditorContentBrowserTab(EditorLayer& layer)
    : UICompoundWidget("ContentBrowserRoot", "panel.canvas")
    , _layer(&layer)
{
    enableTick();
}

void EditorContentBrowserTab::construct()
{
    _explorer = std::make_shared<FileExplorer>();
    _explorer->setConfigScope("editorContentBrowser");
    _explorer->initFromVFS();
    _explorer->setFilterMode(FileExplorer::FilterMode::Both);
    _explorer->setSelectionMode(FileExplorer::SelectionMode::File);
    _explorer->setLeftPanelWidth(180.0f);

    auto pathText = ui::text("ContentPath")
                         .setStyleKey("text.small")
                         .setVAlign(EWidgetAlignV::Center);
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
                          .setOnClick([this]() { _explorer->navigateBack(); })
                          .child(ui::text("ContentBack_Label")
                                     .setText("< Back")
                                     .setStyleKey(editorStyle(StyleKey::Text))
                                     .setHAlign(EWidgetAlignH::Center)
                                     .setVAlign(EWidgetAlignV::Center));

    auto searchField = ui::textField("ContentSearch")
                           .setStyleKey(editorStyle(StyleKey::TextField))
                           .setOnTextChanged([this](const std::string& text) {
                               _explorer->setSearchText(text);
                               _bRowsDirty = true;
                           });
    _searchField = searchField.share();

    auto listToggle = ui::selectableRow("ContentViewList")
                          .setItemId("list")
                          .setContentPadding(FMargin{6.0f, 0.0f, 6.0f, 0.0f})
                          .setOnSelect([this](const std::string&) {
                              setViewMode(FileExplorer::ViewMode::List);
                          })
                          .child(ui::text("ContentViewList_Label")
                                     .setText("List")
                                     .setStyleKey(editorStyle(StyleKey::Text))
                                     .setHAlign(EWidgetAlignH::Center)
                                     .setVAlign(EWidgetAlignV::Center),
                                 ui::contentSlot().fill());
    _listModeToggle = listToggle.share();

    auto gridToggle = ui::selectableRow("ContentViewGrid")
                          .setItemId("grid")
                          .setContentPadding(FMargin{6.0f, 0.0f, 6.0f, 0.0f})
                          .setOnSelect([this](const std::string&) {
                              setViewMode(FileExplorer::ViewMode::Icon);
                          })
                          .child(ui::text("ContentViewGrid_Label")
                                     .setText("Grid")
                                     .setStyleKey(editorStyle(StyleKey::Text))
                                     .setHAlign(EWidgetAlignH::Center)
                                     .setVAlign(EWidgetAlignV::Center),
                                 ui::contentSlot().fill());
    _gridModeToggle = gridToggle.share();

    auto header = ui::row("ContentBrowser.ContainerHeader", "Header")
                      .setSpacing(6.0f)
                      .child(std::move(backButton), ui::boxSlot().preferredSize({52.0f, 22.0f}))
                      .child(std::move(pathText))
                      .child(std::move(listToggle), ui::boxSlot().preferredSize({44.0f, 22.0f}))
                      .child(std::move(gridToggle), ui::boxSlot().preferredSize({44.0f, 22.0f}))
                      .child(std::move(searchField), ui::boxSlot().preferredSize({160.0f, 24.0f}));

    auto mountScroll = ui::scroll("ContentMountScroll").child(_mountList, ui::contentSlot().fill());
    auto entryScroll = ui::scroll("ContentEntryScroll").child(_entryList, ui::contentSlot().fill());
    _entryScroll = entryScroll.share();
    auto body = ui::splitPane("ContentBody")
                    .setOrientation(ESplitOrientation::Vertical)
                    .setSplitRatio(0.22f)
                    .setMinFirstExtent(120.0f)
                    .setMinSecondExtent(180.0f)
                    .child(std::move(mountScroll))
                    .child(std::move(entryScroll));
    _bodySplit = body.share();
    _bodySplit->setSplitRatioChangedCallback([this](float ratio) {
        const float total = _bodySplit->getLayoutRect().extent.x;
        if (total > 1.0f) {
            _explorer->setLeftPanelWidth(ratio * total);
            _explorer->saveConfig();
        }
    });

    auto root = ui::column("ContentBrowserInner")
                    .setSpacing(2.0f)
                    .setPadding({4.0f, 4.0f})
                    .child(std::move(header), ui::boxSlot().preferredSize({0.0f, 28.0f}))
                    .child(std::move(body), ui::boxSlot().fill());
    addDetachedChild(root.release());
}

void EditorContentBrowserTab::onAttached()
{
    refresh();
}

void EditorContentBrowserTab::tick(float)
{
    refresh();
}

void EditorContentBrowserTab::refresh()
{
    refreshFromTree(*getTree());
}

void EditorContentBrowserTab::refreshFromTree(WidgetTree& tree)
{
    if (_layer) {
        const std::string pending = _layer->consumePendingContentReveal();
        if (!pending.empty()) {
            if (auto* vfs = VirtualFileSystem::get()) {
                _explorer->setSelectedPath(vfs->translatePath(pending));
            }
        }
    }

    const uint64_t generation = _explorer->contentGeneration();
    if (generation != _explorerGeneration) {
        _explorerGeneration = generation;
        _bRowsDirty = true;
        _entryScroll->setScrollOffset(0.0f);
        _entryScrollOffset = 0.0f;
        _entryScrollOffsetPx = 0;
    }

    const std::string selectedFingerprint = _explorer->getSelectedPath().string();
    if (selectedFingerprint != _selectedFingerprint) {
        _selectedFingerprint = selectedFingerprint;
        if (!_bRowsDirty) {
            _bSelectionDirty = true;
        }
    }

    if (_entryScroll->isAttached()) {
        const float offset = _entryScroll->getScrollOffset();
        const float viewportHeight = _entryScroll->getLayoutRect().extent.y;
        const float viewportWidth = _entryScroll->getLayoutRect().extent.x;
        const int offsetPx = static_cast<int>(std::lround(offset));
        const int heightPx = std::max(0, static_cast<int>(std::lround(viewportHeight)));
        const int widthPx = std::max(0, static_cast<int>(std::lround(viewportWidth)));
        if (offsetPx != _entryScrollOffsetPx || heightPx != _entryViewportHeightPx ||
            widthPx != _entryViewportWidthPx) {
            _entryScrollOffset = offset;
            _entryViewportHeight = viewportHeight;
            _entryViewportWidth = viewportWidth;
            _entryScrollOffsetPx = offsetPx;
            _entryViewportHeightPx = heightPx;
            _entryViewportWidthPx = widthPx;
            _bRowsDirty = true;
        }
    }
    if (_bRowsDirty && _mountList->isAttached() && _entryList->isAttached() && _entryRows->isAttached()) {
        rebuildRows(tree);
        _bRowsDirty = false;
        _bSelectionDirty = false;
    }
    else if (_bSelectionDirty) {
        applySelection();
        _bSelectionDirty = false;
    }
    if (!_bSplitRatioFromConfig) {
        const float total = _bodySplit->getLayoutRect().extent.x;
        if (total > 1.0f) {
            const float left = _explorer->getLeftPanelWidth();
            _bodySplit->setSplitRatio(std::clamp(left / total, 0.08f, 0.6f));
            _bSplitRatioFromConfig = true;
        }
    }
    if (tree.getFocused() != _searchField.get()) {
        _searchField->setText(_explorer->getSearchText());
    }
    _listModeToggle->setSelected(_explorer->getViewMode() == FileExplorer::ViewMode::List);
    _gridModeToggle->setSelected(_explorer->getViewMode() == FileExplorer::ViewMode::Icon);
}

void EditorContentBrowserTab::rebuildRows(WidgetTree& tree)
{
    const FileExplorer::MountPoint* active = _explorer->getActiveMountPoint();
    const std::filesystem::path selectedPath = _explorer->getSelectedPath();
    std::vector<FileExplorer::FEntry> entries;
    _explorer->collectEntries(entries);

    if (!_mountReconciler) {
        _mountReconciler = std::make_unique<UIKeyedChildReconciler>(
            tree, *_mountList, makeContentRowFactory());
    }

    const FileExplorer::ViewMode viewMode = _explorer->getViewMode();
    if (!_entryReconciler || viewMode != _reconciledViewMode) {
        if (_entryReconciler) {
            _entryReconciler->reconcile({});
        }
        _reconciledViewMode = viewMode;
        _entryReconciler = std::make_unique<UIKeyedChildReconciler>(
            tree,
            *_entryRows,
            viewMode == FileExplorer::ViewMode::Icon ? makeContentGridRowFactory()
                                                     : makeContentRowFactory());
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
            // Mounts switch on a single click (they are tabs, not assets).
            updateContentRow(child,
                             mp.name,
                             mp.name,
                             active != nullptr && active->name == mp.name,
                             [this](const std::string& itemId) { selectMount(itemId); },
                             [this](const std::string& itemId) { selectMount(itemId); },
                             true,
                             /*bActivateOnDoubleClick=*/false);
        },
        bindEditorListRowSlot);

    std::vector<std::string> entryKeys;
    FKeyedVisibleWindow window;
    const float thumbnailSize = _explorer->getThumbnailSize();
    const float pad = std::max(8.0f, _explorer->getPadding() * 0.5f);
    const float rowH = editorGridRowHeight(thumbnailSize);
    const float cellW = thumbnailSize + pad;
    constexpr float kGridScrollbarGutter = 14.0f;
    const float gridWidth = std::max(1.0f, _entryViewportWidth - kGridScrollbarGutter);
    const int cols = std::max(1, static_cast<int>(std::floor(gridWidth / std::max(cellW, 1.0f))));
    _gridColumnCount = cols;

    if (viewMode == FileExplorer::ViewMode::Icon) {
        const size_t rowCount = entries.empty() ? 0
                                                : (entries.size() + static_cast<size_t>(cols) - 1) /
                                                      static_cast<size_t>(cols);
        entryKeys.reserve(rowCount);
        for (size_t row = 0; row < rowCount; ++row) {
            entryKeys.push_back("ContentGridRow_" + std::to_string(row));
        }
        window = computeKeyedVisibleWindow(entryKeys.size(),
                                           rowH,
                                           kEditorListRowSpacing,
                                           _entryViewportHeight,
                                           _entryScrollOffset,
                                           kEditorListOverscan);
    }
    else {
        entryKeys.reserve(entries.size());
        for (const auto& entry : entries) {
            entryKeys.push_back("ContentEntry_" + entry.name);
        }
        window = computeKeyedVisibleWindow(entryKeys.size(),
                                           kEditorListRowHeight,
                                           kEditorListRowSpacing,
                                           _entryViewportHeight,
                                           _entryScrollOffset,
                                           kEditorListOverscan);
    }
    _entryLeading->setHeightOverride(window.leadingExtent);
    _entryTrailing->setHeightOverride(window.trailingExtent);
    const std::vector<std::string> visibleKeys = sliceKeyedVisibleWindow(entryKeys, window);
    if (viewMode == FileExplorer::ViewMode::Icon) {
        _entryReconciler->reconcile(
            visibleKeys,
            [this, &tree, &entries, window, selectedPath, cols, cellW, rowH, thumbnailSize](
                UIElement& child, const std::string&, size_t index) {
                const size_t rowIndex = window.first + index;
                const size_t begin = rowIndex * static_cast<size_t>(cols);
                const size_t remaining = begin < entries.size() ? entries.size() - begin : 0;
                const size_t count = std::min(remaining, static_cast<size_t>(cols));
                syncGridRowTiles(tree, child, entries, begin, count, selectedPath, cellW, rowH, thumbnailSize);
            },
            [rowH](UISlot& slot, const std::string&, size_t) {
                if (auto* box = slot.as<UIBoxSlot>()) {
                    box->setPreferredSize({0.0f, rowH});
                }
            });
    }
    else {
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
                                 [this, path, bDir](const std::string&) { activateItem(path, bDir); },
                                 bDir);
            },
            bindEditorListRowSlot);
    }

    std::string pathText = _explorer->getCurrentDirectory().string();
    if (active) {
        pathText = active->name + ": " + pathText;
    }
    _pathText->setText(pathText);
}

void EditorContentBrowserTab::selectMount(const std::string& itemId)
{
    for (const auto& candidate : _explorer->getMountPoints()) {
        if (candidate.name == itemId) {
            _explorer->selectMountPoint(candidate);
            break;
        }
    }
}

void EditorContentBrowserTab::selectItem(const std::filesystem::path& path, bool bIsDirectory)
{
    _explorer->setSelectedPath(path);
    _bSelectionDirty = true;
    if (bIsDirectory) {
        return;
    }

    std::string assetPath = path_utils::pathToUtf8String(path);
    if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
        assetPath = vfs->toVfsPath(assetPath);
    }
    if (isEditorTexturePath(assetPath)) {
        _layer->inspectAsset(assetPath);
    }
}

void EditorContentBrowserTab::activateItem(const std::filesystem::path& path, bool bIsDirectory)
{
    if (bIsDirectory) {
        _explorer->navigateInto(path);
        return;
    }
    selectItem(path, false);
    std::string utf8Path = path_utils::pathToUtf8String(path);
    if (utf8Path.ends_with(".scene.json")) {
        _layer->cmdLoadScene(std::move(utf8Path));
    }
    else if (utf8Path.ends_with(".lua")) {
        _layer->openDocumentEditor(EEditorDocumentKind::Script, std::move(utf8Path));
    }
    else if (utf8Path.ends_with(".yaui.json")) {
        // Through the document-editor entry point, not the designer session
        // directly: that is what binds the UI root and raises the Designer
        // tab. Talking to the session alone loaded the document invisibly.
        std::string assetPath = utf8Path;
        if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
            const std::string vfsPath = vfs->toVfsPath(assetPath);
            if (!vfsPath.empty()) {
                assetPath = vfsPath;
            }
        }
        if (!_layer->openDocumentEditor(EEditorDocumentKind::UI, std::move(assetPath))) {
            // Fall back to the session so a designer that is not on screen
            // still opens the document rather than silently doing nothing.
            _layer->getEditorUIDesignerSession().openDocument(utf8Path);
        }
    }
    else if (utf8Path.ends_with(".mat") || utf8Path.ends_with(".material")) {
        _layer->openDocumentEditor(EEditorDocumentKind::Material, std::move(utf8Path));
    }
}

void EditorContentBrowserTab::setViewMode(FileExplorer::ViewMode mode)
{
    if (_explorer->getViewMode() == mode) {
        return;
    }
    _explorer->setViewMode(mode);
    _explorer->saveConfig();
    _bRowsDirty = true;
}

std::string EditorContentBrowserTab::entryIconPath(const FileExplorer::FEntry& entry) const
{
    if (entry.bIsDirectory) {
        return editor_icons::kFolder;
    }
    std::string assetPath = path_utils::pathToUtf8String(entry.path);
    if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
        assetPath = vfs->toVfsPath(assetPath);
    }
    if (isEditorTexturePath(assetPath)) {
        return assetPath;
    }
    return editor_icons::kFile;
}

void EditorContentBrowserTab::applySelection()
{
    const FileExplorer::MountPoint* active = _explorer->getActiveMountPoint();
    const std::filesystem::path selectedPath = _explorer->getSelectedPath();
    const std::string selectedName = selectedPath.filename().string();

    for (const auto& child : _mountList->getChildren()) {
        auto* row = dynamic_cast<UISelectableRow*>(child.get());
        if (!row) {
            continue;
        }
        row->setSelected(active != nullptr && row->_itemId == active->name);
    }

    if (_explorer->getViewMode() == FileExplorer::ViewMode::Icon) {
        for (const auto& row : _entryRows->getChildren()) {
            if (!row) {
                continue;
            }
            for (const auto& tile : row->getChildren()) {
                auto* selectable = dynamic_cast<UISelectableRow*>(tile.get());
                if (!selectable) {
                    continue;
                }
                selectable->setSelected(!selectedName.empty() && selectable->_itemId == selectedName);
            }
        }
        return;
    }

    for (const auto& child : _entryRows->getChildren()) {
        auto* row = dynamic_cast<UISelectableRow*>(child.get());
        if (!row) {
            continue;
        }
        row->setSelected(!selectedName.empty() && row->_itemId == selectedName);
    }
}

void EditorContentBrowserTab::syncGridRowTiles(WidgetTree& tree,
                                               UIElement& row,
                                               const std::vector<FileExplorer::FEntry>& entries,
                                               size_t begin,
                                               size_t count,
                                               const std::filesystem::path& selectedPath,
                                               float cellW,
                                               float rowH,
                                               float thumbnailSize)
{
    auto* box = dynamic_cast<UIContainer*>(&row);
    if (!box) {
        YA_CORE_ERROR("EditorContentBrowserTab: grid row '{}' is not a UIContainer", row._name);
        return;
    }

    const FChildSlotInitializer tileSlot = [cellW, rowH](UIElement&, UISlot& edge) {
        if (auto* slot = edge.as<UIBoxSlot>()) {
            slot->setPreferredSize({cellW, rowH});
        }
    };

    while (box->getChildren().size() > count) {
        tree.detach(*box->getChildren().back());
    }
    while (box->getChildren().size() < count) {
        const size_t i = box->getChildren().size();
        const auto& entry = entries[begin + i];
        const std::string key = "ContentTile_" + entry.name;
        auto tile = contentTile(key, entry.name, entry.name, {}, {}).release();
        tree.attach(*box, tile, tileSlot);
    }
    for (size_t i = 0; i < count; ++i) {
        const auto& entry = entries[begin + i];
        const std::filesystem::path path = entry.path;
        const bool bDir = entry.bIsDirectory;
        UIElement& tile = *box->getChildren()[i];
        updateContentTile(tile,
                          entry.name,
                          entry.name,
                          selectedPath == path,
                          [this, path, bDir](const std::string&) { selectItem(path, bDir); },
                          [this, path, bDir](const std::string&) { activateItem(path, bDir); },
                          entryIconPath(entry),
                          thumbnailSize);
        if (UISlot* slot = box->getSlotForChild(tile)) {
            if (auto* boxSlot = slot->as<UIBoxSlot>()) {
                boxSlot->setPreferredSize({cellW, rowH});
            }
        }
    }
}

} // namespace ya

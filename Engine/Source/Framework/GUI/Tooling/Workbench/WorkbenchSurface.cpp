#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GUI/Tooling/Workbench/WorkbenchTheme.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace guiworkbench
{

namespace
{

// The shell chrome reads its palette from the theme tokens (style-system
// Phase 4); the values below mirror the token names for legacy refs. Labels
// keep their authored token colors (explicit-authoring wins over the theme
// in the resolve chain), so headers/status stay hierarchy-stable under any
// mounted theme.
constexpr glm::vec4 kHeaderColor = guiworkbench::tokens::kHeaderColor;

void logWorkbenchRectOnce(const char* label, const ya::UIElement* element)
{
    static int sLoggedFrames = 0;
    if (!element || sLoggedFrames >= 6) {
        return;
    }
    const auto& rect = element->_layoutRect;
    YA_CORE_INFO("Workbench {} rect: pos=({}, {}), extent=({}, {})",
                 label,
                 rect.pos.x,
                 rect.pos.y,
                 rect.extent.x,
                 rect.extent.y);
    ++sLoggedFrames;
}

} // namespace

void FWorkbenchSurface::assembleChrome(ya::WidgetTree& tree, ya::UIElement& parent)
{
    buildMenuBar(tree, parent);
    buildWorkspaceShell(tree, parent);
    buildStatusBar(tree, parent);
    selectPage(_initialPageIndex);
}

void FWorkbenchSurface::buildUI(ya::WidgetTree& tree)
{
    _tree = &tree;

    _root = std::make_shared<ya::UIPanel>("WorkbenchRoot");
    _root->_anchorMin = {0.0f, 0.0f};
    _root->_anchorMax = {1.0f, 1.0f};
    // Shell chrome resolves its fill from the mounted theme (Phase 4): the
    // window key drives the root backdrop; no authored color so the
    // white/dark toggle restyles the whole shell.
    _root->_styleKey = "panel.window";
    tree.attachToLayer(ya::WidgetTree::ELayer::Content, _root);

    assembleChrome(tree, *_root);
}

void FWorkbenchSurface::buildUI(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _tree = &tree;

    _root = std::make_shared<ya::UIPanel>("WorkbenchRoot");
    _root->_anchorMin = {0.0f, 0.0f};
    _root->_anchorMax = {1.0f, 1.0f};
    _root->_styleKey = "panel.window";
    tree.attach(parent, _root);

    assembleChrome(tree, *_root);
}

int FWorkbenchSurface::findPageIndexByName(const std::string& name) const
{
    for (size_t i = 0; i < _pages.size(); ++i) {
        if (_pages[i].name == name) {
            return static_cast<int>(i);
        }
    }
    if (name == "Editor") {
        return static_cast<int>(_pages.size());
    }
    return -1;
}

int FWorkbenchSurface::addPage(const std::string& name, FPageBuilder builder)
{
    _pages.push_back(FPage{.name = name, .build = std::move(builder)});
    return static_cast<int>(_pages.size()) - 1;
}

void FWorkbenchSurface::failSmoke(const std::string& message)
{
    YA_CORE_ERROR("{}", message);
    _bAutomationDone = true;
}

void FWorkbenchSurface::buildMenuBar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _menuBar = std::make_shared<ya::UIMenuBar>("MainMenu");
    _menuBar->_anchorMin = {0.0f, 0.0f};
    _menuBar->_anchorMax = {1.0f, 0.0f};
    _menuBar->setPosition({0.0f, 0.0f});
    _menuBar->setSize({0.0f, 30.0f});
    tree.attach(parent, _menuBar);

    const auto log = [this](const std::string& text) { logStatus(text); };

    _menuBar->addItem("File", [this, log]
    {
        return ya::UIMenu::create({
            ya::UIMenu::FItem{.label = "New Document", .action = [log] { log("Menu: New Document"); }},
            ya::UIMenu::FItem{.label = "Open File...", .action = [log] { log("Menu: Open File..."); }},
            ya::UIMenu::FItem{.label = "Save", .action = [log] { log("Menu: Save"); }},
            ya::UIMenu::FItem{.label = "Save As...", .action = [log] { log("Menu: Save As..."); }},
            ya::UIMenu::FItem::separator(),
            ya::UIMenu::FItem{.label = "Exit", .action = [log] { log("Menu: Exit"); }},
        });
    });
    _menuBar->addItem("Edit", [log]
    {
        return ya::UIMenu::create({
            ya::UIMenu::FItem{.label = "Undo", .action = [log] { log("Menu: Undo"); }},
            ya::UIMenu::FItem{.label = "Redo", .action = [log] { log("Menu: Redo"); }},
            ya::UIMenu::FItem::separator(),
            ya::UIMenu::FItem{.label = "Copy", .action = [log] { log("Menu: Copy"); }},
            ya::UIMenu::FItem{.label = "Paste", .action = [log] { log("Menu: Paste"); }},
        });
    });
    _menuBar->addItem("View", [log]
    {
        return ya::UIMenu::create({
            ya::UIMenu::FItem{.label = "Show Grid", .action = [log] { log("Menu: Show Grid"); }},
            ya::UIMenu::FItem{.label = "Show FPS", .action = [log] { log("Menu: Show FPS"); }},
            ya::UIMenu::FItem{.label = "Fullscreen", .action = [log] { log("Menu: Fullscreen"); }},
        });
    });
    _menuBar->addItem("Help", [log]
    {
        return ya::UIMenu::create({
            ya::UIMenu::FItem{.label = "About", .action = [log] { log("Menu: About"); }},
            ya::UIMenu::FItem{.label = "Documentation", .action = [log] { log("Menu: Documentation"); }},
        });
    });

    // The bar items resolve the "menubar" style from the mounted
    // WorkbenchTheme (Phase 4): its normal/hovered stops are lifted above the
    // window background so hover is visible. No per-item color writes — the
    // shell no longer traverses children to re-style controls.
}

void FWorkbenchSurface::buildTabBar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _tabBar = std::make_shared<ya::UITabBar>("DemoTabs");
    _tabBar->_anchorMin = {0.0f, 0.0f};
    _tabBar->_anchorMax = {1.0f, 1.0f};
    _tabBar->setPosition({0.0f, 0.0f});
    _tabBar->setSize({0.0f, 0.0f});
    _tabBar->setDirection(ya::EWidgetBoxLayout::Vertical);
    _tabBar->setSpacing(4.0f);
    _tabBar->setPadding({10.0f, 10.0f});
    _tabBar->_styleKey = "tab.sidebar";
    tree.attach(parent, _tabBar);

    for (const FPage& page : _pages) {
        _tabBar->addTab(page.name);
    }
    _tabBar->addTab("Editor");
    _editorPageIndex = static_cast<int>(_pages.size());
    _tabBar->_onTabSelected = [this](int index) { selectPage(index); };
}

void FWorkbenchSurface::buildWorkspaceShell(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _workspaceSplit = std::make_shared<ya::UISplitPane>("WorkbenchShellSplit");
    _workspaceSplit->_anchorMin = {0.0f, 0.0f};
    _workspaceSplit->_anchorMax = {1.0f, 1.0f};
    // TODO: remove hardcode postion
    _workspaceSplit->setPosition({0.0f, 34.0f});
    _workspaceSplit->setSize({0.0f, -38.0f});
    _workspaceSplit->setSplitRatio(0.23f);
    _workspaceSplit->setMinFirstExtent(208.0f);
    _workspaceSplit->setMinSecondExtent(520.0f);
    tree.attach(parent, _workspaceSplit);

    buildPageRail(tree, *_workspaceSplit);
    // The demo host must exist before the tab bar selects its first page:
    // selectTab() fires the page-switch callback synchronously.
    buildDemoHost(tree, *_workspaceSplit);
}

void FWorkbenchSurface::buildPageRail(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _pageRail = std::make_shared<ya::UIPanel>("FeatureRail");
    _pageRail->_anchorMin = {0.0f, 0.0f};
    _pageRail->_anchorMax = {1.0f, 1.0f};
    _pageRail->setPosition({0.0f, 0.0f});
    _pageRail->setSize({0.0f, 0.0f});
    _pageRail->_styleKey = "panel.sidebar";
    tree.attach(parent, _pageRail);

    _pageRailTitle = std::make_shared<ya::UIText>("FeatureRailTitle");
    _pageRailTitle->_anchorMin = {0.0f, 0.0f};
    _pageRailTitle->_anchorMax = {1.0f, 0.0f};
    _pageRailTitle->setPosition({16.0f, 14.0f});
    _pageRailTitle->setSize({-32.0f, 18.0f});
    _pageRailTitle->_fontSize = 10;
    _pageRailTitle->setColor(kHeaderColor);
    _pageRailTitle->setText("FEATURE GALLERY");
    tree.attach(*_pageRail, _pageRailTitle);

    _pageRailCard = std::make_shared<ya::UIPanel>("FeatureRailCard");
    _pageRailCard->_anchorMin = {0.0f, 0.0f};
    _pageRailCard->_anchorMax = {1.0f, 1.0f};
    _pageRailCard->setPosition({10.0f, 46.0f});
    _pageRailCard->setSize({-20.0f, -12.0f});
    _pageRailCard->_styleKey = "panel.sidebar.card";
    tree.attach(*_pageRail, _pageRailCard);

    buildTabBar(tree, *_pageRailCard);
}

void FWorkbenchSurface::buildDemoHost(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _contentFrame = std::make_shared<ya::UIPanel>("DemoContentFrame");
    _contentFrame->_anchorMin = {0.0f, 0.0f};
    _contentFrame->_anchorMax = {1.0f, 1.0f};
    _contentFrame->setPosition({0.0f, 0.0f});
    _contentFrame->setSize({0.0f, 0.0f});
    _contentFrame->_styleKey = "panel.window";
    tree.attach(parent, _contentFrame);

    _demoHost = std::make_shared<ya::UIPanel>("DemoHost");
    _demoHost->_anchorMin = {0.0f, 0.0f};
    _demoHost->_anchorMax = {1.0f, 1.0f};
    _demoHost->setPosition({14.0f, 12.0f});
    _demoHost->setSize({-28.0f, -24.0f});
    _demoHost->_styleKey = "panel.surface";
    tree.attach(*_contentFrame, _demoHost);
}

void FWorkbenchSurface::buildStatusBar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _statusText = std::make_shared<ya::UIText>("Status");
    _statusText->_anchorMin = {0.0f, 1.0f};
    _statusText->_anchorMax = {0.0f, 1.0f};
    _statusText->setPosition({12.0f, -30.0f});
    _statusText->setSize({520.0f, 24.0f});
    _statusText->_fontSize  = 13;
    _statusText->setText("Tab: switch demo | Click / drag / keyboard to explore");
    _statusText->setColor(kHeaderColor);
    tree.attach(parent, _statusText);

    _commandResultText = std::make_shared<ya::UIText>("CommandResult");
    // Bottom full-width strip: right-aligned status text that never leaves
    // the window on resize (a corner anchor with a pixel position would).
    _commandResultText->_anchorMin = {0.0f, 1.0f};
    _commandResultText->_anchorMax = {1.0f, 1.0f};
    _commandResultText->setPosition({0.0f, -30.0f});
    _commandResultText->setSize({0.0f, 24.0f});
    _commandResultText->_fontSize  = 13;
    _commandResultText->setText("Ready");
    _commandResultText->setColor({0.60f, 0.80f, 0.62f, 1.0f});
    _commandResultText->_hAlign    = ya::EWidgetAlignH::Right;
    tree.attach(parent, _commandResultText);
}

void FWorkbenchSurface::selectPage(int index)
{
    const int pageCount = static_cast<int>(_pages.size()) + 1; // + built-in Editor
    if (index < 0 || index >= pageCount || index == _currentPageIndex) {
        return;
    }

    if (_tabBar) {
        _tabBar->syncSelectedTab(index);
    }
    _currentPageIndex = index;
    clearDemoHost();

    if (index == _editorPageIndex) {
        buildEditorDemo(*_tree, *_demoHost);
    }
    else {
        FPage& page = _pages[static_cast<size_t>(index)];
        const auto log = [this](const std::string& text) { logStatus(text); };
        page.build(*_tree, *_demoHost, log);
    }

    if (_tree) {
        _tree->invalidateLayout();
    }
}

void FWorkbenchSurface::clearDemoHost()
{
    if (!_tree || !_demoHost) {
        return;
    }
    const auto children = _demoHost->getChildren();
    for (const auto& child : children) {
        if (child->isAttached()) {
            _tree->detach(*child);
        }
    }
}

void FWorkbenchSurface::logStatus(const std::string& text)
{
    if (_commandResultText) {
        _commandResultText->setText(text);
    }
}

const std::string& FWorkbenchSurface::getStatusText() const
{
    static const std::string kEmpty;
    return _commandResultText ? _commandResultText->getText() : kEmpty;
}

// === Editor demo page (the original workbench editor loop) ===

void FWorkbenchSurface::buildEditorDemo(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto toolButton = [](std::string name, const std::string& label, float width = 0.0f)
    {
        auto button = ya::ui::button(name).child(
            ya::ui::text(name + "_Label")
                .setText(label)
                .setFontSize(14)
                .setVisibility(ya::EWidgetVisibility::SelfHitTestInvisible)
                .setHAlign(ya::EWidgetAlignH::Center)
                .setVAlign(ya::EWidgetAlignV::Center));
        if (width > 0.0f) {
            return std::move(button).setSize({width, 24.0f});
        }
        return std::move(button).setContentPadding({10.0f, 4.0f});
    };
    auto headerText = [](const std::string& text)
    {
        return ya::ui::text(text + "_Header")
            .setText(text)
            .setFontSize(13)
            .setColor(kHeaderColor)
            .setSize({200.0f, 20.0f});
    };

    auto addButton = toolButton("Add", "Add").setOnClick([this] { cmdAdd(); });
    _addButton     = addButton.share();
    auto removeButton = toolButton("Remove", "Remove").setOnClick([this] { cmdRemove(); });
    _removeButton     = removeButton.share();
    auto renameButton = toolButton("Rename", "Rename").setOnClick([this] { cmdRename(); });
    _renameButton     = renameButton.share();
    auto resetButton  = toolButton("ResetLayout", "Reset Layout").setOnClick([this] { cmdResetLayout(); });
    _resetButton      = resetButton.share();

    auto toolbar = ya::ui::row("Toolbar")
                       .setAnchors({0.0f, 0.0f}, {1.0f, 0.0f})
                       .setPosition({0.0f, 6.0f})
                       .setSize({0.0f, 32.0f})
                       .setSpacing(8.0f)
                       .setPadding({8.0f, 4.0f})
                       .children(std::move(addButton),
                                 std::move(removeButton),
                                 std::move(renameButton),
                                 std::move(resetButton));

    auto rowList = ya::ui::column("RowList").fillParent().setPadding({8.0f, 8.0f}).setSpacing(2.0f);
    _rowList     = rowList.share();
    auto scroll  = ya::ui::scroll("ItemScroll")
                      .fillParent()
                      .setPosition({0.0f, 34.0f})
                      .setSize({0.0f, 0.0f})
                      .child(std::move(rowList));
    _rowScroll = scroll.share();
    auto listPanel = ya::ui::panel("ItemList")
                         .fillParent()
                         .setStyleKey("panel")
                         .child(headerText("ITEMS").setPosition({10.0f, 8.0f}))
                         .child(std::move(scroll));
    _listPanel = listPanel.share();

    auto previewName = ya::ui::text("PreviewName")
                           .setText("(no selection)")
                           .setFontSize(15)
                           .setColor({0.95f, 0.96f, 0.98f, 1.0f})
                           .setHAlign(ya::EWidgetAlignH::Center)
                           .setPosition({0.0f, 0.0f})
                           .setSize({400.0f, 24.0f});
    _previewName = previewName.share();
    auto highlight = ya::ui::panel("SelectionHighlight")
                         .setAnchors({0.5f, 0.5f}, {0.5f, 0.5f})
                         .setPosition({-70.0f, -45.0f})
                         .setSize({140.0f, 90.0f})
                         .setColor({0.35f, 0.55f, 0.90f, 1.0f})
                         .child(std::move(previewName));
    _highlightPanel = highlight.share();
    auto canvas     = ya::ui::panel("PreviewCanvas")
                      .fillParent()
                      .setStyleKey("panel.canvas")
                      .child(headerText("PREVIEW").setPosition({10.0f, 8.0f}))
                      .child(std::move(highlight));
    _canvasPanel = canvas.share();

    auto nameField = ya::ui::textField("NameField")
                         .setSize({220.0f, 26.0f})
                         .setFontSize(14)
                         .setOnCommit(
                             [this](const std::string& text)
                             {
                                 workspace.renameSelected(text);
                                 setCommandResult(workspace.commandResult);
                             });
    _nameField         = nameField.share();
    auto visibleToggle = toolButton("VisibleToggle", "Visible: on", 110.0f)
                             .setOnClick(
                                 [this]
                                 {
                                     workspace.toggleSelectedVisible();
                                     setCommandResult(workspace.commandResult);
                                 });
    _visibleToggle = visibleToggle.share();
    auto colorCycle = toolButton("ColorCycle", "Cycle Color", 110.0f)
                          .setOnClick(
                              [this]
                              {
                                  workspace.cycleSelectedColor();
                                  setCommandResult(workspace.commandResult);
                              });
    _colorCycle = colorCycle.share();
    auto colorValue = ya::ui::text("ColorValue").setSize({220.0f, 14.0f}).setFontSize(12).setText("").setColor(kHeaderColor);
    _colorValue     = colorValue.share();
    auto sizeGrow   = toolButton("SizeGrow", "Grow +20", 90.0f)
                        .setOnClick(
                            [this]
                            {
                                workspace.stepSelectedSize({20.0f, 20.0f});
                                setCommandResult(workspace.commandResult);
                            });
    _sizeGrow    = sizeGrow.share();
    auto sizeShrink = toolButton("SizeShrink", "Shrink -20", 100.0f)
                          .setOnClick(
                              [this]
                              {
                                  workspace.stepSelectedSize({-20.0f, -20.0f});
                                  setCommandResult(workspace.commandResult);
                              });
    _sizeShrink = sizeShrink.share();
    auto sizeValue = ya::ui::text("SizeValue").setSize({220.0f, 14.0f}).setFontSize(12).setText("").setColor(kHeaderColor);
    _sizeValue     = sizeValue.share();

    auto inspector = ya::ui::panel("Inspector")
                         .fillParent()
                         .setStyleKey("panel")
                         .child(ya::ui::column("InspectorForm")
                                    .fillParent()
                                    .setSize({0.0f, 0.0f})
                                    .setPadding({10.0f, 8.0f})
                                    .setSpacing(4.0f)
                                    .children(headerText("INSPECTOR"),
                                              headerText("Name"),
                                              std::move(nameField),
                                              headerText("Visible"),
                                              std::move(visibleToggle),
                                              headerText("Color"),
                                              std::move(colorCycle),
                                              std::move(colorValue),
                                              headerText("Size"),
                                              ya::ui::row("SizeRow")
                                                  .setSpacing(6.0f)
                                                  .children(std::move(sizeGrow), std::move(sizeShrink)),
                                              std::move(sizeValue)));

    auto rightSplit = ya::ui::splitPane("RightSplit")
                          .fillParent()
                          .setSplitRatio(0.66f)
                          .setMinFirstExtent(240.0f)
                          .setMinSecondExtent(220.0f)
                          .children(std::move(canvas), std::move(inspector));
    _rightSplit = rightSplit.share();

    auto mainSplit = ya::ui::splitPane("MainSplit")
                         .fillParent()
                         .setSize({0.0f, 0.0f})
                         .setPadding({0.0f, 42.0f})
                         .setSplitRatio(0.24f)
                         .setMinFirstExtent(180.0f)
                         .setMinSecondExtent(420.0f)
                         .children(std::move(listPanel), std::move(rightSplit));
    _mainSplit = mainSplit.share();

    auto page = ya::ui::panel("EditorDemo")
                    .fillParent()
                    .setStyleKey("panel.window")
                    .children(std::move(toolbar), std::move(mainSplit));
    ya::ui::build(tree, parent, std::move(page));

    workspace.resetLayout();
    _bRowsDirty = true;
}

void FWorkbenchSurface::rebuildItemRows()
{
    if (_rowList && _rowList->isAttached()) {
        _tree->detach(*_rowList);
    }
    _rowList = std::make_shared<ya::UIContainer>("RowList");
    _rowList->_anchorMin = {0.0f, 0.0f};
    _rowList->_anchorMax = {1.0f, 1.0f};
    _rowList->setPadding({8.0f, 8.0f});
    _rowList->setDirection(ya::EWidgetBoxLayout::Vertical);
    _rowList->setSpacing(2.0f);
    _tree->attach(*_rowScroll, _rowList);
    _rows.clear();

    for (const FWorkbenchItem* itemPtr : workspace.orderedItems()) {
        const FWorkbenchItem& item = *itemPtr;
        auto row = std::make_shared<ya::UISelectableRow>("Row_" + item.id);
        row->_itemId = item.id;
        row->setSize({240.0f, 22.0f});
        row->_onSelect = [this](const std::string& id) {
            workspace.select(id);
            setCommandResult("List: selected '" + id + "'");
        };
        row->_onActivate = [this](const std::string& id) {
            workspace.select(id);
            setCommandResult("List: activated '" + id + "'");
        };
        // Drag & drop reparent: rows are both sources and drop targets.
        row->setDraggable(true);
        row->setDragPayload(item.id);
        row->setDragGhostLabel(item.name);
        const std::string targetId = item.id;
        row->setOnDropHandler([this, targetId](const std::string& droppedId) {
            if (workspace.reparent(droppedId, targetId)) {
                _bRowsDirty = true;
                setCommandResult(workspace.commandResult);
            }
        });

        auto label = std::make_shared<ya::UIText>("RowLabel_" + item.id);
        label->setSize({240.0f, 22.0f});
        label->_fontSize = 13;
        label->setText(item.bVisible ? item.name : item.name + " (hidden)");
        label->setStyleKey("text");
        label->_vAlign   = ya::EWidgetAlignV::Center;
        // Tree indentation: one level per parent depth.
        label->setPosition({static_cast<float>(workspace.getDepth(item.id)) * 14.0f, 0.0f});

        _tree->attach(*_rowList, row);
        _tree->attach(*row, label);
        _rows.push_back(row);
    }
    _bRowsDirty = false;
}

void FWorkbenchSurface::syncPresentationState()
{
    if (!_tree || _currentPageIndex != _editorPageIndex) {
        return;
    }
    if (_bRowsDirty) {
        rebuildItemRows();
    }

    const FWorkbenchItem* selected = workspace.getSelected();

    const auto ordered = workspace.orderedItems();
    for (size_t i = 0; i < _rows.size() && i < ordered.size(); ++i) {
        const FWorkbenchItem& item = *ordered[i];
        if (!_rows[i]->getChildren().empty()) {
            if (auto* label = dynamic_cast<ya::UIText*>(_rows[i]->getChildren()[0].get())) {
                label->setText(item.bVisible ? item.name : item.name + " (hidden)");
            }
        }
        _rows[i]->setSelected(selected != nullptr && _rows[i]->_itemId == selected->id);
    }

    if (selected) {
        _highlightPanel->setVisibility(selected->bVisible ? ya::EWidgetVisibility::Visible
                                                          : ya::EWidgetVisibility::Hidden);
        _highlightPanel->setSize(selected->size);
        _highlightPanel->setPosition(-selected->size * 0.5f);
        _highlightPanel->setColor(selected->color);
        _previewName->setText(selected->bVisible ? selected->name : selected->name + " (hidden)");
    } else {
        _highlightPanel->setVisibility(ya::EWidgetVisibility::Hidden);
        _previewName->setText("(no selection)");
    }

    if (_tree->getFocused() != _nameField.get()) {
        _nameField->setText(selected ? selected->name : "");
        _nameField->clampCursor();
    }
    _colorValue->setText(selected ? std::format("rgba({:.2f}, {:.2f}, {:.2f})",
                                                selected->color.r, selected->color.g, selected->color.b)
                                  : "-");
    _sizeValue->setText(selected ? std::format("{} x {}",
                                               static_cast<int>(selected->size.x),
                                               static_cast<int>(selected->size.y))
                                 : "-");
    if (!_visibleToggle->getChildren().empty()) {
        if (auto* label = dynamic_cast<ya::UIText*>(_visibleToggle->getChildren()[0].get())) {
            label->setText((selected && selected->bVisible) ? "Visible: on" : "Visible: off");
        }
    }
}

void FWorkbenchSurface::cmdAdd()
{
    workspace.addItem(std::format("Item {}", workspace.items.size() + 1));
    _bRowsDirty = true;
    setCommandResult(workspace.commandResult);
}

void FWorkbenchSurface::cmdRemove()
{
    workspace.removeSelected();
    _bRowsDirty = true;
    setCommandResult(workspace.commandResult);
}

void FWorkbenchSurface::cmdRename()
{
    workspace.renameSelected(_nameField->_text);
    setCommandResult(workspace.commandResult);
}

void FWorkbenchSurface::cmdResetLayout()
{
    workspace.resetLayout();
    _bRowsDirty = true;
    setCommandResult(workspace.commandResult);
}

void FWorkbenchSurface::setCommandResult(const std::string& text)
{
    workspace.commandResult = text;
    logStatus(text);
}

void FWorkbenchSurface::updateUI()
{
    if (!_root || !_root->isAttached()) {
        ++_frame;
        return;
    }

    syncPresentationState();
    logWorkbenchRectOnce("PreviewCanvas", _canvasPanel.get());
    logWorkbenchRectOnce("SelectionHighlight", _highlightPanel.get());
    logWorkbenchRectOnce("PreviewName", _previewName.get());
    ++_frame;
    if (_bSmokeActions && !_bAutomationDone) {
        runAutomation();
    }
}

void FWorkbenchSurface::onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result)
{
    if (result == ya::EWidgetRouteResult::NotHandled &&
        event.getEventType() == ya::EEvent::KeyPressed) {
        handleUnhandledKey(static_cast<const ya::KeyPressedEvent&>(event));
    }
}

void FWorkbenchSurface::handleUnhandledKey(const ya::KeyPressedEvent& keyEvent)
{
    if (_currentPageIndex != _editorPageIndex) {
        return;
    }
    if (keyEvent.bRepeat) {
        return;
    }
    if (keyEvent._keyCode == ya::EKey::Down) {
        workspace.selectRelative(1);
        setCommandResult("List: navigated");
    } else if (keyEvent._keyCode == ya::EKey::Up) {
        workspace.selectRelative(-1);
        setCommandResult("List: navigated");
    }
}

void FWorkbenchSurface::dispatchPointer(const ya::Event& event, const glm::vec2& point)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = point;
    _tree->dispatchEvent(event, ctx);
}

void FWorkbenchSurface::dispatchKey(const ya::Event& event)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = {-1.0f, -1.0f};
    onRoutedEvent(event, _tree->dispatchEvent(event, ctx));
}

void FWorkbenchSurface::runAutomation()
{
    // App-registered demo pages drive their own automation: frames the app
    // claims (returns true) are handled entirely there; everything else falls
    // through to the built-in Editor page automation below.
    if (externalAutomationStep && externalAutomationStep(_frame)) {
        return;
    }

    auto failAutomation = [this](const std::string& message) {
        YA_CORE_ERROR("{}", message);
        _bAutomationDone = true;
    };
    const auto centerOf = [](const ya::UIElement* element) -> glm::vec2
    {
        return element ? element->_layoutRect.pos + element->_layoutRect.extent * 0.5f : glm::vec2{};
    };
    const auto click = [this, &centerOf](const ya::UIElement* element)
    {
        const glm::vec2 center = centerOf(element);
        dispatchPointer(ya::MouseButtonPressedEvent(ya::EMouse::Left), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(ya::EMouse::Left), center);
    };
    switch (_frame) {
    case 21: {
        // Editor: Add.
        click(_addButton.get());
        if (workspace.items.size() != 4u || workspace.getSelected() == nullptr ||
            workspace.getSelected()->name != "Item 4") {
            failAutomation(std::format("Demo automation: editor Add failed (items={}, selected='{}')",
                                       workspace.items.size(),
                                       workspace.getSelected() ? workspace.getSelected()->name : "<none>"));
            return;
        }
        break;
    }
    case 22: {
        // Editor: rename through the inspector field.
        click(_nameField.get());
        ya::KeyPressedEvent home{};
        home._keyCode = ya::EKey::Home;
        home._mod     = 0;
        dispatchKey(home);
        ya::KeyTypedEvent typed("Star_");
        typed._mod = 0;
        dispatchKey(typed);
        ya::KeyPressedEvent enter{};
        enter._keyCode = ya::EKey::Enter;
        enter._mod     = 0;
        dispatchKey(enter);
        const FWorkbenchItem* selected = workspace.getSelected();
        if (!selected || selected->name != "Star_Item 4") {
            failAutomation(std::format("Demo automation: editor rename failed ('{}')", selected ? selected->name : "<none>"));
            return;
        }
        break;
    }
    case 23: {
        // Editor: drag the first row onto the third -> reparent under it.
        const glm::vec2 from = centerOf(_rows[0].get());
        const glm::vec2 to   = centerOf(_rows[2].get());
        dispatchPointer(ya::MouseButtonPressedEvent(ya::EMouse::Left), from);
        dispatchPointer(ya::MouseMoveEvent(to.x, to.y), to);
        dispatchPointer(ya::MouseButtonReleasedEvent(ya::EMouse::Left), to);
        if (workspace.items[0].parentId != "item.light") {
            failAutomation(std::format("Demo automation: drag reparent failed (parent='{}')",
                                       workspace.items[0].parentId));
            return;
        }
        break;
    }
    case 24: {
        // The row rebuild (triggered by the reparent) must keep the tree
        // structure: depth re-derives from the stored parent links.
        if (workspace.items[0].parentId != "item.light" || workspace.getDepth("item.cube") != 1) {
            failAutomation("Demo automation: reparent state lost after row rebuild");
            return;
        }
        _bSmokePassed    = true;
        _bAutomationDone = true;
        YA_CORE_INFO("Workbench automation PASSED: render baseline (app) -> demo pages (app) -> editor (shell, incl. drag reparent)");
        break;
    }
    default:
        break;
    }
}

} // namespace guiworkbench

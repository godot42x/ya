#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GUI/Tooling/Workbench/WorkbenchTheme.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
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
    // Shell chrome resolves its fill from the mounted theme (Phase 4): the
    // window key drives the root backdrop; no authored color so the
    // white/dark toggle restyles the whole shell.
    _root->_styleKey = "panel.window";
    ya::FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
    const ya::WidgetAttachment attached = tree.attachToLayer(ya::WidgetTree::ELayer::Content, _root, fillArgs);
    YA_CORE_ASSERT(attached.valid(), "WorkbenchSurface: failed to attach WorkbenchRoot");

    assembleChrome(tree, *_root);
}

void FWorkbenchSurface::buildUI(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _tree = &tree;

    _root = std::make_shared<ya::UIPanel>("WorkbenchRoot");
    _root->_styleKey = "panel.window";
    tree.attach(parent, _root);
    ya::ui::attachSlot(parent, *_root, ya::ui::canvasSlot().fill());

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
    tree.attach(parent, _menuBar);
    // Layout intent lives on the parent->child edge: full width, fixed height,
    // pinned to the top of the canvas host.
    ya::ui::attachSlot(parent, *_menuBar,
                       ya::ui::canvasSlot()
                           .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                           .size({0.0f, 30.0f}));

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
    _tabBar->setDirection(ya::EWidgetBoxLayout::Vertical);
    _tabBar->setSpacing(4.0f);
    _tabBar->setPadding({10.0f, 10.0f});
    _tabBar->_styleKey = "tab.sidebar";
    tree.attach(parent, _tabBar);
    ya::ui::attachSlot(parent, *_tabBar, ya::ui::canvasSlot().fill());

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
    _workspaceSplit->setSplitRatio(0.23f);
    _workspaceSplit->setMinFirstExtent(208.0f);
    _workspaceSplit->setMinSecondExtent(520.0f);
    tree.attach(parent, _workspaceSplit);
    // TODO: remove hardcode offset (menu bar + status bar chrome height).
    ya::ui::attachSlot(parent, *_workspaceSplit,
                       ya::ui::canvasSlot().fill().offset({0.0f, 34.0f}));

    buildPageRail(tree, *_workspaceSplit);
    // The demo host must exist before the tab bar selects its first page:
    // selectTab() fires the page-switch callback synchronously.
    buildDemoHost(tree, *_workspaceSplit);
}

void FWorkbenchSurface::buildPageRail(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _pageRail = std::make_shared<ya::UIPanel>("FeatureRail");
    _pageRail->_styleKey = "panel.sidebar";
    tree.attach(parent, _pageRail);

    _pageRailTitle = std::make_shared<ya::UIText>("FeatureRailTitle");
    _pageRailTitle->_fontSize = 10;
    _pageRailTitle->setColor(kHeaderColor);
    _pageRailTitle->setText("FEATURE GALLERY");
    tree.attach(*_pageRail, _pageRailTitle);
    ya::ui::attachSlot(*_pageRail, *_pageRailTitle,
                       ya::ui::canvasSlot()
                           .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                           .offset({16.0f, 14.0f})
                           .size({0.0f, 18.0f}));

    _pageRailCard = std::make_shared<ya::UIPanel>("FeatureRailCard");
    _pageRailCard->_styleKey = "panel.sidebar.card";
    tree.attach(*_pageRail, _pageRailCard);
    ya::ui::attachSlot(*_pageRail, *_pageRailCard,
                       ya::ui::canvasSlot().fill().offset({10.0f, 46.0f}));

    buildTabBar(tree, *_pageRailCard);
}

void FWorkbenchSurface::buildDemoHost(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _contentFrame = std::make_shared<ya::UIPanel>("DemoContentFrame");
    _contentFrame->_styleKey = "panel.window";
    tree.attach(parent, _contentFrame);

    _demoHost = std::make_shared<ya::UIPanel>("DemoHost");
    _demoHost->_styleKey = "panel.surface";
    tree.attach(*_contentFrame, _demoHost);
    ya::ui::attachSlot(*_contentFrame, *_demoHost,
                       ya::ui::canvasSlot().fill().offset({14.0f, 12.0f}));
}

void FWorkbenchSurface::buildStatusBar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _statusText = std::make_shared<ya::UIText>("Status");
    _statusText->_fontSize  = 13;
    _statusText->setText("Tab: switch demo | Click / drag / keyboard to explore");
    _statusText->setColor(kHeaderColor);
    tree.attach(parent, _statusText);
    // Bottom-left corner anchor: no span, so the slot size is honoured and
    // the slot offset pins it above the bottom edge.
    ya::ui::attachSlot(parent, *_statusText,
                       ya::ui::canvasSlot()
                           .anchor({0.0f, 1.0f}, {0.0f, 1.0f})
                           .offset({12.0f, -30.0f})
                           .size({520.0f, 24.0f}));

    _commandResultText = std::make_shared<ya::UIText>("CommandResult");
    // Bottom full-width strip: right-aligned status text that never leaves
    // the window on resize (a corner anchor with a pixel position would).
    _commandResultText->_fontSize  = 13;
    _commandResultText->setText("Ready");
    _commandResultText->setColor({0.60f, 0.80f, 0.62f, 1.0f});
    _commandResultText->_hAlign    = ya::EWidgetAlignH::Right;
    tree.attach(parent, _commandResultText);
    ya::ui::attachSlot(parent, *_commandResultText,
                       ya::ui::canvasSlot()
                           .anchor({0.0f, 1.0f}, {1.0f, 1.0f})
                           .offset({0.0f, -30.0f})
                           .size({0.0f, 24.0f}));
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
    auto toolButton = [](std::string name, const std::string& label)
    {
        auto button = ya::ui::button(name).child(
            ya::ui::text(name + "_Label")
                .setText(label)
                .setFontSize(14)
                .setVisibility(ya::EWidgetVisibility::SelfHitTestInvisible)
                .setHAlign(ya::EWidgetAlignH::Center)
                .setVAlign(ya::EWidgetAlignV::Center));
        return std::move(button).setContentPadding({10.0f, 4.0f});
    };
    auto headerText = [](const std::string& text)
    {
        return ya::ui::text(text + "_Header")
            .setText(text)
            .setFontSize(13)
            .setColor(kHeaderColor);
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
                       .setSpacing(8.0f)
                       .setPadding({8.0f, 4.0f})
                       .children(std::move(addButton),
                                 std::move(removeButton),
                                 std::move(renameButton),
                                 std::move(resetButton));

    auto rowList = ya::ui::column("RowList").setPadding({8.0f, 8.0f}).setSpacing(2.0f);
    _rowList     = rowList.share();
    auto scroll  = ya::ui::scroll("ItemScroll")
                      .child(std::move(rowList));
    _rowScroll = scroll.share();
    auto listPanel = ya::ui::panel("ItemList")
                         .setStyleKey("panel")
                         .child(headerText("ITEMS"), ya::ui::canvasSlot().offset({10.0f, 8.0f}).size({200.0f, 20.0f}))
                         .child(std::move(scroll), ya::ui::canvasSlot().fill().offset({0.0f, 34.0f}));
    _listPanel = listPanel.share();

    auto previewName = ya::ui::text("PreviewName")
                           .setText("(no selection)")
                           .setFontSize(15)
                           .setColor({0.95f, 0.96f, 0.98f, 1.0f})
                           .setHAlign(ya::EWidgetAlignH::Center);
    _previewName = previewName.share();
    auto highlight = ya::ui::panel("SelectionHighlight")
                         .setColor({0.35f, 0.55f, 0.90f, 1.0f})
                         .child(std::move(previewName), ya::ui::canvasSlot().size({400.0f, 24.0f}));
    _highlightPanel = highlight.share();
    auto canvas     = ya::ui::panel("PreviewCanvas")
                      .setStyleKey("panel.canvas")
                      .child(headerText("PREVIEW"), ya::ui::canvasSlot().offset({10.0f, 8.0f}).size({200.0f, 20.0f}))
                      .child(std::move(highlight), ya::ui::canvasSlot()
                           .anchor({0.5f, 0.5f}, {0.5f, 0.5f})
                           .offset({-70.0f, -45.0f})
                           .size({140.0f, 90.0f}));
    _canvasPanel = canvas.share();

    auto nameField = ya::ui::textField("NameField")
                         .setFontSize(14)
                         .setOnCommit(
                             [this](const std::string& text)
                             {
                                 workspace.renameSelected(text);
                                 setCommandResult(workspace.commandResult);
                             });
    _nameField         = nameField.share();
    auto visibleToggle = toolButton("VisibleToggle", "Visible: on")
                             .setOnClick(
                                 [this]
                                 {
                                     workspace.toggleSelectedVisible();
                                     setCommandResult(workspace.commandResult);
                                 });
    _visibleToggle = visibleToggle.share();
    auto colorCycle = toolButton("ColorCycle", "Cycle Color")
                          .setOnClick(
                              [this]
                              {
                                  workspace.cycleSelectedColor();
                                  setCommandResult(workspace.commandResult);
                              });
    _colorCycle = colorCycle.share();
    auto colorValue = ya::ui::text("ColorValue").setFontSize(12).setText("").setColor(kHeaderColor);
    _colorValue     = colorValue.share();
    auto sizeGrow   = toolButton("SizeGrow", "Grow +20")
                        .setOnClick(
                            [this]
                            {
                                workspace.stepSelectedSize({20.0f, 20.0f});
                                setCommandResult(workspace.commandResult);
                            });
    _sizeGrow    = sizeGrow.share();
    auto sizeShrink = toolButton("SizeShrink", "Shrink -20")
                          .setOnClick(
                              [this]
                              {
                                  workspace.stepSelectedSize({-20.0f, -20.0f});
                                  setCommandResult(workspace.commandResult);
                              });
    _sizeShrink = sizeShrink.share();
    auto sizeValue = ya::ui::text("SizeValue").setFontSize(12).setText("").setColor(kHeaderColor);
    _sizeValue     = sizeValue.share();

    auto inspectorForm = ya::ui::column("InspectorForm")
                                    .setPadding({10.0f, 8.0f})
                                    .setSpacing(4.0f)
                                    .child(headerText("INSPECTOR"), ya::ui::boxSlot().preferredSize({200.0f, 20.0f}))
                                    .child(headerText("Name"), ya::ui::boxSlot().preferredSize({200.0f, 20.0f}))
                                    .child(std::move(nameField), ya::ui::boxSlot().preferredSize({220.0f, 26.0f}))
                                    .child(headerText("Visible"), ya::ui::boxSlot().preferredSize({200.0f, 20.0f}))
                                    .child(std::move(visibleToggle), ya::ui::boxSlot().preferredSize({110.0f, 24.0f}))
                                    .child(headerText("Color"), ya::ui::boxSlot().preferredSize({200.0f, 20.0f}))
                                    .child(std::move(colorCycle), ya::ui::boxSlot().preferredSize({110.0f, 24.0f}))
                                    .child(std::move(colorValue), ya::ui::boxSlot().preferredSize({220.0f, 14.0f}))
                                    .child(headerText("Size"), ya::ui::boxSlot().preferredSize({200.0f, 20.0f}))
                                    .child(ya::ui::row("SizeRow")
                                                  .setSpacing(6.0f)
                                                  .child(std::move(sizeGrow), ya::ui::boxSlot().preferredSize({90.0f, 24.0f}))
                                                  .child(std::move(sizeShrink), ya::ui::boxSlot().preferredSize({100.0f, 24.0f})))
                                    .child(std::move(sizeValue), ya::ui::boxSlot().preferredSize({220.0f, 14.0f}));
    auto inspector = ya::ui::panel("Inspector")
                         .setStyleKey("panel")
                         .child(std::move(inspectorForm), ya::ui::canvasSlot().fill());

    auto rightSplit = ya::ui::splitPane("RightSplit")
                          .setSplitRatio(0.66f)
                          .setMinFirstExtent(240.0f)
                          .setMinSecondExtent(220.0f)
                          .children(std::move(canvas), std::move(inspector));
    _rightSplit = rightSplit.share();

    auto mainSplit = ya::ui::splitPane("MainSplit")
                         .setPadding({0.0f, 42.0f})
                         .setSplitRatio(0.24f)
                         .setMinFirstExtent(180.0f)
                         .setMinSecondExtent(420.0f)
                         .children(std::move(listPanel), std::move(rightSplit));
    _mainSplit = mainSplit.share();

    auto page = ya::ui::panel("EditorDemo")
                    .setStyleKey("panel.window")
                    .child(std::move(toolbar), ya::ui::canvasSlot()
                         .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                         .offset({0.0f, 6.0f})
                         .size({0.0f, 32.0f}))
                    .child(std::move(mainSplit), ya::ui::canvasSlot().fill());
    // The editor page is mounted into DemoHost, whose default layout is a
    // canvas host. The page root therefore must declare its own fill edge at
    // attach time; otherwise the default canvas slot leaves the whole editor
    // subtree at 0x0 and later children can still spill over the toolbar.
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());

    workspace.resetLayout();
    _bRowsDirty = true;
}

void FWorkbenchSurface::rebuildItemRows()
{
    if (_rowList && _rowList->isAttached()) {
        _tree->detach(*_rowList);
    }
    _rowList = std::make_shared<ya::UIContainer>("RowList");
    _rowList->setPadding({8.0f, 8.0f});
    _rowList->setDirection(ya::EWidgetBoxLayout::Vertical);
    _rowList->setSpacing(2.0f);
    _tree->attach(*_rowScroll, _rowList);
    _rows.clear();

    for (const FWorkbenchItem* itemPtr : workspace.orderedItems()) {
        const FWorkbenchItem& item = *itemPtr;
        auto row = std::make_shared<ya::UISelectableRow>("Row_" + item.id);
        row->_itemId = item.id;
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
        label->_fontSize = 13;
        label->setText(item.bVisible ? item.name : item.name + " (hidden)");
        label->setStyleKey("text");
        label->_vAlign   = ya::EWidgetAlignV::Center;

        _tree->attach(*_rowList, row);
        if (auto* slot = dynamic_cast<ya::UIBoxSlot*>(_rowList->getSlotForChild(*row))) {
            slot->setPreferredSize({240.0f, 22.0f});
        }
        row->getContentLayout().setPadding(
            ya::FMargin{static_cast<float>(workspace.getDepth(item.id)) * 14.0f, 0.0f, 0.0f, 0.0f});
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
        if (auto* slot = _canvasPanel
                             ? dynamic_cast<ya::UICanvasSlot*>(_canvasPanel->getSlotForChild(*_highlightPanel))
                             : nullptr) {
            slot->setFixedSize(selected->size);
            slot->setOffset(-selected->size * 0.5f);
        }
        else {
            YA_CORE_ERROR("Workbench preview highlight is missing its canvas slot");
        }
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

    // Page switches and dynamic row rebuilds happen during the preceding
    // input/update phase. Resolve the new parent-owned slot geometry before
    // syncing presentation state or running automation in this same update
    // boundary; the later snapshot pass may still reuse the clean result.
    _tree->layout();
    syncPresentationState();
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

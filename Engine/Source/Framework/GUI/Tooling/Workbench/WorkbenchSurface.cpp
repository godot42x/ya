#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GUI/Tooling/Workbench/WorkbenchTheme.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

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

void FWorkbenchSurface::assembleChrome(ya::WidgetTree& tree, ya::UICanvasPanel& parent)
{
    // Root is a canvas layout host. Window fill is a sibling UIBorder so
    // chrome (column) can still pack by box layout.
    auto chrome = ya::ui::column("WorkbenchChrome").setSpacing(0.0f).share();
    _chromeColumn = chrome;
    ya::ui::attach(tree, parent, chrome, ya::ui::canvasSlot().fill());

    buildMenuBar(tree, *chrome);
    buildWorkspaceShell(tree, *chrome);
    buildStatusBar(tree, *chrome);
    selectPage(_initialPageIndex);
}

void FWorkbenchSurface::buildUI(ya::WidgetTree& tree)
{
    _tree = &tree;

    if (findPageIndexByName("Editor") < 0) {
        _pages.push_back(FPage{.group = "Composition", .name = "Editor", .build = {}});
    }
    _editorPageIndex = findPageIndexByName("Editor");

    _root = std::make_shared<ya::UICanvasPanel>("WorkbenchRoot");
    ya::FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
    const ya::WidgetAttachment attached = tree.attachToLayer(ya::WidgetTree::ELayer::Content, _root, fillArgs);
    YA_CORE_ASSERT(attached.valid(), "WorkbenchSurface: failed to attach WorkbenchRoot");

    auto windowFill = ya::ui::border("WorkbenchRootFill")
                          .setStyleKey("panel.window")
                          .setVisibility(ya::EWidgetVisibility::HitTestInvisible)
                          .release();
    ya::ui::attach(tree, *_root, windowFill, ya::ui::canvasSlot().fill());

    assembleChrome(tree, *_root);
}

int FWorkbenchSurface::findPageIndexByName(const std::string& name) const
{
    std::string canonical = name;
    if (name == "Layout") {
        canonical = "Box";
    }
    else if (name == "Gallery") {
        canonical = "Binding";
    }
    else if (name == "Modal" || name == "Interactions") {
        canonical = "Dialog";
    }
    else if (name == "Unicode" || name == "中文测试") {
        canonical = "Fonts";
    }
    else if (name == "RoundedRect") {
        canonical = "Brush";
    }
    for (size_t i = 0; i < _pages.size(); ++i) {
        if (_pages[i].name == canonical) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FWorkbenchSurface::addPage(const std::string& group, const std::string& name, FPageBuilder builder)
{
    _lastPageGroup = group;
    _pages.push_back(FPage{.group = group, .name = name, .build = std::move(builder)});
    return static_cast<int>(_pages.size()) - 1;
}

int FWorkbenchSurface::addPage(const std::string& name, FPageBuilder builder)
{
    return addPage(_lastPageGroup, name, std::move(builder));
}

void FWorkbenchSurface::setPageLeave(const std::string& name, FPageLeave leave)
{
    const int index = findPageIndexByName(name);
    if (index < 0) {
        return;
    }
    _pages[static_cast<size_t>(index)].leave = std::move(leave);
}

bool FWorkbenchSurface::selectPageByName(const std::string& name)
{
    const int index = findPageIndexByName(name);
    if (index < 0) {
        return false;
    }
    selectPage(index);
    return true;
}

ya::UISelectableRow* FWorkbenchSurface::getPageRow(const std::string& name) const
{
    for (const auto& row : _pageRows) {
        if (row && row->_itemId == name) {
            return row.get();
        }
    }
    return nullptr;
}

void FWorkbenchSurface::failSmoke(const std::string& message)
{
    YA_CORE_ERROR("{}", message);
    _bAutomationDone = true;
}

void FWorkbenchSurface::applyChromeSafeZone(const FChromeSafeZone& zone)
{
    const bool bChanged = _chromeSafeZone.left != zone.left ||
                          _chromeSafeZone.right != zone.right ||
                          _chromeSafeZone.titleHeight != zone.titleHeight;
    _chromeSafeZone = zone;
    if (!bChanged || !_chromeColumn || !_menuBar) {
        return;
    }
    if (ya::UIBoxSlot* slot = _chromeColumn->getBoxSlot(*_menuBar)) {
        slot->setMargin(ya::FMargin{zone.left, 0.0f, zone.right, 0.0f});
        const float height = zone.titleHeight > 0.0f ? zone.titleHeight : 30.0f;
        slot->setPreferredSize({0.0f, height});
    }
    if (_tree) {
        _tree->invalidateLayout();
    }
}

void FWorkbenchSurface::buildMenuBar(ya::WidgetTree& tree, ya::UIContainer& parent)
{
    _menuBar = std::make_shared<ya::UIMenuBar>("MainMenu");
    (void)ya::ui::attach(tree,
                         parent,
                         _menuBar,
                         ya::ui::boxSlot()
                             .preferredSize({0.0f, 30.0f})
                             .margin(ya::FMargin{_chromeSafeZone.left,
                                                 0.0f,
                                                 _chromeSafeZone.right,
                                                 0.0f}));

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
    _menuBar->addItem("View", [this, log]
    {
        std::vector<ya::UIMenu::FItem> items = {
            ya::UIMenu::FItem{.label = "Show Grid", .action = [log] { log("Menu: Show Grid"); }},
        };
#if !defined(YA_PROFILING_DISABLED)
        auto channelItem = [log](const char* label, ya::EGuiFrameInspectorChannel channel) {
            ya::UIMenu::FItem item;
            item.label     = label;
            item.bCheckable = true;
            item.bChecked  = ya::isGuiFrameInspectorChannelOn(channel);
            item.action   = [log, channel, label]() {
                ya::toggleGuiFrameInspectorChannel(channel);
                log(std::format("{} {}", label, ya::isGuiFrameInspectorChannelOn(channel) ? "on" : "off"));
            };
            return item;
        };
        items.push_back(channelItem("Frame Inspector HUD", ya::EGuiFrameInspectorChannel::Hud));
        items.push_back(channelItem("Rebuild Flash", ya::EGuiFrameInspectorChannel::Rebuild));
        items.push_back(channelItem("Overdraw Heatmap", ya::EGuiFrameInspectorChannel::Overdraw));
#endif
        items.push_back(ya::UIMenu::FItem{.label = "Fullscreen", .action = [log] { log("Menu: Fullscreen"); }});
        items.push_back(ya::UIMenu::FItem::separator());
        items.push_back(ya::UIMenu::FItem{.label = "Dark Theme", .action = [this, log]
        {
            bDarkTheme = true;
            if (onToggleTheme) {
                onToggleTheme(true);
            }
            log("Theme -> dark");
        }});
        items.push_back(ya::UIMenu::FItem{.label = "Light Theme", .action = [this, log]
        {
            bDarkTheme = false;
            if (onToggleTheme) {
                onToggleTheme(false);
            }
            log("Theme -> white");
        }});
        return ya::UIMenu::create(std::move(items));
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

void FWorkbenchSurface::buildPageList(ya::WidgetTree& tree, ya::UIBorder& parent)
{
    _pageRailScroll = std::make_shared<ya::UIScrollViewport>("FeatureRailScroll");
    (void)ya::ui::attach(tree, parent, _pageRailScroll, ya::ui::contentSlot().fill());

    _pageRailList = std::make_shared<ya::UIContainer>("FeatureRailList");
    _pageRailList->setDirection(ya::EWidgetBoxLayout::Vertical);
    _pageRailList->setSpacing(2.0f);
    _pageRailList->setPadding({6.0f, 6.0f});
    tree.attach(*_pageRailScroll, _pageRailList);

    _pageRows.clear();
    std::string lastGroup;
    for (size_t i = 0; i < _pages.size(); ++i) {
        const FPage& page = _pages[i];
        if (page.group != lastGroup && !page.group.empty()) {
            lastGroup = page.group;
            auto header = std::make_shared<ya::UIText>("RailGroup_" + page.group);
            header->_fontSize = 10;
            header->setStyleKey("text.eyebrow");
            header->setText(page.group);
            tree.attach(*_pageRailList, header);
            if (auto* slot = dynamic_cast<ya::UIBoxSlot*>(_pageRailList->getSlotForChild(*header))) {
                slot->setPreferredSize({0.0f, 18.0f});
            }
        }

        auto row = std::make_shared<ya::UISelectableRow>("RailPage_" + page.name);
        row->_itemId = page.name;
        row->_onSelect = [this](const std::string& id) { (void)selectPageByName(id); };
        auto label = std::make_shared<ya::UIText>("RailPageLabel_" + page.name);
        label->_fontSize = 13;
        label->setStyleKey("text");
        label->setText(page.name);
        label->_vAlign = ya::EWidgetAlignV::Center;
        tree.attach(*_pageRailList, row);
        if (auto* slot = dynamic_cast<ya::UIBoxSlot*>(_pageRailList->getSlotForChild(*row))) {
            slot->setPreferredSize({0.0f, 22.0f});
        }
        row->setContentPadding(ya::FMargin{8.0f, 0.0f, 4.0f, 0.0f});
        tree.attach(*row, label);
        _pageRows.push_back(row);
    }
}

void FWorkbenchSurface::syncRailSelection()
{
    for (size_t i = 0; i < _pageRows.size(); ++i) {
        _pageRows[i]->setSelected(static_cast<int>(i) == _currentPageIndex);
    }
}

void FWorkbenchSurface::buildWorkspaceShell(ya::WidgetTree& tree, ya::UIContainer& parent)
{
    _workspaceSplit = std::make_shared<ya::UISplitPane>("WorkbenchShellSplit");
    _workspaceSplit->setSplitRatio(0.23f);
    _workspaceSplit->setMinFirstExtent(208.0f);
    _workspaceSplit->setMinSecondExtent(520.0f);
    ya::ui::attach(tree, parent, _workspaceSplit, ya::ui::boxSlot().fill());

    buildPageRail(tree, *_workspaceSplit);
    // The demo host must exist before the tab bar selects its first page:
    // selectTab() fires the page-switch callback synchronously.
    buildDemoHost(tree, *_workspaceSplit);
}


void FWorkbenchSurface::buildPageRail(ya::WidgetTree& tree, ya::UISplitPane& parent)
{
    using namespace ya;

    _pageRailCard = ui::border("FeatureRailCard")
                        .setStyleKey("panel.sidebar.card")
                        .takeAs<UIBorder>();

    auto pageRail =
        ui::border("FeatureRail")
            .setStyleKey("panel.sidebar")
            .child(
                ui::column()
                    .child(
                        ui::text("FeatureRailTitle")
                            .setText("FEATURE GALLERY")
                            .setFontSize(10)
                            .setColor(kHeaderColor)
                            .setHAlign(EWidgetAlignH::Center)
                            .setVAlign(EWidgetAlignV::Center),
                        ui::boxSlot())
                    .child(_pageRailCard, ui::boxSlot().fill()),
                ui::contentSlot().fill());

    ya::ui::attach(tree, parent, pageRail.release(), ya::ui::contentSlot().fill());

    buildPageList(tree, *_pageRailCard);
}

void FWorkbenchSurface::buildDemoHost(ya::WidgetTree& tree, ya::UISplitPane& parent)
{
    _contentFrame = std::make_shared<ya::UIBorder>("DemoContentFrame");
    _contentFrame->_styleKey = "panel.window";
    _contentFrame->setPadding(ya::FMargin{14.0f, 12.0f, 14.0f, 12.0f});
    ya::ui::attach(tree, parent, _contentFrame);

    auto surface = ya::ui::border("DemoSurface")
                       .setStyleKey("panel.surface")
                       .takeAs<ya::UIBorder>();
    ya::ui::attach(tree, *_contentFrame, surface, ya::ui::contentSlot().fill());

    _demoHost = std::make_shared<ya::UICanvasPanel>("DemoHost");
    ya::ui::attach(tree, *surface, _demoHost, ya::ui::contentSlot().fill());
}

void FWorkbenchSurface::buildStatusBar(ya::WidgetTree& tree, ya::UIContainer& parent)
{
    auto statusText = ya::ui::text("Status")
                          .setText("Select a feature | Click / drag / keyboard to explore")
                          .setFontSize(13)
                          .setColor(kHeaderColor)
                          .setHAlign(ya::EWidgetAlignH::Left);
    _statusText = statusText.share();

    auto commandResultText = ya::ui::text("CommandResult")
                                 .setText("Ready")
                                 .setFontSize(13)
                                 .setColor({0.60f, 0.80f, 0.62f, 1.0f})
                                 .setHAlign(ya::EWidgetAlignH::Right);
    _commandResultText = commandResultText.share();

    // A fill slot takes the remainder of the row and lets the text right-align
    // inside it, giving a "left status / right result" shell without canvas math.
    auto statusBar = ya::ui::row("WorkbenchStatusBar")
                         .setSpacing(8.0f)
                         .setPadding({12.0f, 3.0f})
                         .child(std::move(statusText), ya::ui::boxSlot().preferredSize({0.0f, 24.0f}))
                         .child(std::move(commandResultText),
                                ya::ui::boxSlot().fill().preferredSize({0.0f, 24.0f}));

    ya::ui::attach(tree, parent, statusBar.release(), ya::ui::boxSlot().preferredSize({0.0f, 30.0f}));
}

void FWorkbenchSurface::selectPage(int index)
{
    const int pageCount = static_cast<int>(_pages.size());
    if (index < 0 || index >= pageCount || index == _currentPageIndex) {
        return;
    }

    const int previous = _currentPageIndex;
    _currentPageIndex  = index;
    syncRailSelection();
    if (previous >= 0 && previous < pageCount && _tree) {
        if (FPageLeave& leave = _pages[static_cast<size_t>(previous)].leave; leave) {
            leave(*_tree);
        }
    }
    clearDemoHost();

    FPage& page = _pages[static_cast<size_t>(index)];
    if (index == _editorPageIndex || !page.build) {
        buildEditorDemo(*_tree, *_demoHost);
    }
    else {
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

void FWorkbenchSurface::buildEditorDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent)
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
    auto listPanel = ya::ui::canvasPanel("ItemList")
                         .child(ya::ui::border("ItemListFill")
                                    .setStyleKey("panel")
                                    .setVisibility(ya::EWidgetVisibility::HitTestInvisible),
                                ya::ui::canvasSlot().fill())
                         .child(headerText("ITEMS"), ya::ui::canvasSlot().offset({10.0f, 8.0f}).size({200.0f, 20.0f}))
                         .child(std::move(scroll), ya::ui::canvasSlot().fill().insets(ya::FMargin{0.0f, 34.0f, 0.0f, 0.0f}));
    _listPanel = listPanel.share();

    auto previewName = ya::ui::text("PreviewName")
                           .setText("(no selection)")
                           .setFontSize(15)
                           .setColor({0.95f, 0.96f, 0.98f, 1.0f})
                           .setHAlign(ya::EWidgetAlignH::Center);
    _previewName = previewName.share();
    auto highlight = ya::ui::border("SelectionHighlight")
                         .setColor({0.35f, 0.55f, 0.90f, 1.0f})
                         .child(std::move(previewName), ya::ui::contentSlot().fill());
    _highlightPanel = highlight.share();
    auto canvas     = ya::ui::canvasPanel("PreviewCanvas")
                      .child(ya::ui::border("PreviewCanvasFill")
                                 .setStyleKey("panel.canvas")
                                 .setVisibility(ya::EWidgetVisibility::HitTestInvisible),
                             ya::ui::canvasSlot().fill())
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
    auto inspector = ya::ui::border("Inspector")
                         .setStyleKey("panel")
                         .child(std::move(inspectorForm), ya::ui::contentSlot().fill());

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

    auto page = ya::ui::canvasPanel("EditorDemo")
                    .child(ya::ui::border("EditorDemoFill")
                               .setStyleKey("panel.window")
                               .setVisibility(ya::EWidgetVisibility::HitTestInvisible),
                           ya::ui::canvasSlot().fill())
                    .child(std::move(toolbar), ya::ui::canvasSlot()
                         .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                         .offset({0.0f, 6.0f})
                         .size({0.0f, 32.0f}))
                    .child(std::move(mainSplit), ya::ui::canvasSlot().fill());
    // The editor page is mounted into DemoHost, whose default layout is a
    // canvas host. The page root therefore must declare its own fill edge at
    // attach time; otherwise the default canvas slot leaves the whole editor
    // subtree at 0x0 and later children can still spill over the toolbar.
    ya::ui::attach(tree,
                   parent,
                   std::move(page.take()),
                   ya::ui::canvasSlot().fill());

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

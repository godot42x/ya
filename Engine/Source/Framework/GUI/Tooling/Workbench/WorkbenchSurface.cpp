#include "GUI/Tooling/Workbench/WorkbenchSurface.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

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

constexpr glm::vec4 kWindowColor   = {0.075f, 0.082f, 0.10f, 1.0f};
constexpr glm::vec4 kPanelColor    = {0.11f, 0.12f, 0.15f, 1.0f};
constexpr glm::vec4 kCanvasColor   = {0.05f, 0.055f, 0.07f, 1.0f};
constexpr glm::vec4 kHeaderColor   = {0.55f, 0.60f, 0.68f, 1.0f};
constexpr glm::vec4 kButtonNormal  = {0.20f, 0.22f, 0.27f, 1.0f};
constexpr glm::vec4 kButtonHovered = {0.27f, 0.30f, 0.37f, 1.0f};
constexpr glm::vec4 kButtonPressed = {0.14f, 0.16f, 0.20f, 1.0f};
constexpr glm::vec4 kButtonFocused = {0.24f, 0.46f, 0.82f, 1.0f};

std::shared_ptr<ya::UIButton> makeToolButton(const std::string& name, const std::string& label, float width = 0.0f)
{
    auto button = std::make_shared<ya::UIButton>(name);
    if (width > 0.0f) {
        // Explicit control (inspector form): keep the fixed width.
        button->_size = {width, 24.0f};
    }
    else {
        // SizeToContent: the label text sizes the button (toolbar row).
        button->_bAutoSize      = true;
        button->setContentPadding({10.0f, 4.0f});
    }
    button->_normalColor  = kButtonNormal;
    button->_hoveredColor = kButtonHovered;
    button->_pressedColor = kButtonPressed;
    button->_focusedColor = kButtonFocused;

    auto text = std::make_shared<ya::UIText>(name + "_Label");
    text->_bAutoSize = true;
    text->_fontSize  = 14;
    text->_text      = label;
    text->_color     = {0.92f, 0.94f, 0.97f, 1.0f};
    text->_hAlign    = ya::EWidgetAlignH::Center;
    text->_vAlign    = ya::EWidgetAlignV::Center;
    button->addDetachedChild(text);
    return button;
}

std::shared_ptr<ya::UIText> makeHeaderText(const std::string& text)
{
    auto label = std::make_shared<ya::UIText>(text + "_Header");
    label->_size     = {200.0f, 20.0f};
    label->_fontSize = 13;
    label->_text     = text;
    label->_color    = kHeaderColor;
    return label;
}

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

void FWorkbenchSurface::buildUI(ya::WidgetTree& tree)
{
    _tree = &tree;

    _root = std::make_shared<ya::UIPanel>("WorkbenchRoot");
    _root->_anchorMin = {0.0f, 0.0f};
    _root->_anchorMax = {1.0f, 1.0f};
    _root->_color     = kWindowColor;
    tree.attachToLayer(ya::WidgetTree::ELayer::Content, _root);

    buildMenuBar(tree, *_root);
    // The demo host must exist before the tab bar selects its first page:
    // selectTab() fires the page-switch callback synchronously.
    buildDemoHost(tree, *_root);
    buildTabBar(tree, *_root);
    buildStatusBar(tree, *_root);

    selectPage(_initialPageIndex);
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
    _menuBar->_position  = {0.0f, 0.0f};
    _menuBar->_size      = {0.0f, 30.0f};
    tree.attach(parent, _menuBar);

    const auto log = [this](const std::string& text) { logStatus(text); };

    _menuBar->addItem("File", [this, log]
    {
        return ya::UIMenu::create({
            {"New Document", [log] { log("Menu: New Document"); }},
            {"Open File...", [log] { log("Menu: Open File..."); }},
            {"Save", [log] { log("Menu: Save"); }},
            {"Save As...", [log] { log("Menu: Save As..."); }},
            {"---", nullptr},
            {"Exit", [log] { log("Menu: Exit"); }},
        });
    });
    _menuBar->addItem("Edit", [log]
    {
        return ya::UIMenu::create({
            {"Undo", [log] { log("Menu: Undo"); }},
            {"Redo", [log] { log("Menu: Redo"); }},
            {"---", nullptr},
            {"Copy", [log] { log("Menu: Copy"); }},
            {"Paste", [log] { log("Menu: Paste"); }},
        });
    });
    _menuBar->addItem("View", [log]
    {
        return ya::UIMenu::create({
            {"Show Grid", [log] { log("Menu: Show Grid"); }},
            {"Show FPS", [log] { log("Menu: Show FPS"); }},
            {"Fullscreen", [log] { log("Menu: Fullscreen"); }},
        });
    });
    _menuBar->addItem("Help", [log]
    {
        return ya::UIMenu::create({
            {"About", [log] { log("Menu: About"); }},
            {"Documentation", [log] { log("Menu: Documentation"); }},
        });
    });
}

void FWorkbenchSurface::buildTabBar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _tabBar = std::make_shared<ya::UITabBar>("DemoTabs");
    _tabBar->_anchorMin = {0.0f, 0.0f};
    _tabBar->_anchorMax = {1.0f, 0.0f};
    _tabBar->_position  = {0.0f, 34.0f};
    _tabBar->_size      = {0.0f, 30.0f};
    tree.attach(parent, _tabBar);

    for (const FPage& page : _pages) {
        _tabBar->addTab(page.name);
    }
    _tabBar->addTab("Editor");
    _editorPageIndex = static_cast<int>(_pages.size());
    _tabBar->_onTabSelected = [this](int index) { selectPage(index); };
}

void FWorkbenchSurface::buildDemoHost(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _demoHost = std::make_shared<ya::UIPanel>("DemoHost");
    _demoHost->_anchorMin = {0.0f, 0.085f};
    _demoHost->_anchorMax = {1.0f, 0.94f};
    _demoHost->_color     = kWindowColor;
    tree.attach(parent, _demoHost);
}

void FWorkbenchSurface::buildStatusBar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _statusText = std::make_shared<ya::UIText>("Status");
    _statusText->_anchorMin = {0.0f, 1.0f};
    _statusText->_anchorMax = {0.0f, 1.0f};
    _statusText->_position  = {12.0f, -30.0f};
    _statusText->_size      = {420.0f, 24.0f};
    _statusText->_fontSize  = 13;
    _statusText->_text      = "Tab: switch demo | Click / drag / keyboard to explore";
    _statusText->_color     = kHeaderColor;
    tree.attach(parent, _statusText);

    _commandResultText = std::make_shared<ya::UIText>("CommandResult");
    // Bottom full-width strip: right-aligned status text that never leaves
    // the window on resize (a corner anchor with a pixel position would).
    _commandResultText->_anchorMin = {0.0f, 1.0f};
    _commandResultText->_anchorMax = {1.0f, 1.0f};
    _commandResultText->_position  = {0.0f, -30.0f};
    _commandResultText->_size      = {0.0f, 24.0f};
    _commandResultText->_fontSize  = 13;
    _commandResultText->_text      = "Ready";
    _commandResultText->_color     = {0.60f, 0.80f, 0.62f, 1.0f};
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
        const auto log = [this](const std::string& text) { logStatus(text); };
        _pages[static_cast<size_t>(index)].build(*_tree, *_demoHost, log);
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
        _commandResultText->_text = text;
    }
}

const std::string& FWorkbenchSurface::getStatusText() const
{
    static const std::string kEmpty;
    return _commandResultText ? _commandResultText->_text : kEmpty;
}

// === Editor demo page (the original workbench editor loop) ===

void FWorkbenchSurface::buildEditorDemo(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto editorPanel = std::make_shared<ya::UIPanel>("EditorDemo");
    editorPanel->_anchorMin = {0.0f, 0.0f};
    editorPanel->_anchorMax = {1.0f, 1.0f};
    editorPanel->_color     = kWindowColor;
    tree.attach(parent, editorPanel);

    buildToolbar(tree, *editorPanel);

    _mainSplit = std::make_shared<ya::UISplitPane>("MainSplit");
    _mainSplit->_anchorMin       = {0.0f, 0.0f};
    _mainSplit->_anchorMax       = {1.0f, 1.0f};
    _mainSplit->setPadding({0.0f, 42.0f});
    _mainSplit->_size            = {0.0f, 0.0f};
    _mainSplit->setSplitRatio(0.24f);
    _mainSplit->setMinFirstExtent(180.0f);
    _mainSplit->setMinSecondExtent(420.0f);
    tree.attach(*editorPanel, _mainSplit);

    buildDocumentList(tree, *_mainSplit);

    _rightSplit = std::make_shared<ya::UISplitPane>("RightSplit");
    _rightSplit->_anchorMin       = {0.0f, 0.0f};
    _rightSplit->_anchorMax       = {1.0f, 1.0f};
    _rightSplit->setSplitRatio(0.66f);
    _rightSplit->setMinFirstExtent(240.0f);
    _rightSplit->setMinSecondExtent(220.0f);
    tree.attach(*_mainSplit, _rightSplit);

    buildCanvas(tree, *_rightSplit);
    buildInspector(tree, *_rightSplit);

    workspace.resetLayout();
    _bRowsDirty = true;
}

void FWorkbenchSurface::buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto toolbar = std::make_shared<ya::UIContainer>("Toolbar");
    toolbar->_anchorMin = {0.0f, 0.0f};
    toolbar->_anchorMax = {1.0f, 0.0f};
    toolbar->_position  = {0.0f, 6.0f};
    toolbar->_size      = {0.0f, 32.0f};
    toolbar->setDirection(ya::EWidgetBoxLayout::Horizontal);
    toolbar->setSpacing(8.0f);
    toolbar->setPadding({8.0f, 4.0f});
    tree.attach(parent, toolbar);

    _addButton    = makeToolButton("Add", "Add");
    _removeButton = makeToolButton("Remove", "Remove");
    _renameButton = makeToolButton("Rename", "Rename");
    _resetButton  = makeToolButton("ResetLayout", "Reset Layout");
    _addButton->_onClick    = [this] { cmdAdd(); };
    _removeButton->_onClick = [this] { cmdRemove(); };
    _renameButton->_onClick = [this] { cmdRename(); };
    _resetButton->_onClick  = [this] { cmdResetLayout(); };
    tree.attach(*toolbar, _addButton);
    tree.attach(*toolbar, _removeButton);
    tree.attach(*toolbar, _renameButton);
    tree.attach(*toolbar, _resetButton);
}

void FWorkbenchSurface::buildDocumentList(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _listPanel = std::make_shared<ya::UIPanel>("ItemList");
    _listPanel->_anchorMin = {0.0f, 0.0f};
    _listPanel->_anchorMax = {1.0f, 1.0f};
    _listPanel->_color     = kPanelColor;
    tree.attach(parent, _listPanel);

    auto header = makeHeaderText("ITEMS");
    header->_position = {10.0f, 8.0f};
    tree.attach(*_listPanel, header);

    auto scroll = std::make_shared<ya::UIScrollViewport>("ItemScroll");
    scroll->_anchorMin = {0.0f, 0.0f};
    scroll->_anchorMax = {1.0f, 1.0f};
    scroll->_position  = {0.0f, 34.0f};
    scroll->_size      = {0.0f, 0.0f};
    tree.attach(*_listPanel, scroll);
    _rowScroll = scroll;

    _rowList = std::make_shared<ya::UIContainer>("RowList");
    _rowList->_anchorMin = {0.0f, 0.0f};
    _rowList->_anchorMax = {1.0f, 1.0f};
    _rowList->setPadding({8.0f, 8.0f});
    _rowList->setDirection(ya::EWidgetBoxLayout::Vertical);
    _rowList->setSpacing(2.0f);
    tree.attach(*scroll, _rowList);
}

void FWorkbenchSurface::buildCanvas(ya::WidgetTree& tree, ya::UIElement& parent)
{
    _canvasPanel = std::make_shared<ya::UIPanel>("PreviewCanvas");
    _canvasPanel->_anchorMin = {0.0f, 0.0f};
    _canvasPanel->_anchorMax = {1.0f, 1.0f};
    _canvasPanel->_color     = kCanvasColor;
    tree.attach(parent, _canvasPanel);

    auto header = makeHeaderText("PREVIEW");
    header->_position = {10.0f, 8.0f};
    tree.attach(*_canvasPanel, header);

    _highlightPanel = std::make_shared<ya::UIPanel>("SelectionHighlight");
    _highlightPanel->_anchorMin = {0.5f, 0.5f};
    _highlightPanel->_anchorMax = {0.5f, 0.5f};
    _highlightPanel->_position  = {-70.0f, -45.0f};
    _highlightPanel->_size      = {140.0f, 90.0f};
    _highlightPanel->_color     = {0.35f, 0.55f, 0.90f, 1.0f};
    tree.attach(*_canvasPanel, _highlightPanel);

    _previewName = std::make_shared<ya::UIText>("PreviewName");
    _previewName->_position = {0.0f, 0.0f};
    _previewName->_size     = {400.0f, 24.0f};
    _previewName->_fontSize = 15;
    _previewName->_text     = "(no selection)";
    _previewName->_color    = {0.95f, 0.96f, 0.98f, 1.0f};
    _previewName->_hAlign   = ya::EWidgetAlignH::Center;
    tree.attach(*_highlightPanel, _previewName);
}

void FWorkbenchSurface::buildInspector(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto inspectorPanel = std::make_shared<ya::UIPanel>("Inspector");
    inspectorPanel->_anchorMin = {0.0f, 0.0f};
    inspectorPanel->_anchorMax = {1.0f, 1.0f};
    inspectorPanel->_color     = kPanelColor;
    tree.attach(parent, inspectorPanel);

    auto form = std::make_shared<ya::UIContainer>("InspectorForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({10.0f, 8.0f});
    form->_size      = {0.0f, 0.0f};
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(4.0f);
    tree.attach(*inspectorPanel, form);

    auto header = makeHeaderText("INSPECTOR");
    tree.attach(*form, header);

    auto nameLabel = makeHeaderText("Name");
    tree.attach(*form, nameLabel);

    _nameField = std::make_shared<ya::UITextField>("NameField");
    _nameField->_size     = {220.0f, 26.0f};
    _nameField->_fontSize = 14;
    _nameField->_onCommit = [this](const std::string& text) {
        workspace.renameSelected(text);
        setCommandResult(workspace.commandResult);
    };
    tree.attach(*form, _nameField);

    auto visibleLabel = makeHeaderText("Visible");
    tree.attach(*form, visibleLabel);

    _visibleToggle = makeToolButton("VisibleToggle", "Visible: on", 110.0f);
    _visibleToggle->_onClick = [this] {
        workspace.toggleSelectedVisible();
        setCommandResult(workspace.commandResult);
    };
    tree.attach(*form, _visibleToggle);

    auto colorLabel = makeHeaderText("Color");
    tree.attach(*form, colorLabel);

    _colorCycle = makeToolButton("ColorCycle", "Cycle Color", 110.0f);
    _colorCycle->_onClick = [this] {
        workspace.cycleSelectedColor();
        setCommandResult(workspace.commandResult);
    };
    tree.attach(*form, _colorCycle);

    _colorValue = std::make_shared<ya::UIText>("ColorValue");
    _colorValue->_size     = {220.0f, 14.0f};
    _colorValue->_fontSize = 12;
    _colorValue->_text     = "";
    _colorValue->_color    = kHeaderColor;
    tree.attach(*form, _colorValue);

    auto sizeLabel = makeHeaderText("Size");
    tree.attach(*form, sizeLabel);

    auto sizeRow = std::make_shared<ya::UIContainer>("SizeRow");
    sizeRow->setDirection(ya::EWidgetBoxLayout::Horizontal);
    sizeRow->setSpacing(6.0f);
    tree.attach(*form, sizeRow);

    _sizeGrow = makeToolButton("SizeGrow", "Grow +20", 90.0f);
    _sizeGrow->_onClick = [this] {
        workspace.stepSelectedSize({20.0f, 20.0f});
        setCommandResult(workspace.commandResult);
    };
    tree.attach(*sizeRow, _sizeGrow);

    _sizeShrink = makeToolButton("SizeShrink", "Shrink -20", 100.0f);
    _sizeShrink->_onClick = [this] {
        workspace.stepSelectedSize({-20.0f, -20.0f});
        setCommandResult(workspace.commandResult);
    };
    tree.attach(*sizeRow, _sizeShrink);

    _sizeValue = std::make_shared<ya::UIText>("SizeValue");
    _sizeValue->_size     = {220.0f, 14.0f};
    _sizeValue->_fontSize = 12;
    _sizeValue->_text     = "";
    _sizeValue->_color    = kHeaderColor;
    tree.attach(*form, _sizeValue);
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
        row->_size   = {240.0f, 22.0f};
        row->_onSelect = [this](const std::string& id) {
            workspace.select(id);
            setCommandResult("List: selected '" + id + "'");
        };
        row->_onActivate = [this](const std::string& id) {
            workspace.select(id);
            setCommandResult("List: activated '" + id + "'");
        };
        // Drag & drop reparent: rows are both sources and drop targets.
        row->_bDraggable     = true;
        row->_dragPayload    = item.id;
        row->_dragGhostLabel = item.name;
        const std::string targetId = item.id;
        row->_onDropped = [this, targetId](const std::string& droppedId)
        {
            if (workspace.reparent(droppedId, targetId)) {
                _bRowsDirty = true;
                setCommandResult(workspace.commandResult);
            }
        };

        auto label = std::make_shared<ya::UIText>("RowLabel_" + item.id);
        label->_size     = {240.0f, 22.0f};
        label->_fontSize = 13;
        label->_text     = item.bVisible ? item.name : item.name + " (hidden)";
        label->_color    = {0.88f, 0.90f, 0.94f, 1.0f};
        label->_vAlign   = ya::EWidgetAlignV::Center;
        // Tree indentation: one level per parent depth.
        label->_position = {static_cast<float>(workspace.getDepth(item.id)) * 14.0f, 0.0f};

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

    bool bGeometryChanged = false;

    const auto ordered = workspace.orderedItems();
    for (size_t i = 0; i < _rows.size() && i < ordered.size(); ++i) {
        const FWorkbenchItem& item = *ordered[i];
        if (!_rows[i]->getChildren().empty()) {
            if (auto* label = dynamic_cast<ya::UIText*>(_rows[i]->getChildren()[0].get())) {
                label->_text = item.bVisible ? item.name : item.name + " (hidden)";
            }
        }
        _rows[i]->_bSelected = (selected != nullptr && _rows[i]->_itemId == selected->id);
    }

    if (selected) {
        _highlightPanel->_visibility = selected->bVisible ? ya::EWidgetVisibility::Visible
                                                          : ya::EWidgetVisibility::Hidden;
        _highlightPanel->_size       = selected->size;
        _highlightPanel->_position   = -selected->size * 0.5f;
        _highlightPanel->_color      = selected->color;
        _previewName->_text = selected->bVisible ? selected->name : selected->name + " (hidden)";
        if (_highlightPanel->_size != _lastHighlightSize || _highlightPanel->_position != _lastHighlightPos) {
            bGeometryChanged     = true;
            _lastHighlightSize   = _highlightPanel->_size;
            _lastHighlightPos    = _highlightPanel->_position;
        }
    } else {
        _highlightPanel->_visibility = ya::EWidgetVisibility::Hidden;
        _previewName->_text          = "(no selection)";
    }

    if (_tree->getFocused() != _nameField.get()) {
        _nameField->_text = selected ? selected->name : "";
        _nameField->clampCursor();
    }
    _colorValue->_text = selected ? std::format("rgba({:.2f}, {:.2f}, {:.2f})",
                                                selected->color.r, selected->color.g, selected->color.b)
                                  : "-";
    _sizeValue->_text  = selected ? std::format("{} x {}",
                                                static_cast<int>(selected->size.x),
                                                static_cast<int>(selected->size.y))
                                  : "-";
    if (!_visibleToggle->getChildren().empty()) {
        if (auto* label = dynamic_cast<ya::UIText*>(_visibleToggle->getChildren()[0].get())) {
            label->_text = (selected && selected->bVisible) ? "Visible: on" : "Visible: off";
            if (label->_text != _lastToggleText) {
                bGeometryChanged = true;
                _lastToggleText  = label->_text;
            }
        }
    }

    if (bGeometryChanged && _tree) {
        _tree->invalidateLayout();
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
        dispatchPointer(ya::MouseButtonPressedEvent(0), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
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
        dispatchPointer(ya::MouseButtonPressedEvent(0), from);
        dispatchPointer(ya::MouseMoveEvent(to.x, to.y), to);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), to);
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

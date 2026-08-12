#include "GUI/Tooling/Workbench/WorkbenchSurface.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SplitPane.h"
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
        button->_size         = {width, 24.0f};
    }
    else {
        // SizeToContent: the label text sizes the button (toolbar row).
        button->_bAutoSize    = true;
        button->_contentPadding = {10.0f, 4.0f};
    }
    button->_normalColor  = kButtonNormal;
    button->_hoveredColor = kButtonHovered;
    button->_pressedColor = kButtonPressed;
    button->_focusedColor = kButtonFocused;

    auto text = std::make_shared<ya::UIText>(name + "_Label");
    text->_bAutoSize = true; // label measures its own text
    text->_fontSize = 14;
    text->_text     = label;
    text->_color    = {0.92f, 0.94f, 0.97f, 1.0f};
    text->_hAlign   = ya::EWidgetAlignH::Center;
    text->_vAlign   = ya::EWidgetAlignV::Center;
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

    buildToolbar(tree, *_root);

    _mainSplit = std::make_shared<ya::UISplitPane>("MainSplit");
    _mainSplit->_anchorMin      = {0.0f, 0.06f};
    _mainSplit->_anchorMax      = {1.0f, 0.955f};
    _mainSplit->_splitRatio     = 0.24f;
    _mainSplit->_minFirstExtent = 180.0f;
    _mainSplit->_minSecondExtent = 420.0f;
    tree.attach(*_root, _mainSplit);

    buildDocumentList(tree, *_mainSplit);

    _rightSplit = std::make_shared<ya::UISplitPane>("RightSplit");
    _rightSplit->_anchorMin       = {0.0f, 0.0f};
    _rightSplit->_anchorMax       = {1.0f, 1.0f};
    _rightSplit->_splitRatio      = 0.66f;
    _rightSplit->_minFirstExtent  = 240.0f;
    _rightSplit->_minSecondExtent = 220.0f;
    tree.attach(*_mainSplit, _rightSplit);

    buildCanvas(tree, *_rightSplit);
    buildInspector(tree, *_rightSplit);

    _statusText = std::make_shared<ya::UIText>("Status");
    _statusText->_anchorMin = {0.0f, 1.0f};
    _statusText->_anchorMax = {0.0f, 1.0f};
    _statusText->_position  = {16.0f, -42.0f};
    _statusText->_size      = {600.0f, 24.0f};
    _statusText->_fontSize  = 13;
    _statusText->_text      = "Arrow keys: navigate list | Tab: focus | Enter: activate";
    _statusText->_color     = kHeaderColor;
    tree.attach(*_root, _statusText);

    _commandResultText = std::make_shared<ya::UIText>("CommandResult");
    _commandResultText->_anchorMin = {1.0f, 1.0f};
    _commandResultText->_anchorMax = {1.0f, 1.0f};
    _commandResultText->_position  = {-400.0f, -42.0f};
    _commandResultText->_size      = {380.0f, 24.0f};
    _commandResultText->_fontSize  = 13;
    _commandResultText->_text      = workspace.commandResult;
    _commandResultText->_color     = {0.60f, 0.80f, 0.62f, 1.0f};
    _commandResultText->_hAlign    = ya::EWidgetAlignH::Right;
    tree.attach(*_root, _commandResultText);

    workspace.resetLayout();
    _bRowsDirty = true;
}

void FWorkbenchSurface::buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto toolbar = std::make_shared<ya::UIContainer>("Toolbar");
    toolbar->_anchorMin = {0.0f, 0.0f};
    toolbar->_anchorMax = {1.0f, 0.0f};
    toolbar->_position  = {0.0f, 8.0f};
    toolbar->_size      = {0.0f, 34.0f};
    toolbar->_direction = ya::EWidgetBoxLayout::Horizontal;
    toolbar->_spacing   = 8.0f;
    toolbar->_padding   = 8.0f;
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
    _rowList->_padding   = 8.0f;
    _rowList->_direction = ya::EWidgetBoxLayout::Vertical;
    _rowList->_spacing   = 2.0f;
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
    form->_position  = {10.0f, 8.0f};
    form->_size      = {0.0f, 0.0f};
    form->_direction = ya::EWidgetBoxLayout::Vertical;
    form->_spacing   = 4.0f;
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
    sizeRow->_direction = ya::EWidgetBoxLayout::Horizontal;
    sizeRow->_spacing   = 6.0f;
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
    _rowList->_padding   = 8.0f;
    _rowList->_direction = ya::EWidgetBoxLayout::Vertical;
    _rowList->_spacing   = 2.0f;
    _tree->attach(*_rowScroll, _rowList);
    _rows.clear();

    for (const FWorkbenchItem& item : workspace.items) {
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

        auto label = std::make_shared<ya::UIText>("RowLabel_" + item.id);
        label->_size     = {240.0f, 22.0f};
        label->_fontSize = 13;
        label->_text     = item.bVisible ? item.name : item.name + " (hidden)";
        label->_color    = {0.88f, 0.90f, 0.94f, 1.0f};
        label->_vAlign   = ya::EWidgetAlignV::Center;

        _tree->attach(*_rowList, row);
        _tree->attach(*row, label);
        _rows.push_back(row);
    }
    _bRowsDirty = false;
}

void FWorkbenchSurface::syncPresentationState()
{
    if (!_tree) {
        return;
    }
    if (_bRowsDirty) {
        rebuildItemRows();
    }

    const FWorkbenchItem* selected = workspace.getSelected();

    // SizeToContent widgets (auto-size buttons / highlight) need a relayout
    // when their runtime label / size changes; track and invalidate only on
    // geometry-affecting deltas.
    bool bGeometryChanged = false;

    for (size_t i = 0; i < _rows.size() && i < workspace.items.size(); ++i) {
        const FWorkbenchItem& item = workspace.items[i];
        if (!_rows[i]->getChildren().empty()) {
            if (auto* label = dynamic_cast<ya::UIText*>(_rows[i]->getChildren()[0].get())) {
                label->_text = item.bVisible ? item.name : item.name + " (hidden)";
            }
        }
        _rows[i]->_bSelected = (selected != nullptr && _rows[i]->_itemId == selected->id);
    }

    if (selected) {
        _highlightPanel->_visibility = ya::EWidgetVisibility::Visible;
        _highlightPanel->_size       = selected->size;
        _highlightPanel->_position   = -selected->size * 0.5f;
        _highlightPanel->_color      = selected->bVisible
                                           ? selected->color
                                           : glm::vec4(selected->color.r, selected->color.g, selected->color.b, 0.35f);
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

    _commandResultText->_text = workspace.commandResult;

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
    if (_commandResultText) {
        _commandResultText->_text = text;
    }
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
    auto failAutomation = [this](const std::string& message) {
        YA_CORE_ERROR("{}", message);
        _bAutomationDone = true;
    };

    switch (_frame) {
    case 3: {
        const glm::vec2 center = _addButton->_layoutRect.pos + _addButton->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
        if (workspace.items.size() != 4u || workspace.getSelected() == nullptr ||
            workspace.getSelected()->name != "Item 4") {
            failAutomation(std::format("Workbench automation: Add failed (items={}, selected='{}')",
                                       workspace.items.size(),
                                       workspace.getSelected() ? workspace.getSelected()->name : "<none>"));
            return;
        }
        break;
    }
    case 4: {
        dispatchPointer(ya::MouseButtonPressedEvent(0), _nameField->_layoutRect.pos + _nameField->_layoutRect.extent * 0.5f);
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
            failAutomation(std::format("Workbench automation: rename failed ('{}')", selected ? selected->name : "<none>"));
            return;
        }
        break;
    }
    case 5: {
        const glm::vec2 center = _removeButton->_layoutRect.pos + _removeButton->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
        if (workspace.items.size() != 3u) {
            failAutomation(std::format("Workbench automation: Remove failed (items={})", workspace.items.size()));
            return;
        }
        break;
    }
    case 6: {
        ya::KeyPressedEvent up{};
        up._keyCode = ya::EKey::Up;
        up._mod     = 0;
        dispatchKey(up);
        if (workspace.getSelectedIndex() != 1) {
            failAutomation(std::format("Workbench automation: arrow navigation failed (index={})",
                                       workspace.getSelectedIndex()));
            return;
        }
        break;
    }
    case 7: {
        const FWorkbenchItem* selected = workspace.getSelected();
        const bool bNameSynced = selected && _nameField->_text == selected->name;
        const bool bRowsSynced = selected && std::any_of(_rows.begin(), _rows.end(), [&](const auto& row) {
            return row->_itemId == selected->id && row->_bSelected;
        });
        const bool bRowLabelSynced = selected && std::any_of(_rows.begin(), _rows.end(), [&](const auto& row) {
            if (row->_itemId != selected->id || row->getChildren().empty()) {
                return false;
            }
            auto* label = dynamic_cast<ya::UIText*>(row->getChildren()[0].get());
            return label != nullptr && label->_text == selected->name;
        });
        const bool bPreviewSynced = selected && _previewName->_text == selected->name;
        const bool bStatusSynced  = _commandResultText->_text == "List: navigated";
        if (!bNameSynced || !bRowsSynced || !bRowLabelSynced || !bPreviewSynced || !bStatusSynced) {
            failAutomation(std::format("Workbench automation: presenter sync incomplete (name={} rows={} labels={} preview={} status={})",
                                       bNameSynced, bRowsSynced, bRowLabelSynced, bPreviewSynced, bStatusSynced));
            return;
        }
        break;
    }
    case 8: {
        for (int i = 0; i < 30; ++i) {
            const glm::vec2 center = _addButton->_layoutRect.pos + _addButton->_layoutRect.extent * 0.5f;
            dispatchPointer(ya::MouseButtonPressedEvent(0), center);
            dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
        }
        if (workspace.items.size() != 33u) {
            failAutomation(std::format("Workbench automation: list growth failed (items={})", workspace.items.size()));
            return;
        }
        break;
    }
    case 9: {
        _tree->layout();
        const glm::vec2 listPoint = _rowScroll->_layoutRect.pos + _rowScroll->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseScrolledEvent(0.0f, -1.0f), listPoint);
        if (_rowScroll->_scrollOffset <= 0.0f) {
            failAutomation(std::format("Workbench automation: scroll failed (offset={})", _rowScroll->_scrollOffset));
            return;
        }
        break;
    }
    case 10: {
        _tree->layout();
        const float oldRatio = _mainSplit->_splitRatio;
        const glm::vec2 divider = _mainSplit->getDividerRect().pos + _mainSplit->getDividerRect().extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), divider);
        dispatchPointer(ya::MouseMoveEvent(divider.x + 60.0f, divider.y), {divider.x + 60.0f, divider.y});
        dispatchPointer(ya::MouseButtonReleasedEvent(0), {divider.x + 60.0f, divider.y});
        if (std::abs(_mainSplit->_splitRatio - oldRatio) < 0.01f) {
            failAutomation(std::format("Workbench automation: split drag failed (ratio {})", _mainSplit->_splitRatio));
            return;
        }
        break;
    }
    case 11: {
        ya::KeyPressedEvent tab{};
        tab._keyCode = ya::EKey::Tab;
        tab._mod     = 0;
        dispatchKey(tab);
        if (_tree->getFocused() == nullptr) {
            failAutomation("Workbench automation: Tab traversal failed");
            return;
        }
        break;
    }
    case 12: {
        if (_rowScroll->_scrollOffset <= 0.0f || _tree->getFocused() == nullptr) {
            failAutomation("Workbench automation: final state check failed");
            return;
        }
        _bSmokePassed    = true;
        _bAutomationDone = true;
        YA_CORE_INFO("Workbench automation PASSED: add -> rename -> remove -> navigate -> grow -> scroll -> split-drag -> tab -> sync");
        break;
    }
    default:
        break;
    }
}

} // namespace guiworkbench

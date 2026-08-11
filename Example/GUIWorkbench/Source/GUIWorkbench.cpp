#include "GUIWorkbench.h"

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
#include <format>

namespace guiworkbench
{

namespace
{

constexpr glm::vec4 kWindowColor        = {0.075f, 0.082f, 0.10f, 1.0f};
constexpr glm::vec4 kPanelColor         = {0.11f, 0.12f, 0.15f, 1.0f};
constexpr glm::vec4 kHeaderColor        = {0.55f, 0.60f, 0.68f, 1.0f};
constexpr glm::vec4 kCanvasColor        = {0.05f, 0.055f, 0.07f, 1.0f};
constexpr glm::vec4 kButtonNormal       = {0.20f, 0.22f, 0.27f, 1.0f};
constexpr glm::vec4 kButtonHovered      = {0.27f, 0.30f, 0.37f, 1.0f};
constexpr glm::vec4 kButtonPressed      = {0.14f, 0.16f, 0.20f, 1.0f};
constexpr glm::vec4 kButtonFocused      = {0.24f, 0.46f, 0.82f, 1.0f};

std::shared_ptr<ya::UIButton> makeToolButton(const std::string& name, const std::string& label, float width)
{
    auto button = std::make_shared<ya::UIButton>(name);
    button->_size         = {width, 30.0f};
    button->_normalColor  = kButtonNormal;
    button->_hoveredColor = kButtonHovered;
    button->_pressedColor = kButtonPressed;
    button->_focusedColor = kButtonFocused;

    auto text = std::make_shared<ya::UIText>(name + "_Label");
    text->_size     = {width, 30.0f};
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

} // namespace

void FWorkbenchApp::buildUI(ya::WidgetTree& tree)
{
    _tree = &tree;

    // Start with the mock document open, like a tool restoring its last
    // session; New / Open / Reload switch it afterwards.
    workspace.openDocument();

    _root = std::make_shared<ya::UIPanel>("WorkbenchRoot");
    _root->_anchorMin = {0.0f, 0.0f};
    _root->_anchorMax = {1.0f, 1.0f};
    _root->_color     = kWindowColor;
    tree.attachToLayer(ya::WidgetTree::ELayer::Content, _root);

    buildToolbar(tree, *_root);
    buildDocumentList(tree, *_root);
    buildCanvas(tree, *_root);
    buildInspector(tree, *_root);

    _statusText = std::make_shared<ya::UIText>("Status");
    _statusText->_anchorMin = {0.0f, 1.0f};
    _statusText->_anchorMax = {0.0f, 1.0f};
    _statusText->_position  = {16.0f, -40.0f};
    _statusText->_size      = {600.0f, 24.0f};
    _statusText->_fontSize  = 13;
    _statusText->_text      = "Arrow keys: navigate list | Tab: focus | Enter: activate";
    _statusText->_color     = kHeaderColor;
    tree.attach(*_root, _statusText);

    _commandResultText = std::make_shared<ya::UIText>("CommandResult");
    _commandResultText->_anchorMin = {1.0f, 1.0f};
    _commandResultText->_anchorMax = {1.0f, 1.0f};
    _commandResultText->_position  = {-400.0f, -40.0f};
    _commandResultText->_size      = {380.0f, 24.0f};
    _commandResultText->_fontSize  = 13;
    _commandResultText->_text      = "Ready";
    _commandResultText->_color     = {0.60f, 0.80f, 0.62f, 1.0f};
    _commandResultText->_hAlign    = ya::EWidgetAlignH::Right;
    tree.attach(*_root, _commandResultText);

    rebuildDocumentRows(tree);
    syncPresentationState();
}

void FWorkbenchApp::buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto toolbar = std::make_shared<ya::UIContainer>("Toolbar");
    toolbar->_anchorMin = {0.0f, 0.0f};
    toolbar->_anchorMax = {1.0f, 0.0f};
    toolbar->_position  = {0.0f, 8.0f};
    toolbar->_size      = {0.0f, 36.0f};
    toolbar->_direction = ya::EWidgetBoxLayout::Horizontal;
    toolbar->_spacing   = 8.0f;
    toolbar->_padding   = 8.0f;
    tree.attach(parent, toolbar);

    _newButton    = makeToolButton("New", "New", 72.0f);
    _openButton   = makeToolButton("Open", "Open", 72.0f);
    _saveButton   = makeToolButton("Save", "Save", 72.0f);
    _reloadButton = makeToolButton("Reload", "Reload", 84.0f);

    _newButton->_onClick    = [this] { workspace.newDocument();   _bRowsDirty = true; setCommandResult(workspace.commandResult); };
    _openButton->_onClick   = [this] { workspace.openDocument();  _bRowsDirty = true; setCommandResult(workspace.commandResult); };
    _saveButton->_onClick   = [this] { workspace.saveDocument();  setCommandResult(workspace.commandResult); };
    _reloadButton->_onClick = [this] { workspace.reloadDocument(); _bRowsDirty = true; setCommandResult(workspace.commandResult); };

    tree.attach(*toolbar, _newButton);
    tree.attach(*toolbar, _openButton);
    tree.attach(*toolbar, _saveButton);
    tree.attach(*toolbar, _reloadButton);
}

void FWorkbenchApp::buildDocumentList(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto listPanel = std::make_shared<ya::UIPanel>("DocumentList");
    listPanel->_anchorMin = {0.0f, 0.0625f};
    listPanel->_anchorMax = {0.22f, 0.94f};
    listPanel->_color     = kPanelColor;
    tree.attach(parent, listPanel);

    auto header = makeHeaderText("DOCUMENT");
    header->_position = {10.0f, 8.0f};
    tree.attach(*listPanel, header);

    auto scroll = std::make_shared<ya::UIScrollViewport>("DocumentScroll");
    scroll->_anchorMin = {0.0f, 0.0f};
    scroll->_anchorMax = {1.0f, 1.0f};
    scroll->_position  = {8.0f, 32.0f};
    scroll->_size      = {0.0f, 0.0f};
    tree.attach(*listPanel, scroll);
    _rowScroll = scroll;

    _rowList = std::make_shared<ya::UIContainer>("RowList");
    _rowList->_direction = ya::EWidgetBoxLayout::Vertical;
    _rowList->_spacing   = 2.0f;
    tree.attach(*scroll, _rowList);
}

void FWorkbenchApp::buildCanvas(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto canvasPanel = std::make_shared<ya::UIPanel>("PreviewCanvas");
    canvasPanel->_anchorMin = {0.24f, 0.0625f};
    canvasPanel->_anchorMax = {0.76f, 0.94f};
    canvasPanel->_color     = kCanvasColor;
    tree.attach(parent, canvasPanel);

    auto header = makeHeaderText("PREVIEW");
    header->_position = {10.0f, 8.0f};
    tree.attach(*canvasPanel, header);

    _highlightPanel = std::make_shared<ya::UIPanel>("SelectionHighlight");
    _highlightPanel->_anchorMin = {0.5f, 0.5f};
    _highlightPanel->_anchorMax = {0.5f, 0.5f};
    _highlightPanel->_position  = {-70.0f, -45.0f};
    _highlightPanel->_size      = {140.0f, 90.0f};
    _highlightPanel->_color     = {0.35f, 0.55f, 0.90f, 1.0f};
    tree.attach(*canvasPanel, _highlightPanel);

    _previewName = std::make_shared<ya::UIText>("PreviewName");
    _previewName->_position = {0.0f, 0.0f};
    _previewName->_size     = {400.0f, 24.0f};
    _previewName->_fontSize = 16;
    _previewName->_text     = "";
    _previewName->_color    = {0.95f, 0.96f, 0.98f, 1.0f};
    _previewName->_hAlign   = ya::EWidgetAlignH::Center;
    tree.attach(*_highlightPanel, _previewName);
}

void FWorkbenchApp::buildInspector(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto inspectorPanel = std::make_shared<ya::UIPanel>("Inspector");
    inspectorPanel->_anchorMin = {0.78f, 0.0625f};
    inspectorPanel->_anchorMax = {1.0f, 0.94f};
    inspectorPanel->_color     = kPanelColor;
    tree.attach(parent, inspectorPanel);

    auto header = makeHeaderText("INSPECTOR");
    header->_position = {10.0f, 8.0f};
    tree.attach(*inspectorPanel, header);

    auto form = std::make_shared<ya::UIContainer>("InspectorForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->_position  = {10.0f, 36.0f};
    form->_size      = {0.0f, 0.0f};
    form->_direction = ya::EWidgetBoxLayout::Vertical;
    form->_spacing   = 6.0f;
    tree.attach(*inspectorPanel, form);

    auto nameLabel = std::make_shared<ya::UIText>("NameLabel");
    nameLabel->_size     = {200.0f, 18.0f};
    nameLabel->_fontSize = 13;
    nameLabel->_text     = "Name";
    nameLabel->_color    = kHeaderColor;
    tree.attach(*form, nameLabel);

    _nameField = std::make_shared<ya::UITextField>("NameField");
    _nameField->_size     = {200.0f, 26.0f};
    _nameField->_fontSize = 14;
    _nameField->_onTextChanged = [this](const std::string& text) {
        workspace.renameSelected(text);
        setCommandResult(workspace.bDirty ? "Editing: name changed" : "Editing");
    };
    _nameField->_onCommit = [this](const std::string& text) {
        workspace.renameSelected(text);
        setCommandResult("Inspector: committed name");
    };
    tree.attach(*form, _nameField);

    auto visibleLabel = makeHeaderText("Visible");
    tree.attach(*form, visibleLabel);

    _visibleToggle = makeToolButton("VisibleToggle", "Visible: on", 110.0f);
    _visibleToggle->_size = {110.0f, 24.0f};
    _visibleToggle->_onClick = [this] {
        workspace.toggleSelectedVisible();
        setCommandResult(workspace.bDirty ? "Inspector: visibility toggled" : "Inspector: no selection");
    };
    tree.attach(*form, _visibleToggle);

    auto colorLabel = makeHeaderText("Color");
    tree.attach(*form, colorLabel);

    _colorCycle = makeToolButton("ColorCycle", "Cycle Color", 110.0f);
    _colorCycle->_size = {110.0f, 24.0f};
    _colorCycle->_onClick = [this] {
        workspace.cycleSelectedColor();
        setCommandResult(workspace.bDirty ? "Inspector: color changed" : "Inspector: no selection");
    };
    tree.attach(*form, _colorCycle);

    auto sizeLabel = makeHeaderText("Size");
    tree.attach(*form, sizeLabel);

    _sizeGrow = makeToolButton("SizeGrow", "Grow +20", 90.0f);
    _sizeGrow->_size = {90.0f, 24.0f};
    _sizeGrow->_onClick = [this] {
        workspace.stepSelectedSize({20.0f, 20.0f});
        setCommandResult(workspace.bDirty ? "Inspector: size changed" : "Inspector: no selection");
    };
    tree.attach(*form, _sizeGrow);

    _sizeShrink = makeToolButton("SizeShrink", "Shrink -20", 100.0f);
    _sizeShrink->_size = {100.0f, 24.0f};
    _sizeShrink->_onClick = [this] {
        workspace.stepSelectedSize({-20.0f, -20.0f});
        setCommandResult(workspace.bDirty ? "Inspector: size changed" : "Inspector: no selection");
    };
    tree.attach(*form, _sizeShrink);

    _visibleValue = std::make_shared<ya::UIText>("VisibleValue");
    _visibleValue->_size     = {200.0f, 14.0f};
    _visibleValue->_fontSize = 12;
    _visibleValue->_text     = "";
    _visibleValue->_color    = kHeaderColor;
    tree.attach(*form, _visibleValue);

    _colorValue = std::make_shared<ya::UIText>("ColorValue");
    _colorValue->_size     = {200.0f, 14.0f};
    _colorValue->_fontSize = 12;
    _colorValue->_text     = "";
    _colorValue->_color    = kHeaderColor;
    tree.attach(*form, _colorValue);

    _sizeValue = std::make_shared<ya::UIText>("SizeValue");
    _sizeValue->_size     = {200.0f, 14.0f};
    _sizeValue->_fontSize = 12;
    _sizeValue->_text     = "";
    _sizeValue->_color    = kHeaderColor;
    tree.attach(*form, _sizeValue);
}

void FWorkbenchApp::rebuildDocumentRows(ya::WidgetTree& tree)
{
    // Frame-boundary subtree rebuild: detach the old list, then rebuild rows
    // from the workspace document. Snapshot resources of the old subtree are
    // released by the frame's retention rules (never during recording).
    if (_rowList && _rowList->isAttached()) {
        tree.detach(*_rowList);
    }
    _rowList = std::make_shared<ya::UIContainer>("RowList");
    _rowList->_direction = ya::EWidgetBoxLayout::Vertical;
    _rowList->_spacing   = 2.0f;
    tree.attach(*_rowScroll, _rowList);
    _rows.clear();

    for (const auto& item : workspace.items) {
        auto row = std::make_shared<ya::UISelectableRow>("Row_" + item.id);
        row->_itemId   = item.id;
        row->_size     = {220.0f, 24.0f};
        row->_onSelect = [this](const std::string& id) {
            workspace.select(id);
            setCommandResult("List: selected '" + id + "'");
        };
        row->_onActivate = [this](const std::string& id) {
            const FWorkbenchItem* item = nullptr;
            for (const auto& candidate : workspace.items) {
                if (candidate.id == id) {
                    item = &candidate;
                    break;
                }
            }
            setCommandResult(item ? std::format("List: activated '{}'", item->name)
                                  : std::format("List: activated '{}'", id));
        };

        auto label = std::make_shared<ya::UIText>("RowLabel_" + item.id);
        label->_size     = {220.0f, 24.0f};
        label->_fontSize = 14;
        label->_text     = item.bVisible ? item.name : item.name + " (hidden)";
        label->_color    = {0.90f, 0.92f, 0.95f, 1.0f};
        label->_vAlign   = ya::EWidgetAlignV::Center;
        tree.attach(*_rowList, row);
        tree.attach(*row, label);
        _rows.push_back(row);
    }
    _bRowsDirty = false;
}

void FWorkbenchApp::syncPresentationState()
{
    if (!_tree) {
        return;
    }

    if (_bRowsDirty) {
        rebuildDocumentRows(*_tree);
    }

    const FWorkbenchItem* selected = workspace.getSelected();

    // Rows: presenter-owned selected state.
    for (const auto& row : _rows) {
        row->_bSelected = (selected != nullptr && row->_itemId == selected->id);
    }

    // Preview: highlight + name follow the selection.
    if (selected) {
        _highlightPanel->_visibility = ya::EWidgetVisibility::Visible;
        _highlightPanel->_size       = selected->size;
        _highlightPanel->_position   = -selected->size * 0.5f;
        _highlightPanel->_color      = selected->bVisible ? selected->color
                                                          : glm::vec4(selected->color.r, selected->color.g,
                                                                      selected->color.b, 0.35f);
        _previewName->_text = selected->bVisible ? selected->name : selected->name + " (hidden)";
    }
    else {
        _highlightPanel->_visibility = ya::EWidgetVisibility::Hidden;
        _previewName->_text          = "(no selection)";
    }

    // Inspector values.
    _nameField->_text = selected ? selected->name : "";
    _nameField->clampCursor(); // buffer replaced by the presenter: keep the caret valid
    _visibleValue->_text = selected ? (selected->bVisible ? "state: visible" : "state: hidden") : "state: -";
    _colorValue->_text   = selected ? std::format("rgba({:.2f}, {:.2f}, {:.2f})",
                                                  selected->color.r, selected->color.g, selected->color.b)
                                    : "rgba(-)";
    _sizeValue->_text    = selected ? std::format("{} x {}", static_cast<int>(selected->size.x),
                                                  static_cast<int>(selected->size.y))
                                    : "-";
}

void FWorkbenchApp::onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result)
{
    // Unhandled keyboard events reach the app layer: arrow keys navigate the
    // document list (workspace selection), everything else is ignored.
    if (result == ya::EWidgetRouteResult::NotHandled &&
        event.getEventType() == ya::EEvent::KeyPressed) {
        handleUnhandledKey(static_cast<const ya::KeyPressedEvent&>(event));
    }
}

void FWorkbenchApp::handleUnhandledKey(const ya::KeyPressedEvent& keyEvent)
{
    if (keyEvent.bRepeat) {
        return;
    }
    if (keyEvent._keyCode == ya::EKey::Down) {
        workspace.selectRelative(1);
        setCommandResult("List: navigated");
    }
    else if (keyEvent._keyCode == ya::EKey::Up) {
        workspace.selectRelative(-1);
        setCommandResult("List: navigated");
    }
}

void FWorkbenchApp::updateUI()
{
    syncPresentationState();
    ++_frame;
    if (bSmokeActions && !_bAutomationDone) {
        runAutomation();
    }
}

void FWorkbenchApp::setCommandResult(const std::string& text)
{
    workspace.commandResult = text;
    if (_commandResultText) {
        _commandResultText->_text = text;
    }
}

void FWorkbenchApp::dispatchPointer(const ya::Event& event, const glm::vec2& point)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = point;
    _tree->dispatchEvent(event, ctx);
}

void FWorkbenchApp::dispatchKey(const ya::Event& event)
{
    // Faithful simulation of GUIAppHost::dispatchToTree: dispatch, then feed
    // the route result back to the delegate observation hook.
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = {-1.0f, -1.0f};
    onRoutedEvent(event, _tree->dispatchEvent(event, ctx));
}

void FWorkbenchApp::runAutomation()
{
    switch (_frame) {
    case 3: {
        // 1. Pointer: select the Sphere row (real hit walk + capture path).
        if (_rows.size() < 2u) {
            YA_CORE_ERROR("Workbench automation: expected at least 2 document rows");
            _bAutomationDone = true;
            return;
        }
        const glm::vec2 rowCenter = _rows[1]->_layoutRect.pos + _rows[1]->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), rowCenter);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), rowCenter);
        if (workspace.selectedId != "item.sphere") {
            YA_CORE_ERROR("Workbench automation: row click did not select 'item.sphere'");
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 4: {
        // 2. Keyboard: focus the name field, jump to the end, type " V2".
        dispatchPointer(ya::MouseButtonPressedEvent(0),
                        _nameField->_layoutRect.pos + _nameField->_layoutRect.extent * 0.5f);
        ya::KeyPressedEvent end{};
        end._keyCode = ya::EKey::End;
        dispatchKey(end);
        dispatchKey(ya::KeyTypedEvent(" V2"));
        if (_nameField->_text != "Sphere V2" || !workspace.bDirty) {
            YA_CORE_ERROR("Workbench automation: text edit failed ('{}', dirty={})",
                          _nameField->_text, workspace.bDirty);
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 5: {
        // 3. Pointer: Save command (dirty -> clean + feedback).
        const glm::vec2 saveCenter = _saveButton->_layoutRect.pos + _saveButton->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), saveCenter);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), saveCenter);
        if (workspace.bDirty || workspace.commandResult != "Save: saved changes") {
            YA_CORE_ERROR("Workbench automation: save command failed ('{}')", workspace.commandResult);
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 6: {
        // 4. Keyboard: unhandled Down arrow navigates the list.
        ya::KeyPressedEvent down{};
        down._keyCode = ya::EKey::Down;
        dispatchKey(down);
        if (workspace.selectedId != "item.light") {
            YA_CORE_ERROR("Workbench automation: arrow navigation failed (selected '{}')",
                          workspace.selectedId);
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 7: {
        // 5. Verify the presenter synced everything after frame 6's sync.
        const FWorkbenchItem* selected = workspace.getSelected();
        const bool bNameSynced    = _nameField->_text == "Light";
        const bool bRowsSynced    = std::any_of(_rows.begin(), _rows.end(), [](const auto& row) {
            return row->_itemId == "item.light" && row->_bSelected;
        });
        const bool bPreviewSynced = _previewName->_text == "Light (hidden)";
        const bool bStatusSynced  = _commandResultText->_text == "List: navigated";
        if (!selected || !bNameSynced || !bRowsSynced || !bPreviewSynced || !bStatusSynced) {
            YA_CORE_ERROR("Workbench automation: presenter sync incomplete "
                          "(name={} rows={} preview={} status={})",
                          bNameSynced, bRowsSynced, bPreviewSynced, bStatusSynced);
            _bAutomationDone = true;
            return;
        }
        _bSmokePassed   = true;
        _bAutomationDone = true;
        YA_CORE_INFO("Workbench automation PASSED: selection -> edit -> save -> navigate -> sync");
        break;
    }
    default:
        break;
    }
}

} // namespace guiworkbench

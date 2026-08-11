#include "GUIWorkbench.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
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
constexpr glm::vec4 kCanvasColor        = {0.05f, 0.055f, 0.07f, 1.0f};
constexpr glm::vec4 kHeaderColor        = {0.55f, 0.60f, 0.68f, 1.0f};
constexpr glm::vec4 kButtonNormal       = {0.20f, 0.22f, 0.27f, 1.0f};
constexpr glm::vec4 kButtonHovered      = {0.27f, 0.30f, 0.37f, 1.0f};
constexpr glm::vec4 kButtonPressed      = {0.14f, 0.16f, 0.20f, 1.0f};
constexpr glm::vec4 kButtonFocused      = {0.24f, 0.46f, 0.82f, 1.0f};

std::string shortTypeName(const std::string& typeId)
{
    const size_t dot = typeId.find_last_of('.');
    return dot == std::string::npos ? typeId : typeId.substr(dot + 1);
}

std::shared_ptr<ya::UIButton> makeToolButton(const std::string& name, const std::string& label, float width)
{
    auto button = std::make_shared<ya::UIButton>(name);
    button->_size         = {width, 28.0f};
    button->_normalColor  = kButtonNormal;
    button->_hoveredColor = kButtonHovered;
    button->_pressedColor = kButtonPressed;
    button->_focusedColor = kButtonFocused;

    auto text = std::make_shared<ya::UIText>(name + "_Label");
    text->_size     = {width, 28.0f};
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

    if (!startupDocumentPath.empty()) {
        workspace.openDocument(startupDocumentPath);
        startupDocumentPath.clear();
    }

    _root = std::make_shared<ya::UIPanel>("WorkbenchRoot");
    _root->_anchorMin = {0.0f, 0.0f};
    _root->_anchorMax = {1.0f, 1.0f};
    _root->_color     = kWindowColor;
    tree.attachToLayer(ya::WidgetTree::ELayer::Content, _root);

    buildToolbar(tree, *_root);
    buildDocumentList(tree, *_root);
    buildCanvas(tree, *_root);
    buildInspector(tree, *_root);
    buildPalette(tree, *_root);

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

    _bPreviewDirty = true;
    _bRowsDirty    = true;
}

void FWorkbenchApp::buildToolbar(ya::WidgetTree& tree, ya::UIElement& parent)
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

    _newButton  = makeToolButton("New", "New", 64.0f);
    _saveButton = makeToolButton("Save", "Save", 64.0f);
    _newButton->_onClick  = [this] { cmdNew(); };
    _saveButton->_onClick = [this] { cmdSave(); };
    tree.attach(*toolbar, _newButton);
    tree.attach(*toolbar, _saveButton);

    // Document path row: Open / Save As through the shared text field.
    auto pathRow = std::make_shared<ya::UIContainer>("PathRow");
    pathRow->_anchorMin = {0.0f, 0.0f};
    pathRow->_anchorMax = {1.0f, 0.0f};
    pathRow->_position  = {0.0f, 46.0f};
    pathRow->_size      = {0.0f, 30.0f};
    pathRow->_direction = ya::EWidgetBoxLayout::Horizontal;
    pathRow->_spacing   = 6.0f;
    pathRow->_padding   = 8.0f;
    tree.attach(parent, pathRow);

    _pathField = std::make_shared<ya::UITextField>("PathField");
    _pathField->_size     = {0.0f, 26.0f};
    _pathField->_fontSize = 13;
    _pathField->_text     = "Engine/Saved/GUIWorkbench/untitled.yaui";
    tree.attach(*pathRow, _pathField);

    _openButton  = makeToolButton("Open", "Open", 64.0f);
    _saveAsButton = makeToolButton("SaveAs", "Save As", 76.0f);
    _openButton->_onClick  = [this] { cmdOpen(); };
    _saveAsButton->_onClick = [this] { cmdSaveAs(); };
    tree.attach(*pathRow, _openButton);
    tree.attach(*pathRow, _saveAsButton);
}

void FWorkbenchApp::buildDocumentList(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto listPanel = std::make_shared<ya::UIPanel>("DocumentList");
    listPanel->_anchorMin = {0.0f, 0.12f};
    listPanel->_anchorMax = {0.24f, 0.955f};
    listPanel->_color     = kPanelColor;
    tree.attach(parent, listPanel);

    auto header = makeHeaderText("DOCUMENT");
    header->_position = {10.0f, 8.0f};
    tree.attach(*listPanel, header);

    auto scroll = std::make_shared<ya::UIScrollViewport>("DocumentScroll");
    scroll->_anchorMin = {0.0f, 0.0f};
    scroll->_anchorMax = {1.0f, 1.0f};
    scroll->_position  = {8.0f, 34.0f};
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
    _canvasPanel = std::make_shared<ya::UIPanel>("PreviewCanvas");
    _canvasPanel->_anchorMin = {0.26f, 0.12f};
    _canvasPanel->_anchorMax = {0.68f, 0.955f};
    _canvasPanel->_color     = kCanvasColor;
    tree.attach(parent, _canvasPanel);

    auto header = makeHeaderText("PREVIEW");
    header->_position = {10.0f, 8.0f};
    tree.attach(*_canvasPanel, header);

    _highlightPanel = std::make_shared<ya::UIPanel>("SelectionHighlight");
    _highlightPanel->_zOrder   = 1000; // above the edited document
    _highlightPanel->_position = {0.0f, 0.0f};
    _highlightPanel->_size     = {0.0f, 0.0f};
    _highlightPanel->_color    = {0.90f, 0.62f, 0.15f, 0.45f};
    tree.attach(*_canvasPanel, _highlightPanel);

    _previewName = std::make_shared<ya::UIText>("PreviewName");
    _previewName->_position = {8.0f, 8.0f};
    _previewName->_size     = {400.0f, 24.0f};
    _previewName->_fontSize = 15;
    _previewName->_text     = "(no document)";
    _previewName->_color    = {0.95f, 0.96f, 0.98f, 1.0f};
    tree.attach(*_canvasPanel, _previewName);
}

void FWorkbenchApp::buildInspector(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto inspectorPanel = std::make_shared<ya::UIPanel>("Inspector");
    inspectorPanel->_anchorMin = {0.70f, 0.12f};
    inspectorPanel->_anchorMax = {1.0f, 0.955f};
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

    _typeValue = std::make_shared<ya::UIText>("TypeValue");
    _typeValue->_size     = {220.0f, 18.0f};
    _typeValue->_fontSize = 13;
    _typeValue->_text     = "-";
    _typeValue->_color    = kHeaderColor;
    tree.attach(*form, _typeValue);

    auto nameLabel = makeHeaderText("Name");
    tree.attach(*form, nameLabel);

    _nameField = std::make_shared<ya::UITextField>("NameField");
    _nameField->_size     = {220.0f, 26.0f};
    _nameField->_fontSize = 14;
    _nameField->_onCommit = [this](const std::string& text) { renameSelected(text); };
    tree.attach(*form, _nameField);

    auto visibleLabel = makeHeaderText("Visible");
    tree.attach(*form, visibleLabel);

    _visibleToggle = makeToolButton("VisibleToggle", "Visible: on", 110.0f);
    _visibleToggle->_size = {110.0f, 24.0f};
    _visibleToggle->_onClick = [this] { toggleSelectedVisible(); };
    tree.attach(*form, _visibleToggle);

    auto sizeLabel = makeHeaderText("Size");
    tree.attach(*form, sizeLabel);

    auto sizeRow = std::make_shared<ya::UIContainer>("SizeRow");
    sizeRow->_direction = ya::EWidgetBoxLayout::Horizontal;
    sizeRow->_spacing   = 6.0f;
    tree.attach(*form, sizeRow);

    _sizeGrow = makeToolButton("SizeGrow", "Grow +20", 90.0f);
    _sizeGrow->_size = {90.0f, 24.0f};
    _sizeGrow->_onClick = [this] { stepSelectedSize({20.0f, 20.0f}); };
    tree.attach(*sizeRow, _sizeGrow);

    _sizeShrink = makeToolButton("SizeShrink", "Shrink -20", 100.0f);
    _sizeShrink->_size = {100.0f, 24.0f};
    _sizeShrink->_onClick = [this] { stepSelectedSize({-20.0f, -20.0f}); };
    tree.attach(*sizeRow, _sizeShrink);

    _sizeValue = std::make_shared<ya::UIText>("SizeValue");
    _sizeValue->_size     = {220.0f, 14.0f};
    _sizeValue->_fontSize = 12;
    _sizeValue->_text     = "-";
    _sizeValue->_color    = kHeaderColor;
    tree.attach(*form, _sizeValue);

    _deleteButton = makeToolButton("Delete", "Delete Selected", 130.0f);
    _deleteButton->_size = {130.0f, 24.0f};
    _deleteButton->_onClick = [this] { deleteSelected(); };
    tree.attach(*form, _deleteButton);
}

void FWorkbenchApp::buildPalette(ya::WidgetTree& tree, ya::UIElement& parent)
{
    auto palettePanel = std::make_shared<ya::UIPanel>("Palette");
    palettePanel->_anchorMin = {0.0f, 0.0f};
    palettePanel->_anchorMax = {1.0f, 0.0f};
    palettePanel->_position  = {0.0f, 0.0f};
    palettePanel->_size      = {0.0f, 56.0f};
    palettePanel->_color     = kPanelColor;
    tree.attach(parent, palettePanel);

    auto header = makeHeaderText("PALETTE");
    header->_position = {10.0f, 6.0f};
    tree.attach(*palettePanel, header);

    _paletteList = std::make_shared<ya::UIContainer>("PaletteList");
    _paletteList->_anchorMin = {0.0f, 0.0f};
    _paletteList->_anchorMax = {1.0f, 0.0f}; // horizontal span only; fixed 30px height
    _paletteList->_position  = {10.0f, 26.0f};
    _paletteList->_size      = {0.0f, 30.0f};
    _paletteList->_direction = ya::EWidgetBoxLayout::Horizontal;
    _paletteList->_spacing   = 6.0f;
    tree.attach(*palettePanel, _paletteList);

    for (const std::string& typeId : ya::UITypeRegistry::instance().getTypeIds()) {
        auto button = makeToolButton("Palette_" + typeId, shortTypeName(typeId), 76.0f);
        button->_size = {76.0f, 24.0f};
        button->_onClick = [this, typeId] { addWidgetFromPalette(typeId); };
        tree.attach(*_paletteList, button);
    }
}

// === Document sync ===

std::string FWorkbenchApp::widgetPathOf(const ya::UIElement& root, const ya::UIElement& widget)
{
    std::string path;
    for (const ya::UIElement* node = &widget; node != nullptr; node = node->getParent()) {
        if (node == &root) {
            return path;
        }
        const ya::UIElement* parent = node->getParent();
        if (!parent) {
            return {};
        }
        size_t index = 0;
        for (const auto& ref : parent->getChildren()) {
            if (ref.get() == node) {
                break;
            }
            ++index;
        }
        path = path.empty() ? std::to_string(index) : std::to_string(index) + "." + path;
    }
    return {};
}

ya::UIElement* FWorkbenchApp::findPreviewWidget(const std::string& path)
{
    if (!_previewRoot || path.empty()) {
        return _previewRoot.get();
    }
    ya::UIElement* node = _previewRoot.get();
    size_t         offset = 0;
    while (offset < path.size()) {
        const size_t dot = path.find('.', offset);
        const std::string segment = path.substr(offset, dot == std::string::npos ? std::string::npos : dot - offset);
        const size_t index = static_cast<size_t>(std::stoull(segment));
        if (index >= node->getChildren().size()) {
            return nullptr;
        }
        node = node->getChildren()[index].get();
        offset = (dot == std::string::npos) ? path.size() : dot + 1;
    }
    return node;
}

ya::UIElement* FWorkbenchApp::getSelectedWidget()
{
    if (!workspace.document) {
        return nullptr;
    }
    return findPreviewWidget(workspace.getSelectedPath());
}

void FWorkbenchApp::rebuildPreviewTree()
{
    if (!_bPreviewDirty) {
        return;
    }
    if (_previewRoot && _previewRoot->isAttached()) {
        _tree->detach(*_previewRoot);
    }
    _previewRoot.reset();

    if (workspace.document) {
        _previewRoot = workspace.document->instantiate();
        if (_previewRoot) {
            _tree->attach(*_canvasPanel, _previewRoot);
            if (workspace.getSelectedPath().empty()) {
                workspace.select("");
            }
        }
    }
    _bPreviewDirty = false;
    _bRowsDirty    = true;
}

void FWorkbenchApp::rebuildDocumentRows()
{
    if (_rowList && _rowList->isAttached()) {
        _tree->detach(*_rowList);
    }
    _rowList = std::make_shared<ya::UIContainer>("RowList");
    _rowList->_direction = ya::EWidgetBoxLayout::Vertical;
    _rowList->_spacing   = 2.0f;
    _tree->attach(*_rowScroll, _rowList);
    _rows.clear();

    for (const FDocumentRow& row : workspace.flattenRows()) {
        auto rowWidget = std::make_shared<ya::UISelectableRow>("Row_" + row.path);
        rowWidget->_itemId = row.path;
        rowWidget->_size   = {240.0f, 22.0f};
        rowWidget->_onSelect = [this](const std::string& path) {
            workspace.select(path);
            setCommandResult("List: selected '" + path + "'");
        };
        rowWidget->_onActivate = [this](const std::string& path) {
            workspace.select(path);
            setCommandResult("List: activated '" + path + "'");
        };

        auto label = std::make_shared<ya::UIText>("RowLabel_" + row.path);
        label->_size     = {240.0f, 22.0f};
        label->_position = {4.0f + static_cast<float>(row.depth) * 14.0f, 0.0f};
        label->_fontSize = 13;
        label->_text     = row.name + "  [" + shortTypeName(row.typeId) + "]";
        label->_color    = {0.88f, 0.90f, 0.94f, 1.0f};
        label->_vAlign   = ya::EWidgetAlignV::Center;

        _tree->attach(*_rowList, rowWidget);
        _tree->attach(*rowWidget, label);
        _rows.push_back(rowWidget);
    }
    _bRowsDirty = false;
}

void FWorkbenchApp::syncPresentationState()
{
    if (!_tree) {
        return;
    }
    if (_bPreviewDirty) {
        rebuildPreviewTree();
    }
    if (_bRowsDirty) {
        rebuildDocumentRows();
    }

    const auto rows = workspace.flattenRows();
    if (_rows.size() == rows.size()) {
        for (size_t i = 0; i < _rows.size(); ++i) {
            auto* label = _rows[i]->getChildren().empty()
                              ? nullptr
                              : dynamic_cast<ya::UIText*>(_rows[i]->getChildren()[0].get());
            if (label) {
                label->_text = rows[i].name + "  [" + shortTypeName(rows[i].typeId) + "]";
            }
            _rows[i]->_bSelected = (rows[i].path == workspace.getSelectedPath());
        }
    }

    // Preview highlight + label.
    ya::UIElement* selected = getSelectedWidget();
    if (selected && _canvasPanel && selected->isAttached()) {
        const glm::vec2 relative = selected->_layoutRect.pos - _canvasPanel->_layoutRect.pos;
        _highlightPanel->_visibility = ya::EWidgetVisibility::Visible;
        _highlightPanel->_position   = relative;
        _highlightPanel->_size       = selected->_layoutRect.extent;
        _previewName->_text = selected->_name + "  [" + shortTypeName(selected->_typeId) + "]";
    }
    else {
        _highlightPanel->_visibility = ya::EWidgetVisibility::Hidden;
        _previewName->_text          = workspace.document ? "(no selection)" : "(no document)";
    }

    // Inspector values.
    // Never overwrite the buffer while the user is typing (commit owns the
    // field -> instance -> document sync).
    if (_tree->getFocused() != _nameField.get()) {
        _nameField->_text = selected ? selected->_name : "";
        _nameField->clampCursor();
    }
    if (selected) {
        _typeValue->_text = "type: " + (selected->_typeId.empty() ? "Widget" : selected->_typeId);
        _sizeValue->_text = std::format("{} x {}", static_cast<int>(selected->_size.x),
                                        static_cast<int>(selected->_size.y));
        if (!_visibleToggle->getChildren().empty()) {
            if (auto* label = dynamic_cast<ya::UIText*>(_visibleToggle->getChildren()[0].get())) {
                label->_text = selected->_visibility == ya::EWidgetVisibility::Visible ? "Visible: on"
                                                                                       : "Visible: off";
            }
        }
    }
    else {
        _typeValue->_text = "-";
        _sizeValue->_text = "-";
    }

    _pathField->clampCursor();
    if (_commandResultText) {
        _commandResultText->_text = workspace.commandResult;
    }
}

// === Commands ===

void FWorkbenchApp::cmdNew()
{
    workspace.newDocument("engine.panel");
    _bPreviewDirty = true;
    setCommandResult(workspace.commandResult);
}

void FWorkbenchApp::cmdOpen()
{
    const bool bOpened = workspace.openDocument(_pathField->_text);
    _bPreviewDirty = bOpened;
    setCommandResult(workspace.commandResult);
}

void FWorkbenchApp::cmdSave()
{
    if (workspace.document && _previewRoot) {
        workspace.rebuildFromPreview(*_previewRoot);
    }
    const bool bSaved = workspace.saveDocument();
    setCommandResult(workspace.commandResult);
    (void)bSaved;
}

void FWorkbenchApp::cmdSaveAs()
{
    if (workspace.document && _previewRoot) {
        workspace.rebuildFromPreview(*_previewRoot);
    }
    const bool bSaved = workspace.saveDocumentAs(_pathField->_text);
    setCommandResult(workspace.commandResult);
    (void)bSaved;
}

void FWorkbenchApp::addWidgetFromPalette(const std::string& typeId)
{
    if (!_previewRoot || !_previewRoot->isAttached()) {
        setCommandResult("Palette: open a document first");
        return;
    }
    ya::UIElementRef widget = ya::UITypeRegistry::instance().createInstance(typeId);
    if (!widget) {
        setCommandResult("Palette: unknown type " + typeId);
        return;
    }
    ya::UIElement* parent = getSelectedWidget();
    if (!parent) {
        parent = _previewRoot.get();
    }
    widget->_name = shortTypeName(typeId);
    _tree->attach(*parent, widget);
    workspace.rebuildFromPreview(*_previewRoot);
    workspace.select(widgetPathOf(*_previewRoot, *widget));
    workspace.recordMutation();
    _bRowsDirty = true;
    setCommandResult("Palette: added " + typeId);
}

void FWorkbenchApp::deleteSelected()
{
    ya::UIElement* selected = getSelectedWidget();
    if (!selected) {
        setCommandResult("Delete: nothing selected");
        return;
    }
    if (selected == _previewRoot.get()) {
        setCommandResult("Delete: document root is protected");
        return;
    }
    const std::string name = selected->_name;
    _tree->detach(*selected);
    workspace.rebuildFromPreview(*_previewRoot);
    workspace.recordMutation();
    _bRowsDirty = true;
    setCommandResult("Delete: '" + name + "'");
}

void FWorkbenchApp::renameSelected(const std::string& name)
{
    if (ya::UIElement* selected = getSelectedWidget()) {
        selected->_name = name;
        workspace.rebuildFromPreview(*_previewRoot);
        workspace.recordMutation();
        setCommandResult("Inspector: name = '" + name + "'");
    }
}

void FWorkbenchApp::toggleSelectedVisible()
{
    if (ya::UIElement* selected = getSelectedWidget()) {
        selected->_visibility = (selected->_visibility == ya::EWidgetVisibility::Visible)
                                    ? ya::EWidgetVisibility::Hidden
                                    : ya::EWidgetVisibility::Visible;
        workspace.recordMutation();
        setCommandResult("Inspector: visibility toggled");
    }
}

void FWorkbenchApp::stepSelectedSize(const glm::vec2& delta)
{
    if (ya::UIElement* selected = getSelectedWidget()) {
        selected->_size = glm::max(glm::vec2(20.0f), selected->_size + delta);
        workspace.recordMutation();
        setCommandResult("Inspector: size changed");
    }
}

void FWorkbenchApp::setCommandResult(const std::string& text)
{
    workspace.commandResult = text;
    if (_commandResultText) {
        _commandResultText->_text = text;
    }
}

// === Input ===

void FWorkbenchApp::updateUI()
{
    syncPresentationState();
    ++_frame;
    if (bSmokeActions && !_bAutomationDone) {
        runAutomation();
    }
}

void FWorkbenchApp::onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result)
{
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

// === Automation smoke ===

void FWorkbenchApp::dispatchPointer(const ya::Event& event, const glm::vec2& point)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = point;
    _tree->dispatchEvent(event, ctx);
}

void FWorkbenchApp::dispatchKey(const ya::Event& event)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = {-1.0f, -1.0f};
    onRoutedEvent(event, _tree->dispatchEvent(event, ctx));
}

void FWorkbenchApp::runAutomation()
{
    switch (_frame) {
    case 3: {
        // 1. New document.
        const glm::vec2 center = _newButton->_layoutRect.pos + _newButton->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
        if (!workspace.document) {
            YA_CORE_ERROR("Workbench automation: New failed");
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 4: {
        // 2. Palette: add a button under the selected root.
        if (_paletteList->getChildren().empty()) {
            YA_CORE_ERROR("Workbench automation: palette missing button type");
            _bAutomationDone = true;
            return;
        }
        // Palette rows are sorted registry type ids: engine.button is first.
        auto* button = dynamic_cast<ya::UIButton*>(_paletteList->getChildren()[0].get());
        if (!button) {
            YA_CORE_ERROR("Workbench automation: palette row 2 is not a button");
            _bAutomationDone = true;
            return;
        }
        const glm::vec2 center = button->_layoutRect.pos + button->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
        if (workspace.flattenRows().size() != 2u) {
            YA_CORE_ERROR("Workbench automation: palette add failed (rows={})",
                          workspace.flattenRows().size());
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 5: {
        // 3. Rename the selected (new) button through the inspector field.
        dispatchPointer(ya::MouseButtonPressedEvent(0),
                        _nameField->_layoutRect.pos + _nameField->_layoutRect.extent * 0.5f);
        ya::KeyPressedEvent end{};
        end._keyCode = ya::EKey::End;
        dispatchKey(end);
        dispatchKey(ya::KeyTypedEvent("_OK"));
        // Commit through the field's own Enter path (same as a user).
        ya::KeyPressedEvent enter{};
        enter._keyCode = ya::EKey::Enter;
        dispatchKey(enter);
        if (_nameField->_text != "button_OK") {
            ya::UIElement* selected = getSelectedWidget();
            YA_CORE_ERROR("Workbench automation: rename failed ('{}' selected='{}' path='{}')",
                          _nameField->_text,
                          selected ? selected->_name : "<none>",
                          workspace.getSelectedPath());
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 6: {
        // 4. Save As through the path field (simulated typed path).
        _pathField->_text = "Engine/Saved/GUIWorkbench/smoke.yaui";
        const glm::vec2 center = _saveAsButton->_layoutRect.pos + _saveAsButton->_layoutRect.extent * 0.5f;
        dispatchPointer(ya::MouseButtonPressedEvent(0), center);
        if (!_saveAsButton->_bPressed) {
            YA_CORE_ERROR("Workbench automation: SaveAs press was not consumed");
            _bAutomationDone = true;
            return;
        }
        dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
        if (workspace.bDirty || workspace.documentPath != "Engine/Saved/GUIWorkbench/smoke.yaui") {
            YA_CORE_ERROR("Workbench automation: save failed ('{}', dirty={})",
                          workspace.commandResult, workspace.bDirty);
            YA_CORE_ERROR("Workbench automation: saveAs rect={} {} {}x{}",
                          _saveAsButton->_layoutRect.pos.x, _saveAsButton->_layoutRect.pos.y,
                          _saveAsButton->_layoutRect.extent.x, _saveAsButton->_layoutRect.extent.y);
            _bAutomationDone = true;
            return;
        }
        break;
    }
    case 7: {
        // 5. Verify: two rows, selection on the renamed button, file on disk.
        const auto rows = workspace.flattenRows();
        std::string content;
        const bool  bFile = VirtualFileSystem::get() &&
                           VirtualFileSystem::get()->readFileToString(workspace.documentPath, content);
        const bool bRowsOk    = rows.size() == 2u && rows[1].name == "button_OK";
        const bool bSelected  = workspace.getSelectedPath() == "0";
        const bool bDirty     = !workspace.bDirty;
        if (!bFile || !bRowsOk || !bSelected || !bDirty) {
            YA_CORE_ERROR("Workbench automation: verification failed "
                          "(file={} rows={} selected={} clean={})",
                          bFile, bRowsOk, bSelected, bDirty);
            _bAutomationDone = true;
            return;
        }
        _bSmokePassed   = true;
        _bAutomationDone = true;
        YA_CORE_INFO("Workbench automation PASSED: new -> palette -> rename -> save -> verify");
        break;
    }
    default:
        break;
    }
}

} // namespace guiworkbench

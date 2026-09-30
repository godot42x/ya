#include "GameEditor/UI/Tabs/EditorUIDesignerTools.h"
#include "GameEditor/UI/Sections/EditorAutoPropertySection.h"
#include "GameEditor/UI/Shell/EditorListRows.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "Core/Event.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/SpinBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/EditorUISlotEdit.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"

#include <cmath>
#include <format>
#include <optional>
#include <vector>

namespace ya
{

namespace
{

UITreeView::FNode makeDesignerTreeNode(const UIElement& widget, const std::string& path)
{
    UITreeView::FNode node;
    node.id = path;
    node.label = widget._name + " [" + widget._typeId + "]";
    const auto& children = widget.getChildren();
    node.children.reserve(children.size());
    for (size_t index = 0; index < children.size(); ++index) {
        node.children.push_back(makeDesignerTreeNode(*children[index], path + "/" + std::to_string(index)));
    }
    return node;
}

void collectDesignerTreeFingerprint(const UIElement& widget, std::string& out, const std::string& path)
{
    out += path + ":" + widget._name + ":" + widget._typeId + ";";
    const auto& children = widget.getChildren();
    for (size_t index = 0; index < children.size(); ++index) {
        collectDesignerTreeFingerprint(*children[index], out, path + "/" + std::to_string(index));
    }
}

std::optional<std::vector<size_t>> parseDesignerChildPath(const std::string& id)
{
    if (id.empty()) {
        return std::nullopt;
    }
    std::vector<size_t> path;
    size_t              start = 0;
    while (start < id.size()) {
        const size_t slash = id.find('/', start);
        const size_t end   = slash == std::string::npos ? id.size() : slash;
        if (start == 0 && id.compare(start, end - start, "root") == 0) {
            start = slash == std::string::npos ? id.size() : slash + 1;
            continue;
        }
        try {
            path.push_back(static_cast<size_t>(std::stoul(id.substr(start, end - start))));
        }
        catch (...) {
            return std::nullopt;
        }
        start = slash == std::string::npos ? id.size() : slash + 1;
    }
    return path;
}

std::string designerSelectionPath(const UIElement& root, const UIElement& target, const std::string& prefix = "root")
{
    if (&root == &target) {
        return prefix;
    }
    const auto& children = root.getChildren();
    for (size_t index = 0; index < children.size(); ++index) {
        const std::string childPrefix = prefix + "/" + std::to_string(index);
        if (children[index].get() == &target) {
            return childPrefix;
        }
        if (std::string nested = designerSelectionPath(*children[index], target, childPrefix); !nested.empty()) {
            return nested;
        }
    }
    return {};
}

struct FAnchorPresetButton
{
    const char*         label;
    ECanvasAnchorPreset preset;
};

constexpr FAnchorPresetButton kAnchorPresetRows[4][3] = {
    {{"TL", ECanvasAnchorPreset::TopLeft}, {"Top", ECanvasAnchorPreset::Top}, {"TR", ECanvasAnchorPreset::TopRight}},
    {{"Left", ECanvasAnchorPreset::Left}, {"Center", ECanvasAnchorPreset::Center}, {"Right", ECanvasAnchorPreset::Right}},
    {{"BL", ECanvasAnchorPreset::BottomLeft}, {"Bottom", ECanvasAnchorPreset::Bottom}, {"BR", ECanvasAnchorPreset::BottomRight}},
    {{"Stretch H", ECanvasAnchorPreset::StretchHorizontal},
     {"Stretch V", ECanvasAnchorPreset::StretchVertical},
     {"Fill", ECanvasAnchorPreset::Fill}},
};

struct FResolutionPreset
{
    const char* label;
    glm::uvec2  size;
};

constexpr FResolutionPreset kResolutionPresets[] = {
    {"1280 x 720", {1280, 720}},
    {"1920 x 1080", {1920, 1080}},
    {"2560 x 1440", {2560, 1440}},
    {"1080 x 1920 (portrait)", {1080, 1920}},
    {"800 x 600", {800, 600}},
};
constexpr int kCustomResolutionIndex = static_cast<int>(std::size(kResolutionPresets));

std::shared_ptr<UISpinBox> makeResolutionSpin(std::string name, std::function<void(float)> onChanged)
{
    auto spin             = std::make_shared<UISpinBox>(std::move(name));
    spin->_min            = 16.0f;
    spin->_max            = 8192.0f;
    spin->_step           = 1.0f;
    spin->_onValueChanged = std::move(onChanged);
    return spin;
}

/// Anchor presets for the selected canvas child. The click reads the selection
/// then, so a stale grid cannot re-anchor a widget it was not built for.
UIElementRef makeAnchorPresetGrid(EditorUIDesignerSession& designer)
{
    auto grid = ui::column("UIDesignerAnchorPresets").setSpacing(2.0f);
    int  row  = 0;
    for (const auto& buttons : kAnchorPresetRows) {
        auto line = ui::row("UIDesignerAnchorPresetRow" + std::to_string(row++)).setSpacing(2.0f);
        for (const FAnchorPresetButton& button : buttons) {
            const ECanvasAnchorPreset preset = button.preset;
            line = line.child(labeledButton(std::string("UIDesignerAnchor_") + button.label, button.label)
                                  .setOnClick([&designer, preset]() {
                                      (void)designer.applyCanvasAnchorPreset(designer.getSelectedWidget(), preset);
                                  }),
                              ui::boxSlot().fill().preferredSize({0.0f, 22.0f}));
        }
        grid = grid.child(line.share());
    }
    return grid.share();
}

} // namespace

EditorUIHierarchyTab::EditorUIHierarchyTab(EditorLayer& layer, ActionMap* actions)
    : UICompoundWidget("UIDesignerHierarchyBody", "panel.canvas")
    , _layer(&layer)
    , _actions(actions)
{
    enableTick();
}

void EditorUIHierarchyTab::construct()
{
    _roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    _selection = std::make_shared<Reactive<std::string>>("");
    auto treeBuilder = ui::treeView("UIDesignerTree")
                           .bindData(_roots)
                           .bindSelection(_selection)
                           .setReorderable(true)
                           .setOnSelectionChanged([this](const std::string& id) {
                               if (!_layer) {
                                   return;
                               }
                               const std::optional<std::vector<size_t>> path = parseDesignerChildPath(id);
                               if (!path) {
                                   return;
                               }
                               _layer->getEditorUIDesignerSession().selectByChildPath(*path);
                           })
                           .setOnReorderHandler([this](const std::string& fromId,
                                                       const std::string& toId,
                                                       int mode) {
                               if (!_layer) {
                                   return;
                               }
                               const std::optional<std::vector<size_t>> fromPath = parseDesignerChildPath(fromId);
                               const std::optional<std::vector<size_t>> toPath   = parseDesignerChildPath(toId);
                               if (!fromPath || !toPath) {
                                   return;
                               }
                               EditorUIDesignerSession& panel = _layer->getEditorUIDesignerSession();
                               UIElement* dragged = panel.findByChildPath(*fromPath);
                               UIElement* target  = panel.findByChildPath(*toPath);
                               if (!dragged || !target) {
                                   return;
                               }
                               EditorUIDesignerSession::EDropPos position = EditorUIDesignerSession::EDropPos::Before;
                               if (mode == 1) {
                                   position = EditorUIDesignerSession::EDropPos::Into;
                               }
                               else if (mode == 2) {
                                   position = EditorUIDesignerSession::EDropPos::After;
                               }
                               panel.applyWidgetDrop(dragged, *target, position);
                           })
                           .setOnContextMenu([this](const std::string& nodeId, const glm::vec2& logicalPoint) {
                               openContextMenu(nodeId, logicalPoint);
                           })
                           .setRowToggleSpec({
                               .bEnabled  = true,
                               .width     = 26.0f,
                               .isOn      = [this](const std::string& nodeId) {
                                   // The eye shows when the row is VISIBLE in the
                                   // designer canvas (default): it is the display
                                   // toggle, not the runtime visibility property.
                                   const auto path = parseDesignerChildPath(nodeId);
                                   EditorUIDesignerSession* panel =
                                       _layer ? &_layer->getEditorUIDesignerSession() : nullptr;
                                   return panel && (!path || !panel->isDesignerHidden(*path));
                               },
                               .onToggled = [this](const std::string& nodeId) {
                                   if (!_layer) {
                                       return;
                                   }
                                   const auto path = parseDesignerChildPath(nodeId);
                                   if (!path) {
                                       return;
                                   }
                                   _layer->getEditorUIDesignerSession().toggleDesignerHidden(*path);
                               },
                           });
    _treeView = treeBuilder.share();
    addDetachedChild(ui::border("UIDesignerHierarchyInner")
                         .setStyleKey("panel.canvas")
                         .setPadding(FMargin::all(8.0f))
                         .child(_treeView, ui::contentSlot().fill())
                         .release());
}

void EditorUIHierarchyTab::openContextMenu(const std::string& nodeId, const glm::vec2& logicalPoint)
{
    WidgetTree*                              tree = getTree();
    const std::optional<std::vector<size_t>> path = parseDesignerChildPath(nodeId);
    if (!tree || !_layer || !_actions || !path) {
        return;
    }
    // The menu acts on the selection, so a right-click selects first.
    _layer->getEditorUIDesignerSession().selectByChildPath(*path);
    auto menu = UIMenu::create({
        UIMenu::FItem::fromAction(*_actions, "selection.duplicate"),
        UIMenu::FItem::fromAction(*_actions, "selection.delete"),
    });
    menu->openAt(*tree, logicalPoint);
}

void EditorUIHierarchyTab::onAttached()
{
    refresh();
}

void EditorUIHierarchyTab::tick(float)
{
    refresh();
}

void EditorUIHierarchyTab::refresh()
{
    if (!_layer || !_treeView) {
        return;
    }
    EditorUIDesignerSession& designer = _layer->getEditorUIDesignerSession();
    std::string fingerprint;
    std::vector<UITreeView::FNode> roots;
    if (UIElement* root = designer.getPreviewRoot()) {
        collectDesignerTreeFingerprint(*root, fingerprint, "root");
        roots.push_back(makeDesignerTreeNode(*root, "root"));
    }
    if (fingerprint != _treeFingerprint) {
        _treeFingerprint = std::move(fingerprint);
        _roots->replace(std::move(roots));
        _treeView->setExpanded("root", true);
    }
    if (UIElement* root = designer.getPreviewRoot()) {
        UIElement* selected = designer.getSelectedWidget();
        std::string selectionPath;
        if (selected) {
            selectionPath = designerSelectionPath(*root, *selected);
        }
        if (selectionPath != _selectionFingerprint) {
            _selectionFingerprint = std::move(selectionPath);
            _selection->set(_selectionFingerprint);
        }
    }
    else if (!_selectionFingerprint.empty()) {
        _selectionFingerprint.clear();
        _selection->set("");
    }
}

EditorUIInspectorTab::EditorUIInspectorTab(EditorLayer& layer)
    : UICompoundWidget("UIDesignerInspectorBody", "panel.canvas")
    , _layer(&layer)
{
    enableTick();
}

EditorUIInspectorTab::~EditorUIInspectorTab() = default;

void EditorUIInspectorTab::construct()
{
    _inspectorHost = ui::column("UIDesignerInspectorHost").setSpacing(6.0f).share();
    addDetachedChild(ui::scroll("UIDesignerInspectorScroll")
                         .setAxis(EScrollAxis::Vertical)
                         .child(ui::column("UIDesignerInspectorColumn")
                                    .setSpacing(6.0f)
                                    .child(ui::text("UIDesignerInspectorTitle")
                                               .setText("Inspector")
                                               .setStyleKey("text.eyebrow"))
                                    .child(_inspectorHost, ui::boxSlot().fill()),
                                ui::contentSlot().fill())
                         .release());
}

void EditorUIInspectorTab::onAttached()
{
    refresh();
}

void EditorUIInspectorTab::tick(float)
{
    refresh();
}

void EditorUIInspectorTab::refresh()
{
    WidgetTree* tree = getTree();
    if (!tree || !_layer) {
        return;
    }
    EditorUIDesignerSession& designer = _layer->getEditorUIDesignerSession();
    rebuildInspector(*tree, designer.getSelectedWidget());
    // Canvas drags and undo change the slot underneath the args copy.
    if (_slotEdit && !_slotEdit->pull()) {
        _inspectorFingerprint.clear();
        rebuildInspector(*tree, designer.getSelectedWidget());
    }
    if (_inspectorSection) {
        _inspectorSection->sync(*tree);
    }
    if (_slotSection) {
        _slotSection->sync(*tree);
    }
    if (_inspectorSection || _slotSection) {
        designer.invalidatePreview();
    }
}

void EditorUIInspectorTab::clearInspector(WidgetTree& tree)
{
    if (_inspectorSection && _inspectorSection->isAttached()) {
        tree.detach(*_inspectorSection);
    }
    if (_slotGroup && _slotGroup->isAttached()) {
        tree.detach(*_slotGroup);
    }
    _inspectorSection.reset();
    _slotSection.reset();
    _slotGroup.reset();
    _slotEdit.reset();
}

void EditorUIInspectorTab::rebuildInspector(WidgetTree& tree, UIElement* selected)
{
    EditorUIDesignerSession& designer = _layer->getEditorUIDesignerSession();
    // Undo rebuilds the preview, so an address alone can name a new widget;
    // a reparent keeps the widget but replaces its slot.
    std::string fingerprint = "none";
    if (selected) {
        const UIElement* parent = selected->getParent();
        fingerprint = std::format("{}:{}:{}:{}",
                                  designer.previewGeneration(),
                                  selected->_typeId,
                                  reinterpret_cast<uintptr_t>(selected),
                                  reinterpret_cast<uintptr_t>(parent ? parent->getSlotForChild(*selected) : nullptr));
    }
    if (fingerprint == _inspectorFingerprint) {
        return;
    }
    clearInspector(tree);
    _inspectorFingerprint = std::move(fingerprint);

    if (!selected || !_inspectorHost) {
        return;
    }

    std::string pathKey = "uidesigner";
    if (const auto path = designer.childPathOf(selected)) {
        for (const size_t index : *path) {
            pathKey += "/" + std::to_string(index);
        }
    }
    EditorUIDesignerSession* session = &designer;
    const EditorAutoPropertySection::FEditCommitSink sink{
        .commit = [session](const std::string& label, const std::string& mergeKey) {
            session->invalidatePreview();
            session->commitEdit(label, mergeKey);
        },
        .beginGesture = [session]() { session->undoStack().beginMerge(); },
        .endGesture   = [session]() { session->undoStack().endMerge(); },
    };

    PropertyGraph graph = PropertyGraph::project(selected->getTypeIndex(), {selected});
    if (graph.hasRetainedEditors()) {
        auto section = std::make_shared<EditorAutoPropertySection>("UIDesignerInspectorSection",
                                                                   std::move(graph),
                                                                   nullptr,
                                                                   pathKey,
                                                                   EditorAssetPickerCallback{},
                                                                   nullptr);
        section->setEditCommitSink(sink);
        _inspectorHost->addDetachedChild(section);
        _inspectorSection = std::move(section);
    }

    std::unique_ptr<EditorUISlotEdit> slotEdit = designer.editSlot(selected);
    if (!slotEdit) {
        return;
    }
    PropertyGraph slotGraph = PropertyGraph::project(slotEdit->argsType(), {slotEdit->args()});
    EditorUISlotEdit* edit = slotEdit.get();
    for (PropertyNode& node : slotGraph.getNodesMutable()) {
        node.binding.setChangeHook([edit]() { (void)edit->push(); });
    }
    auto slotSection = std::make_shared<EditorAutoPropertySection>("UIDesignerSlotSection",
                                                                   std::move(slotGraph),
                                                                   nullptr,
                                                                   pathKey + "/slot",
                                                                   EditorAssetPickerCallback{},
                                                                   nullptr);
    slotSection->setEditCommitSink(sink);
    auto group = ui::column("UIDesignerSlotGroup")
                     .setSpacing(4.0f)
                     .child(ui::text("UIDesignerSlotTitle")
                                .setText(std::string(slotEdit->displayName()))
                                .setStyleKey("text.eyebrow"));
    if (slotEdit->argsType() == type_index_v<FCanvasSlotArgs>) {
        group = group.child(makeAnchorPresetGrid(designer));
    }
    group = group.child(slotSection);
    _slotGroup   = group.share();
    _slotSection = std::move(slotSection);
    _slotEdit    = std::move(slotEdit);
    _inspectorHost->addDetachedChild(_slotGroup);
}

EditorUIPaletteTab::EditorUIPaletteTab(EditorLayer& layer)
    : UICompoundWidget("UIDesignerPaletteBody", "panel.canvas")
    , _layer(&layer)
{
}

void EditorUIPaletteTab::construct()
{
    auto palette = ui::column("UIDesignerPalette").setSpacing(4.0f);
    if (_layer) {
        EditorLayer& layer = *_layer;
        for (const std::string& typeId : UITypeRegistry::instance().getTypeIds()) {
            const std::string label = EditorUIDesignerSession::paletteDisplayName(typeId);
            palette = palette.child(labeledButton("UIDesignerPalette_" + label, label)
                                        .setOnClick([&layer, typeId]() {
                                            (void)layer.getEditorUIDesignerSession().addPaletteWidget(typeId);
                                        }),
                                    ui::boxSlot().preferredSize({0.0f, 24.0f}));
        }
    }
    addDetachedChild(ui::scroll("UIDesignerPaletteScroll")
                         .setAxis(EScrollAxis::Vertical)
                         .child(ui::column("UIDesignerPaletteColumn")
                                    .setSpacing(4.0f)
                                    .child(ui::text("UIDesignerPaletteTitle")
                                               .setText("Palette")
                                               .setStyleKey("text.eyebrow"))
                                    .child(palette.share(), ui::boxSlot().fill()),
                                ui::contentSlot().fill())
                         .release());
}

EditorUICanvasTab::EditorUICanvasTab(EditorLayer& layer)
    : UICompoundWidget("UIDesignerCanvasBody", "panel.canvas")
    , _designer(&layer.getEditorUIDesignerSession())
{
    enableTick();
}

void EditorUICanvasTab::construct()
{
    std::vector<std::string> presetLabels;
    for (const FResolutionPreset& preset : kResolutionPresets) {
        presetLabels.emplace_back(preset.label);
    }
    presetLabels.emplace_back("Custom");
    _resolutionPresets = ui::comboBox("UIDesignerResolutionPresets")
                             .setItems(std::move(presetLabels))
                             .setOnSelectionChanged([this](int index) {
                                 if (index >= 0 && index < kCustomResolutionIndex) {
                                     _designer->setDesignResolution(kResolutionPresets[index].size);
                                 }
                             })
                             .share();
    _resolutionWidth = makeResolutionSpin("UIDesignerResolutionWidth", [this](float value) {
        _designer->setDesignResolution({static_cast<uint32_t>(value), _designer->designResolution().y});
    });
    _resolutionHeight = makeResolutionSpin("UIDesignerResolutionHeight", [this](float value) {
        _designer->setDesignResolution({_designer->designResolution().x, static_cast<uint32_t>(value)});
    });
    _snapToGrid = ui::checkBox("UIDesignerCanvasSnap")
                      .setText("Snap")
                      .setChecked(_designer->isSnapToGrid())
                      .setOnChanged([this](bool value) { _designer->setSnapToGrid(value); })
                      .share();
    auto toolbar = ui::row("UIDesignerCanvasToolbar")
                       .setSpacing(4.0f)
                       .child(_resolutionPresets, ui::boxSlot().preferredSize({170.0f, 0.0f}))
                       .child(_resolutionWidth, ui::boxSlot().preferredSize({72.0f, 0.0f}))
                       .child(_resolutionHeight, ui::boxSlot().preferredSize({72.0f, 0.0f}))
                       .child(_snapToGrid, ui::boxSlot().preferredSize({96.0f, 0.0f}))
                       .child(labeledButton("UIDesignerCanvasFit", "Fit").setOnClick([this]() {
                                  _designer->canvas().bFitPending = true;
                              }),
                              ui::boxSlot().preferredSize({48.0f, 0.0f}));

    auto image = ui::image("UIDesignerCanvasImage");
    _image     = image.share();
    // The picture is the input surface: presses stop here and bubble to this
    // tab, and it takes keyboard focus so a text field elsewhere does not keep
    // the UI page's shortcuts (Delete, Ctrl+Z).
    _image->_hitFilter   = EWidgetHitFilter::Stop;
    _image->_focusPolicy = EWidgetFocusPolicy::Focusable;
    _image->setOpaqueSample(true);
    addDetachedChild(ui::column("UIDesignerCanvasColumn")
                         .setSpacing(4.0f)
                         .child(std::move(toolbar), ui::boxSlot().preferredSize({0.0f, 24.0f}))
                         .child(image.release(), ui::boxSlot().fill())
                         .release());
    syncResolution();
}

void EditorUICanvasTab::syncResolution()
{
    const glm::uvec2 resolution = _designer->designResolution();
    if (resolution == _shownResolution) {
        return;
    }
    _shownResolution = resolution;
    int index        = kCustomResolutionIndex;
    for (int i = 0; i < kCustomResolutionIndex; ++i) {
        if (kResolutionPresets[i].size == resolution) {
            index = i;
        }
    }
    _resolutionPresets->setSelectedIndex(index, false);
    _resolutionWidth->setValue(static_cast<float>(resolution.x));
    _resolutionHeight->setValue(static_cast<float>(resolution.y));
}

void EditorUICanvasTab::onAttached()
{
    ++_designer->canvas().shownCount;
}

void EditorUICanvasTab::onDetached()
{
    endGesture();
    EditorUICanvasView& view = _designer->canvas();
    if (view.shownCount > 0) {
        --view.shownCount;
    }
}

void EditorUICanvasTab::tick(float deltaSeconds)
{
    UICompoundWidget::tick(deltaSeconds);
    // Next frame's canvas is sized from where this tab is laid out now.
    EditorUICanvasView& view = _designer->canvas();
    view.extent              = _image->_layoutRect.extent;
    if (view.bFitPending && view.fitTo(glm::vec2(_designer->designResolution()))) {
    if (_snapToGrid && _snapToGrid->isChecked() != _designer->isSnapToGrid()) {
        _snapToGrid->setChecked(_designer->isSnapToGrid());
    }
        view.bFitPending = false;
    }
    syncResolution();
    pushPicture();
}

void EditorUICanvasTab::pushPicture()
{
    const std::shared_ptr<RenderTexture>& picture = _designer->canvas().image;
    std::shared_ptr<IImage>     image = picture ? picture->getImageShared() : nullptr;
    std::shared_ptr<IImageView> view  = picture ? picture->getImageViewShared() : nullptr;
    if (!image || !view) {
        _image->setTexture(nullptr);
        _texture.reset();
        _textureImage.reset();
        _textureView.reset();
        return;
    }
    if (image != _textureImage || view != _textureView) {
        _textureImage = std::move(image);
        _textureView  = std::move(view);
        _texture      = Texture::wrap(_textureImage, _textureView, "EditorUICanvas");
        _image->setTexture(_texture);
    }
    // Live RT contents change every frame even when the wrap does not.
    _image->markPaintDirty();
}

glm::vec2 EditorUICanvasTab::toView(const glm::vec2& logicalPoint) const
{
    return logicalPoint - _image->_layoutRect.pos;
}

void EditorUICanvasTab::beginPress(const glm::vec2& viewPoint)
{
    _bPressing  = true;
    _pressHit   = nullptr;
    _pressPoint = _designer->canvas().viewToCanvas(viewPoint);

    // Resize handles of the selection win over picking, so an edge can be
    // grabbed without re-selecting whatever lies under it.
    if (UIElement* selected = _designer->getSelectedWidget()) {
        if (const uint8_t mask = _designer->hitTestResizeHandles(viewPoint)) {
            _pressHit = selected;
            _designer->beginResize(selected, _pressPoint, mask);
            return;
        }
    }
    if (UIElement* picked = _designer->pickAt(_pressPoint)) {
        _designer->select(picked);
        _pressHit = picked;
        _designer->beginMove(picked, _pressPoint);
        return;
    }
    _designer->clearSelection();
}

void EditorUICanvasTab::endGesture()
{
    if (_bPressing) {
        _designer->endDrag();
    }
    _bPressing = false;
    _bPanning  = false;
    _pressHit  = nullptr;
}

void EditorUICanvasTab::clearTransientInputState()
{
    endGesture();
    UICompoundWidget::clearTransientInputState();
}

void EditorUICanvasTab::frameSelection()
{
    if (!_designer) {
        return;
    }
    // Frame the selection; with nothing selected, frame the whole design.
    if (const Rect2D* rect = _designer->getSelectedLayoutRect()) {
        (void)_designer->canvas().frameRect(*rect);
        return;
    }
    (void)_designer->canvas().fitTo(glm::vec2(_designer->designResolution()));
}

void EditorUICanvasTab::endNudgeMerge()
{
    if (_bNudgeMergeOpen) {
        _designer->undoStack().endMerge();
        _bNudgeMergeOpen = false;
    }
}

bool EditorUICanvasTab::handleInputEvent(const Event& event, const WidgetEventContext& ctx)
{
    WidgetTree* tree = getTree();
    if (!tree) {
        return false;
    }
    const bool bOnImage = _image->hitTestLayoutRect(ctx.logicalPoint);

    switch (event.getEventType()) {
    case EEvent::MouseButtonPressed: {
        if (!bOnImage || _bPressing || _bPanning) {
            return bOnImage;
        }
        const EMouse::T button = static_cast<const MouseButtonPressedEvent&>(event).GetMouseButton();
        if (button == EMouse::Left) {
            beginPress(toView(ctx.logicalPoint));
        }
        else if (button == EMouse::Right || button == EMouse::Middle) {
            _bPanning = true;
            _panLast  = toView(ctx.logicalPoint);
        }
        else {
            return false;
        }
        tree->setFocus(_image.get());
        tree->setPointerCapture(this);
        return true;
    }
    case EEvent::MouseMoved: {
        const glm::vec2 viewPoint = toView(ctx.logicalPoint);
        if (_bPanning) {
            _designer->canvas().pan += viewPoint - _panLast;
            _panLast = viewPoint;
            return true;
        }
        if (_bPressing && _pressHit && _designer->isDragging(_pressHit)) {
            const glm::vec2 canvasPoint = _designer->canvas().viewToCanvas(viewPoint);
            if (!_designer->applyDragDelta(canvasPoint - _pressPoint)) {
                _pressHit = nullptr;
            }
            return true;
        }
        return false;
    }
    case EEvent::MouseButtonReleased: {
        const EMouse::T button = static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton();
        const bool bEndsPress = _bPressing && button == EMouse::Left;
        const bool bEndsPan   = _bPanning && (button == EMouse::Right || button == EMouse::Middle);
        if (!bEndsPress && !bEndsPan) {
            return false;
        }
        endGesture();
        tree->releasePointerCapture(this);
        return true;
    }
    case EEvent::KeyPressed: {
        // Keyboard editing lives on the canvas: the image takes focus on press,
        // so arrows/Escape/F never leak into other tabs' widgets.
        const auto& press = static_cast<const KeyPressedEvent&>(event);
        switch (press.getKeyCode()) {
        case EKey::Left:
        case EKey::Right:
        case EKey::Up:
        case EKey::Down: {
            const float step    = press.isShiftPressed() ? 10.0f : 1.0f;
            glm::vec2   delta{};
            switch (press.getKeyCode()) {
            case EKey::Left:  delta.x = -step; break;
            case EKey::Right: delta.x = step; break;
            case EKey::Up:    delta.y = -step; break;
            default:          delta.y = step; break;
            }
            if (!press.bRepeat && !_bNudgeMergeOpen) {
                _designer->undoStack().beginMerge();
                _bNudgeMergeOpen = true;
            }
            if (!_designer->nudgeSelection(delta)) {
                endNudgeMerge();
            }
            return true;
        }
        case EKey::Escape:
            endNudgeMerge();
            _designer->clearSelection();
            return true;
        case EKey::K_F:
            endNudgeMerge();
            frameSelection();
            return true;
        default:
            endNudgeMerge();
            return false;
        }
    }
    case EEvent::KeyReleased: {
        const auto& release = static_cast<const KeyReleasedEvent&>(event);
        switch (release.getKeyCode()) {
        case EKey::Left:
        case EKey::Right:
        case EKey::Up:
        case EKey::Down:
            endNudgeMerge();
            return true;
        default:
            return false;
        }
    }
    case EEvent::MouseScrolled: {
        if (!bOnImage) {
            return false;
        }
        const float factor = std::exp(static_cast<const MouseScrolledEvent&>(event).getOffsetY() * 0.12f);
        _designer->canvas().zoomAt(toView(ctx.logicalPoint), factor);
        return true;
    }
    default:
        return false;
    }
}

} // namespace ya

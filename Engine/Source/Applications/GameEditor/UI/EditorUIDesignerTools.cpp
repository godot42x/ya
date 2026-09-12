#include "GameEditor/UI/EditorUIDesignerTools.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"
#include "GameEditor/UI/EditorListRows.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Panels/UIDesignerPanel.h"
#include "GameEditor/UI/EditorDocumentSession.h"

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

} // namespace

EditorUIHierarchyTab::EditorUIHierarchyTab(EditorLayer& layer)
    : UICompoundWidget("UIDesignerHierarchyBody", "panel.canvas")
    , _layer(&layer)
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
                               _layer->getUIDesignerPanel().selectByChildPath(*path);
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
                               UIDesignerPanel& panel = _layer->getUIDesignerPanel();
                               UIElement* dragged = panel.findByChildPath(*fromPath);
                               UIElement* target  = panel.findByChildPath(*toPath);
                               if (!dragged || !target) {
                                   return;
                               }
                               UIDesignerPanel::EDropPos position = UIDesignerPanel::EDropPos::Before;
                               if (mode == 1) {
                                   position = UIDesignerPanel::EDropPos::Into;
                               }
                               else if (mode == 2) {
                                   position = UIDesignerPanel::EDropPos::After;
                               }
                               panel.applyWidgetDrop(dragged, *target, position);
                           });
    _treeView = treeBuilder.share();
    addDetachedChild(ui::border("UIDesignerHierarchyInner")
                         .setStyleKey("panel.canvas")
                         .setPadding(FMargin::all(8.0f))
                         .child(_treeView, ui::contentSlot().fill())
                         .release());
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
    UIDesignerPanel& designer = _layer->getUIDesignerPanel();
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

EditorUIInspectorTab::EditorUIInspectorTab(EditorLayer& layer, UndoStack* undo)
    : UICompoundWidget("UIDesignerInspectorBody", "panel.canvas")
    , _layer(&layer)
    , _undo(undo)
{
    enableTick();
}

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
    UIDesignerPanel& designer = _layer->getUIDesignerPanel();
    if (EditorDocumentSession* session = designer.documentSession()) {
        _undo = &session->undo();
    }
    rebuildInspector(*tree, designer.getSelectedWidget());
    if (_inspectorSection) {
        _inspectorSection->sync(*tree);
        designer.invalidatePreview();
    }
}

void EditorUIInspectorTab::rebuildInspector(WidgetTree& tree, UIElement* selected)
{
    std::string fingerprint = "none";
    if (selected) {
        fingerprint = selected->_typeId + ":" + std::to_string(reinterpret_cast<uintptr_t>(selected));
    }
    if (fingerprint == _inspectorFingerprint) {
        return;
    }

    if (_inspectorSection && _inspectorSection->isAttached()) {
        tree.detach(*_inspectorSection);
    }
    _inspectorSection.reset();
    _inspectorFingerprint = std::move(fingerprint);

    if (!selected || !_inspectorHost) {
        return;
    }

    PropertyGraph graph = PropertyGraph::project(selected->getTypeIndex(), {selected});
    if (!graph.hasRetainedEditors()) {
        return;
    }

    auto section = std::make_shared<EditorAutoPropertySection>(
        "UIDesignerInspectorSection",
        std::move(graph),
        _undo,
        std::string("uidesigner:") + selected->_name,
        EditorAssetPickerCallback{},
        nullptr);
    _inspectorHost->addDetachedChild(section);
    _inspectorSection = std::move(section);
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
            const std::string label = UIDesignerPanel::paletteDisplayName(typeId);
            palette = palette.child(labeledButton("UIDesignerPalette_" + label, label)
                                        .setOnClick([&layer, typeId]() {
                                            (void)layer.getUIDesignerPanel().addPaletteWidget(typeId);
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

EditorUIPreviewTab::EditorUIPreviewTab(EditorLayer& layer)
    : UICompoundWidget("UIDesignerPreviewBody", "panel.canvas")
    , _layer(&layer)
{
    enableTick();
}

void EditorUIPreviewTab::construct()
{
    auto status = ui::text("UIDesignerStatus").setText("No document open").setStyleKey("text.muted").share();
    auto selection = ui::text("UIDesignerSelection").setText("No widget selected").setStyleKey("text.muted").share();
    _statusText = status;
    _selectionText = selection;
    addDetachedChild(ui::column("UIDesignerPreviewColumn")
                         .setSpacing(8.0f)
                         .child(ui::text("UIDesignerPreviewTitle")
                                    .setText("Preview")
                                    .setStyleKey("text.eyebrow"))
                         .child(status)
                         .child(selection)
                         .child(ui::text("UIDesignerPreviewHint")
                                    .setText("Canvas is the Level 2D viewport (PreviewTarget, not a Camera).")
                                    .setStyleKey("text.muted"))
                         .release());
}

void EditorUIPreviewTab::onAttached()
{
    refresh();
}

void EditorUIPreviewTab::tick(float)
{
    refresh();
}

void EditorUIPreviewTab::refresh()
{
    if (!_layer || !_statusText) {
        return;
    }
    const auto& designer = _layer->getUIDesignerPanel();
    const auto& document = designer.getOpenDocument();
    std::string status = document ? "Document: " + document->typeId : "No document open";
    if (designer.isDocumentDirty()) {
        status += " *";
    }
    if (const EditorDocumentSession* session = designer.documentSession()) {
        if (session->ownsPreview()) {
            status += " [preview]";
        }
    }
    _statusText->setText(status);
    UIElement* selected = designer.getSelectedWidget();
    _selectionText->setText(selected ? "Selected: " + selected->_name : "No widget selected");
}

} // namespace ya

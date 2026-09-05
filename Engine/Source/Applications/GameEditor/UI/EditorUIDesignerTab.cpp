#include "GameEditor/UI/EditorUIDesignerTab.h"
#include "GameEditor/UI/EditorAutoPropertySection.h"
#include "GameEditor/UI/EditorListRows.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/Panels/UIDesignerPanel.h"

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

std::shared_ptr<UIElement> EditorUIDesignerTab::build(WidgetTree&)
{
    auto status = ui::text("UIDesignerStatus").setText("No document open").setStyleKey("text.muted").share();
    auto selection = ui::text("UIDesignerSelection").setText("No widget selected").setStyleKey("text.muted").share();
    _roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    _selection = std::make_shared<Reactive<std::string>>("");
    auto treeBuilder = ui::treeView("UIDesignerTree")
                           .bindData(_roots)
                           .bindSelection(_selection)
                           .setOnSelectionChanged([this](const std::string& id) {
                               if (!_layer) {
                                   return;
                               }
                               std::vector<size_t> path;
                               size_t start = 0;
                               while (start < id.size()) {
                                   const size_t slash = id.find('/', start);
                                   const size_t end = slash == std::string::npos ? id.size() : slash;
                                   if (start == 0 && id.compare(start, end - start, "root") == 0) {
                                       start = slash == std::string::npos ? id.size() : slash + 1;
                                       continue;
                                   }
                                   try {
                                       path.push_back(static_cast<size_t>(std::stoul(id.substr(start, end - start))));
                                   }
                                   catch (...) {
                                       return;
                                   }
                                   start = slash == std::string::npos ? id.size() : slash + 1;
                               }
                               _layer->getUIDesignerPanel().selectByChildPath(path);
                           });
    auto tree = treeBuilder.share();
    _treeView = tree;

    auto newBuilder = ui::button("UIDesignerNew").child(ui::text("UIDesignerNewLabel").setText("New Panel"));
    newBuilder.setOnClick([this]() {
        if (_layer) {
            _layer->getUIDesignerPanel().newDocument("panel");
        }
    });
    auto newButton = newBuilder.share();

    auto saveBuilder = ui::button("UIDesignerSave").child(ui::text("UIDesignerSaveLabel").setText("Save"));
    saveBuilder.setOnClick([this]() {
        if (_layer) {
            (void)_layer->getUIDesignerPanel().saveDocument();
        }
    });
    auto saveButton = saveBuilder.share();

    auto closeBuilder = ui::button("UIDesignerClose").child(ui::text("UIDesignerCloseLabel").setText("Close"));
    closeBuilder.setOnClick([this]() {
        if (_layer) {
            _layer->getUIDesignerPanel().clearDocument();
        }
    });
    auto closeButton = closeBuilder.share();

    _statusText = status;
    _selectionText = selection;
    _newButton = newButton;
    _saveButton = saveButton;
    _closeButton = closeButton;

    _paletteList = [this]() {
        auto palette = ui::column("UIDesignerPalette").setSpacing(4.0f);
        if (!_layer) {
            return palette.share();
        }
        EditorLayer& layer = *_layer;
        for (const std::string& typeId : UITypeRegistry::instance().getTypeIds()) {
            const std::string label = UIDesignerPanel::paletteDisplayName(typeId);
            palette = palette.child(labeledButton("UIDesignerPalette_" + label, label)
                                        .setOnClick([&layer, typeId]() {
                                            (void)layer.getUIDesignerPanel().addPaletteWidget(typeId);
                                        }),
                                    ui::boxSlot().preferredSize({0.0f, 24.0f}));
        }
        return palette.share();
    }();

    _inspectorHost = ui::column("UIDesignerInspectorHost").setSpacing(6.0f).share();

    auto mainColumn = ui::column("UIDesignerMainColumn")
                          .setSpacing(8.0f)
                          .child(status)
                          .child(selection)
                          .child(tree, FBoxSlotArgs{.preferredSize = {0.0f, 180.0f}})
                          .child(newButton, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                          .child(saveButton, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                          .child(closeButton, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}});

    return ui::panel("UIDesignerBody")
        .setStyleKey("panel.canvas")
        .child(ui::row("UIDesignerLayout")
                   .setSpacing(8.0f)
                   .setStretchLastChild(true)
                   .child(ui::scroll("UIDesignerPaletteScroll")
                              .setAxis(EScrollAxis::Vertical)
                              .child(ui::column("UIDesignerPaletteColumn")
                                         .setSpacing(4.0f)
                                         .child(ui::text("UIDesignerPaletteTitle")
                                                    .setText("Palette")
                                                    .setStyleKey("text.eyebrow"))
                                         .child(_paletteList, ui::boxSlot().fill()),
                                     ui::overlaySlot().fill()),
                          ui::boxSlot().preferredSize({120.0f, 0.0f}))
                   .child(std::move(mainColumn), ui::boxSlot().fill())
                   .child(ui::scroll("UIDesignerInspectorScroll")
                              .setAxis(EScrollAxis::Vertical)
                              .child(ui::column("UIDesignerInspectorColumn")
                                         .setSpacing(6.0f)
                                         .child(ui::text("UIDesignerInspectorTitle")
                                                    .setText("Inspector")
                                                    .setStyleKey("text.eyebrow"))
                                         .child(_inspectorHost, ui::boxSlot().fill()),
                                     ui::overlaySlot().fill()),
                          ui::boxSlot().preferredSize({220.0f, 0.0f})),
               ui::canvasSlot().fill().offset({12.0f, 12.0f}))
        .release();
}

void EditorUIDesignerTab::sync(WidgetTree& tree)
{
    if (!_layer || !_statusText || !_selectionText || !_roots || !_selection) {
        return;
    }
    const auto& designer = _layer->getUIDesignerPanel();
    const auto& document = designer.getOpenDocument();
    _statusText->setText(document ? "Document: " + document->typeId : "No document open");
    UIElement* selected = designer.getSelectedWidget();
    _selectionText->setText(selected ? "Selected: " + selected->_name : "No widget selected");
    std::string fingerprint;
    std::vector<UITreeView::FNode> roots;
    if (UIElement* root = designer.getPreviewRoot()) {
        collectDesignerTreeFingerprint(*root, fingerprint, "root");
        roots.push_back(makeDesignerTreeNode(*root, "root"));
    }
    if (fingerprint != _treeFingerprint) {
        _treeFingerprint = std::move(fingerprint);
        _roots->replace(std::move(roots));
    }
    if (_saveButton) {
        _saveButton->setEnabled(document != nullptr);
    }
    if (_closeButton) {
        _closeButton->setEnabled(document != nullptr);
    }
    rebuildInspector(tree, selected);
    if (_inspectorSection) {
        _inspectorSection->sync(tree);
        _layer->getUIDesignerPanel().invalidatePreview();
    }
    if (UIElement* root = designer.getPreviewRoot()) {
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

void EditorUIDesignerTab::rebuildInspector(WidgetTree& tree, UIElement* selected)
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
        std::string("uidesigner:") + selected->_name);
    if (!tree.attach(*_inspectorHost, section).valid()) {
        return;
    }
    _inspectorSection = std::move(section);
}

} // namespace ya

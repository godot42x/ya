#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>
#include <functional>
#include <memory>
#include <vector>

namespace guiworkbench
{

namespace
{

auto demoButton(std::string name, const std::string& label)
{
    auto button = ya::ui::button(name).child(
        ya::ui::text(name + "_Label")
            .setText(label)
            .setFontSize(13)
            .setHAlign(ya::EWidgetAlignH::Center)
            .setVAlign(ya::EWidgetAlignV::Center));
    return std::move(button).setContentPadding({12.0f, 4.0f});
}

} // namespace

void buildBindingDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };
    auto paneLabel = [&](std::string key, const std::string& text)
    {
        return body(std::move(key), text)
            .setHAlign(ya::EWidgetAlignH::Center)
            .setVAlign(ya::EWidgetAlignV::Center);
    };

    auto form = ya::ui::column("BindingForm").setPadding({16.0f, 12.0f}).setSpacing(12.0f);
    form.child(header("GallerySection1", "1. Reactive binding — model drives view"));

    auto counterRef    = std::make_shared<ya::Reactive<int>>(0);
    auto counterStrRef = std::make_shared<ya::Reactive<std::string>>("Count: 0");
    form.child(ya::ui::text("GalleryBoundCounter").bindText(counterStrRef).setFontSize(14));
    form.child(demoButton("GalleryInc", "Increment (reactive)").setOnClick(
        [counterRef, counterStrRef, log]
        {
            const int next = counterRef->value() + 1;
            counterRef->set(next);
            counterStrRef->set(std::format("Count: {}", next));
            log(std::format("Reactive counter -> {}", next));
        }), ya::ui::boxSlot().preferredSize({200.0f, 26.0f}));

    auto enabledRef = std::make_shared<ya::Reactive<bool>>(true);
    form.child(demoButton("GalleryDependent", "Enabled by reactive flag").bindEnabled(enabledRef),
               ya::ui::boxSlot().preferredSize({220.0f, 26.0f}));
    form.child(demoButton("GalleryToggle", "Toggle enabled flag").setOnClick(
        [enabledRef, log]
        {
            const bool next = !enabledRef->value();
            enabledRef->set(next);
            log(std::format("Reactive enabled flag -> {}", next ? "on" : "off"));
        }), ya::ui::boxSlot().preferredSize({220.0f, 26.0f}));

    auto menuLabelRef = std::make_shared<ya::Reactive<std::string>>("Dynamic Item");
    auto localBar     = std::make_shared<ya::UIMenuBar>("GalleryMenuBar");
    auto* dynItem     = localBar->addItem("Dynamic Item", nullptr);
    dynItem->bindLabel(menuLabelRef);
    form.child(localBar, ya::ui::boxSlot().preferredSize({0.0f, 28.0f}));
    form.child(demoButton("GalleryRename", "Rename menu item (reactive)").setOnClick(
        [menuLabelRef, log]
        {
            const std::string next = menuLabelRef->value() == "Dynamic Item" ? "Renamed!" : "Dynamic Item";
            menuLabelRef->set(next);
            log(std::format("Reactive menu label -> '{}'", next));
        }), ya::ui::boxSlot().preferredSize({260.0f, 26.0f}));

    auto ratioRef = std::make_shared<ya::Reactive<float>>(0.45f);
    form.child(ya::ui::splitPane("GallerySplit")
            .bindSplitRatio(ratioRef)
            .setMinFirstExtent(80.0f)
            .setMinSecondExtent(80.0f)
            .children(
                ya::ui::panel("GallerySplitLeft")
                    .setColor({0.20f, 0.24f, 0.32f, 1.0f})
                    .child(paneLabel("GallerySplitLeft_Body", "ratio <- reactive")),
                ya::ui::panel("GallerySplitRight")
                    .setColor({0.28f, 0.22f, 0.32f, 1.0f})
                    .child(paneLabel("GallerySplitRight_Body", "drag divider"))),
           ya::ui::boxSlot().preferredSize({0.0f, 120.0f}));
    form.child(demoButton("GalleryRatio", "Set ratio 0.25 (reactive)").setOnClick(
        [ratioRef, log]
        {
            ratioRef->set(0.25f);
            log("Reactive split ratio -> 0.25");
        }), ya::ui::boxSlot().preferredSize({240.0f, 26.0f}));

    form.child(header("IdentityTitle", "SelectionModel / ActionMap / UndoStack"));
    auto selection = std::make_shared<ya::SelectionModel>();
    selection->select("cube");
    auto primaryLabel = std::make_shared<ya::Reactive<std::string>>("primary: cube");

    auto selRow = ya::ui::row("SelectionRow").setSpacing(6.0f);
    for (const char* id : {"cube", "sphere", "light"}) {
        selRow.child(demoButton(std::string("Select_") + id, id).setOnClick(
            [selection, primaryLabel, id, log]
            {
                selection->select(id);
                primaryLabel->set(std::string("primary: ") + id);
                log(std::format("SelectionModel -> {}", id));
            }),
                     ya::ui::boxSlot().preferredSize({90.0f, 24.0f}));
    }
    form.child(std::move(selRow));
    form.child(ya::ui::text("SelectionPrimary").bindText(primaryLabel).setFontSize(13));

    auto undo     = std::make_shared<ya::UndoStack>();
    auto valueRef = std::make_shared<ya::Reactive<int>>(0);
    auto valueStr = std::make_shared<ya::Reactive<std::string>>("value: 0");
    auto actions  = std::make_shared<ya::ActionMap>();
    (void)actions->define({
        .id      = "demo.inc",
        .label   = "Increment",
        .execute = [undo, valueRef, valueStr, log]
        {
            const int prev = valueRef->value();
            const int next = prev + 1;
            valueRef->set(next);
            valueStr->set(std::format("value: {}", next));
            (void)undo->push({
                .label = "Increment",
                .undo  = [valueRef, valueStr, prev]
                {
                    valueRef->set(prev);
                    valueStr->set(std::format("value: {}", prev));
                },
                .redo  = [valueRef, valueStr, next]
                {
                    valueRef->set(next);
                    valueStr->set(std::format("value: {}", next));
                },
            });
            log("ActionMap demo.inc");
        },
    });
    (void)actions->define({
        .id      = "demo.undo",
        .label   = "Undo",
        .execute = [undo, log]
        {
            (void)undo->undo();
            log("UndoStack undo");
        },
        .canExecute = [undo] { return undo->canUndo(); },
    });
    (void)actions->define({
        .id      = "demo.redo",
        .label   = "Redo",
        .execute = [undo, log]
        {
            (void)undo->redo();
            log("UndoStack redo");
        },
        .canExecute = [undo] { return undo->canRedo(); },
    });

    form.child(ya::ui::text("UndoValue").bindText(valueStr).setFontSize(13));
    form.child(ya::ui::row("ActionRow")
                   .setSpacing(8.0f)
                   .child(demoButton("ActionInc", "Increment").setOnClick(
                              [actions] { (void)actions->execute("demo.inc"); }),
                          ya::ui::boxSlot().preferredSize({120.0f, 26.0f}))
                   .child(demoButton("ActionUndo", "Undo").setOnClick(
                              [actions] { (void)actions->execute("demo.undo"); }),
                          ya::ui::boxSlot().preferredSize({90.0f, 26.0f}))
                   .child(demoButton("ActionRedo", "Redo").setOnClick(
                              [actions] { (void)actions->execute("demo.redo"); }),
                          ya::ui::boxSlot().preferredSize({90.0f, 26.0f})));

    auto page = ya::ui::panel("BindingDemo")
                    .setColor(kPanelColor)
                    .child(ya::ui::scroll("GalleryScroll").child(std::move(form)), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    (void)state;
    (void)tree;
}

void buildTreeDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto form = ya::ui::column("TreeForm").setPadding({16.0f, 12.0f}).setSpacing(12.0f);
    form.child(header("GallerySection2", "2. TreeView (data-driven widget) + reactive selection"));

    auto roots = std::make_shared<ya::ReactiveList<ya::UITreeView::FNode>>();
    roots->push({
        .id       = "root",
        .label    = "Scene Root",
        .children = {
            {"mesh", "Mesh", {}},
            {"light", "Light", {
                                   {"point", "Point Light", {}},
                                   {"spot", "Spot Light", {}},
                               }},
            {"camera", "Camera", {}},
        },
    });
    roots->push({
        .id       = "ui",
        .label    = "UI",
        .children = {
            {"hud", "HUD", {}},
            {"menu", "Menu", {}},
        },
    });

    auto treeView = std::make_shared<ya::UITreeView>("GalleryTree");
    treeView->bindData(roots);
    treeView->setExpanded("root", true);
    treeView->setReorderable(true);
    auto treeSelectedId = std::make_shared<ya::Reactive<std::string>>("");
    treeView->bindSelection(treeSelectedId);
    treeView->setOnReorderHandler([roots, log](const std::string& fromId, const std::string& toId, int mode)
    {
        if (fromId == toId) {
            return;
        }
        std::vector<ya::UITreeView::FNode> snapshot;
        for (size_t i = 0; i < roots->size(); ++i) {
            snapshot.push_back(roots->get(i));
        }
        ya::UITreeView::FNode              moved;
        bool                               bFound = false;
        std::vector<ya::UITreeView::FNode> rebuilt;
        for (auto& n : snapshot) {
            if (n.id == fromId) {
                moved  = n;
                bFound = true;
            }
            else {
                rebuilt.push_back(n);
            }
        }
        if (!bFound) {
            return;
        }
        int targetIndex = -1;
        for (size_t i = 0; i < rebuilt.size(); ++i) {
            if (rebuilt[i].id == toId) {
                targetIndex = static_cast<int>(i);
                break;
            }
        }
        if (targetIndex < 0) {
            return;
        }
        if (mode == 1) {
            const std::function<bool(const ya::UITreeView::FNode&, const std::string&)> contains =
                [&](const ya::UITreeView::FNode& n, const std::string& id) -> bool
            {
                if (n.id == id) {
                    return true;
                }
                for (const auto& c : n.children) {
                    if (contains(c, id)) {
                        return true;
                    }
                }
                return false;
            };
            if (contains(moved, toId)) {
                log(std::format("Tree reorder refused: '{}' cannot move into its own descendant '{}'", fromId, toId));
                return;
            }
            rebuilt[static_cast<size_t>(targetIndex)].children.push_back(moved);
        }
        else {
            const int insertIndex = targetIndex + (mode == 2 ? 1 : 0);
            rebuilt.insert(rebuilt.begin() + insertIndex, moved);
        }
        roots->clear();
        for (const auto& n : rebuilt) {
            roots->push(n);
        }
        log(std::format("Tree reorder '{}' {} '{}'", fromId, mode == 0 ? "before" : (mode == 1 ? "into" : "after"), toId));
    });
    treeView->_onToggleExpanded = [log](const std::string& id, bool bExpanded)
    {
        log(std::format("Tree toggle '{}' -> {}", id, bExpanded ? "expanded" : "collapsed"));
    };
    treeView->setOnContextMenu([&tree, log](const std::string& nodeId, const glm::vec2& point)
    {
        log(std::format("Tree context menu -> '{}'", nodeId));
        auto menu = ya::UIMenu::create({
            ya::UIMenu::FItem{
                .label  = "Create Empty Node",
                .action = [log]() { log("Tree menu: Create Empty Node"); },
            },
            ya::UIMenu::FItem::separator(),
            ya::UIMenu::FItem{
                .label  = "Duplicate",
                .action = [log]() { log("Tree menu: Duplicate"); },
            },
            ya::UIMenu::FItem{
                .label  = "Delete",
                .action = [log]() { log("Tree menu: Delete"); },
            },
        });
        menu->openAt(tree, point);
    });
    auto treeFilterRef = std::make_shared<ya::Reactive<std::string>>("");
    treeView->bindFilter(treeFilterRef);
    auto selStrRef = std::make_shared<ya::Reactive<std::string>>("(none)");
    treeView->_onSelectionChanged = [selStrRef, log](const std::string& id)
    {
        selStrRef->set(id.empty() ? "(none)" : id);
        log(std::format("Tree selection -> '{}'", id));
    };
    form.child(treeView, ya::FBoxSlotArgs{.sizeRule = ya::EUIBoxSlotSizeRule::Auto});
    form.child(ya::ui::row("GalleryFilterRow")
                   .setSpacing(8.0f)
                   .child(body("GalleryFilter_Body", "Filter"))
                   .child(ya::ui::textField("GalleryTreeFilter")
                              .setFontSize(13)
                              .setOnTextChanged([treeFilterRef](const std::string& text)
                                                { treeFilterRef->set(text); }),
                          ya::ui::boxSlot().preferredSize({160.0f, 24.0f})));
    form.child(ya::ui::text("GallerySelected").bindText(selStrRef).setFontSize(13));

    auto page = ya::ui::panel("TreeDemo").setColor(kPanelColor).child(std::move(form), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    (void)state;
}

void buildTableDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto form = ya::ui::column("TableForm").setPadding({16.0f, 12.0f}).setSpacing(12.0f);
    form.child(header("GallerySection5", "5. Table — data-driven grid with reactive selection"));

    auto tableRows = std::make_shared<ya::ReactiveList<ya::UITableGrid::FTableRow>>();
    tableRows->push({"hdr", {"Name", "Type", "Count", "Visible"}});
    tableRows->push({"r1", {"Cube", "StaticMesh", "3", "yes"}});
    tableRows->push({"r2", {"PointLight", "Light", "2", "yes"}});
    tableRows->push({"r3", {"Camera", "Node", "1", "no"}});
    tableRows->push({"r4", {"Material", "Asset", "8", "yes"}});

    auto tableGrid             = std::make_shared<ya::UITableGrid>("GalleryTableGrid");
    tableGrid->_columnWidths   = {140.0f, 100.0f, 0.0f, 0.0f};
    tableGrid->bindData(tableRows);
    auto tableSelRef = std::make_shared<ya::Reactive<std::string>>("r1");
    tableGrid->bindSelection(tableSelRef);
    tableGrid->_onSelectionChanged = [log](int row) { log(std::format("Table row -> {}", row)); };
    auto cellButton = demoButton("GalleryCellButton", "Inspect");
    auto cellLive   = cellButton.share();
    tableGrid->addDetachedChild(cellButton.release());
    if (auto* cellSlot = tableGrid->getCellSlot(*cellLive)) {
        cellSlot->setCell(3, 2);
    }
    form.child(tableGrid, ya::FBoxSlotArgs{.sizeRule       = ya::EUIBoxSlotSizeRule::Auto,
                                           .crossAlignment = ya::EUIBoxSlotCrossAlignment::Start,
                                           .preferredSize  = {400.0f, 0.0f}});
    form.child(body("GalleryTableCaption_Body",
                    "Click a row to select (reactive selection ref); row 3 / col 2 holds a real button widget."));

    auto page = ya::ui::panel("TableDemo").setColor(kPanelColor).child(std::move(form), ya::ui::canvasSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    (void)state;
}

} // namespace guiworkbench

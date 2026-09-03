// Binding-layer regression guards (G4.1/G4.2): Reactive now lives under
// GUI/Binding rather than GUI/Widgets. These tests lock the persistent edge
// contract at the binding layer boundary instead of piggybacking only on the
// older snapshot tests.

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

#include <thread>

namespace ya
{

namespace
{

struct ReactiveRelay final : ReactiveBase
{
    Reactive<int>* target = nullptr;
    int nextValue = 0;
    int dirtyCalls = 0;

    void markComputedDirty() override
    {
        ++dirtyCalls;
        if (target) {
            target->set(nextValue);
        }
    }
};

WidgetEventContext pointAt(float x, float y)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {x, y};
    return ctx;
}

} // namespace

TEST(BindingContractTest, ReactiveTransactionCoalescesWritesAndSupportsReentrantBinding)
{
    WidgetTree tree({.width = 320, .height = 120});
    auto text = std::make_shared<UIText>("Text");
    auto value = std::make_shared<Reactive<std::string>>("a");
    text->bindText(value);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);
    tree.buildSnapshot({});

    const auto before = getReactiveDiagnostics();
    {
        ReactiveTransaction tx;
        value->set("b");
        value->set("c");
        value->set("c");
    }
    const auto after = getReactiveDiagnostics();
    EXPECT_EQ(after.notifyCalls - before.notifyCalls, 1u);
    tree.buildSnapshot({});
    EXPECT_EQ(text->resolvedText(), "c");
}

TEST(BindingContractTest, ReactiveMutationFromForeignThreadIsRejected)
{
    Reactive<int> value(1);
    value.set(2);
    const auto before = getReactiveDiagnostics();
    std::thread worker([&] { value.set(3); });
    worker.join();
    const auto after = getReactiveDiagnostics();
    EXPECT_EQ(value.value(), 2);
    EXPECT_EQ(after.wrongThreadMutations - before.wrongThreadMutations, 1u);
}

TEST(BindingContractTest, ReentrantReactiveMutationIsDeferredUntilOuterNotifyCompletes)
{
    Reactive<int> source(1);
    Reactive<int> secondary(10);
    ReactiveRelay relay;
    relay.target = &secondary;
    relay.nextValue = 20;
    source.addComputedDependent(&relay);

    const auto before = getReactiveDiagnostics();
    source.set(2);
    const auto after = getReactiveDiagnostics();

    EXPECT_EQ(relay.dirtyCalls, 1);
    EXPECT_EQ(secondary.value(), 20);
    EXPECT_EQ(after.deferredReentrantNotifications - before.deferredReentrantNotifications, 1u);
    EXPECT_EQ(after.notifyCalls - before.notifyCalls, 2u);
}

TEST(BindingContractTest, KeyedReactiveListReportsStructuralDiffAndRevision)
{
    struct Row { std::string id; int value = 0; };
    ReactiveList<Row> rows;
    EXPECT_EQ(rows.revision(), 0u);
    ASSERT_TRUE(rows.replaceKeyed({{"a", 1}, {"b", 2}}, [](const Row& row) { return row.id; }));
    EXPECT_EQ(rows.revision(), 1u);
    EXPECT_EQ(rows.lastDiff().inserted.size(), 2u);
    ASSERT_TRUE(rows.replaceKeyed({{"b", 2}, {"c", 3}}, [](const Row& row) { return row.id; }));
    EXPECT_EQ(rows.lastDiff().removed.size(), 1u);
    EXPECT_EQ(rows.lastDiff().inserted.size(), 1u);
    EXPECT_EQ(rows.lastDiff().moved.size(), 1u);
    EXPECT_EQ(rows.revision(), 2u);
    ASSERT_FALSE(rows.replaceKeyed({{"c", 3}, {"c", 4}}, [](const Row& row) { return row.id; }));
    EXPECT_EQ(rows.size(), 2u);
}

TEST(BindingContractTest, KeyedReactiveListMutationContractPreservesIdentity)
{
    struct Row { std::string id; int value = 0; };
    ReactiveList<Row> rows;
    ASSERT_TRUE(rows.replaceKeyed({{"a", 1}, {"b", 2}, {"c", 3}},
                                  [](const Row& row) { return row.id; }));

    ASSERT_TRUE(rows.insertAt(1, {"x", 9}));
    ASSERT_EQ(rows.lastDiff().inserted.size(), 1u);
    EXPECT_EQ(rows.get(1).id, "x");

    ASSERT_TRUE(rows.updateAt(1, {"x", 10}));
    ASSERT_EQ(rows.lastDiff().updated.size(), 1u);
    EXPECT_EQ(rows.get(1).value, 10);
    EXPECT_FALSE(rows.updateAt(1, {"renamed", 10}));

    ASSERT_TRUE(rows.move(1, 3));
    ASSERT_EQ(rows.lastDiff().moved.size(), 1u);
    EXPECT_EQ(rows.get(3).id, "x");

    EXPECT_FALSE(rows.insertAt(0, {"a", 99}));
    EXPECT_FALSE(rows.removeAt(99));

    ASSERT_TRUE(rows.replaceKeyed({{"a", 1}, {"b", 2}, {"c", 3}, {"x", 11}},
                                  [](const Row& row) { return row.id; }));
    EXPECT_EQ(rows.lastDiff().updated.size(), 4u);

    rows.clear();
    EXPECT_EQ(rows.lastDiff().removed.size(), 4u);
}

TEST(BindingContractTest, ComputedCachesUntilAnUpstreamChangeMarksItDirty)
{
    auto source = std::make_shared<Reactive<int>>(1);
    int calls = 0;
    Computed<int> doubled([&] {
        ++calls;
        return source->get() * 2;
    });

    const auto before = getReactiveDiagnostics();
    EXPECT_EQ(doubled.get(), 2);
    EXPECT_EQ(doubled.get(), 2);
    EXPECT_EQ(calls, 1);

    source->set(3);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(doubled.get(), 6);
    EXPECT_EQ(calls, 2);

    const auto after = getReactiveDiagnostics();
    EXPECT_EQ(after.computedRecomputes - before.computedRecomputes, 2u);
}

TEST(BindingContractTest, ComputedChainsPropagateDirtyAcrossDerivedValues)
{
    auto source = std::make_shared<Reactive<int>>(2);
    int firstCalls = 0;
    int secondCalls = 0;
    Computed<int> plusOne([&] {
        ++firstCalls;
        return source->get() + 1;
    });
    Computed<int> doubled([&] {
        ++secondCalls;
        return plusOne.get() * 2;
    });

    EXPECT_EQ(doubled.get(), 6);
    EXPECT_EQ(firstCalls, 1);
    EXPECT_EQ(secondCalls, 1);

    source->set(4);
    EXPECT_EQ(secondCalls, 1);
    EXPECT_EQ(doubled.get(), 10);
    EXPECT_EQ(firstCalls, 2);
    EXPECT_EQ(secondCalls, 2);
}

TEST(BindingContractTest, ComputedCycleReportsDiagnosticsAndReturnsTheLastStableCache)
{
    std::shared_ptr<Computed<int>> a;
    std::shared_ptr<Computed<int>> b;
    a = std::make_shared<Computed<int>>([&] { return b ? b->get() + 1 : 1; });
    b = std::make_shared<Computed<int>>([&] { return a->get() + 1; });

    const auto before = getReactiveDiagnostics();
    EXPECT_EQ(a->get(), 2);
    const auto after = getReactiveDiagnostics();
    EXPECT_EQ(after.computedCycles - before.computedCycles, 1u);
}

TEST(BindingContractTest, ComputedDropsUpstreamPointerWhenSourceIsDestroyedFirst)
{
    const auto before = getReactiveDiagnostics();
    auto source = std::make_unique<Reactive<int>>(7);
    auto derived = std::make_unique<Computed<int>>([&] { return source ? source->get() * 2 : 0; });
    EXPECT_EQ(derived->get(), 14);
    source.reset();
    const auto after = getReactiveDiagnostics();
    EXPECT_EQ(after.computedUpstreamUnlinks - before.computedUpstreamUnlinks, 1u);
}

TEST(BindingContractTest, TreeViewKeyedReplacePrunesRemovedExpansionState)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    ASSERT_TRUE(roots->replaceKeyed(
        {
            {.id = "keep", .label = "Keep", .children = {{.id = "keep.child", .label = "Keep Child"}}},
            {.id = "drop", .label = "Drop", .children = {{.id = "drop.child", .label = "Drop Child"}}},
        },
        [](const UITreeView::FNode& node) { return node.id; }));

    auto treeView = std::make_shared<UITreeView>("Tree");
    FCanvasSlotArgs slot;
    slot.fixedSize = {240.0f, 160.0f};
    treeView->bindData(roots);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), treeView, slot);
    tree.buildSnapshot({});

    treeView->setExpanded("keep", true);
    treeView->setExpanded("drop", true);
    tree.buildSnapshot({});
    EXPECT_EQ(treeView->getVisibleRowCount(), 4);

    ASSERT_TRUE(roots->replaceKeyed(
        {
            {.id = "keep", .label = "Keep", .children = {{.id = "keep.child", .label = "Keep Child"}}},
        },
        [](const UITreeView::FNode& node) { return node.id; }));
    tree.buildSnapshot({});
    EXPECT_TRUE(treeView->isExpanded("keep"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 2);

    ASSERT_TRUE(roots->replaceKeyed(
        {
            {.id = "drop", .label = "Drop", .children = {{.id = "drop.child", .label = "Drop Child"}}},
            {.id = "keep", .label = "Keep", .children = {{.id = "keep.child", .label = "Keep Child"}}},
        },
        [](const UITreeView::FNode& node) { return node.id; }));
    tree.buildSnapshot({});
    EXPECT_FALSE(treeView->isExpanded("drop"));
    EXPECT_TRUE(treeView->isExpanded("keep"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 3);
}

TEST(BindingContractTest, PersistentLayoutBindingOnDetachedWidgetDoesNotInvalidateTree)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    auto       ratio = std::make_shared<Reactive<float>>(0.5f);
    split->bindSplitRatio(ratio);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    const uint64_t before = tree.getPerfStats().layoutDirtyTransitions;

    tree.detach(*split);
    ratio->set(0.8f);
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, before);
}

TEST(BindingContractTest, PersistentLayoutBindingSurvivesDetachAndReattach)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    auto       ratio = std::make_shared<Reactive<float>>(0.5f);
    split->bindSplitRatio(ratio);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.detach(*split);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});

    const uint64_t before = tree.getPerfStats().layoutDirtyTransitions;
    ratio->set(0.25f);
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_GT(tree.getPerfStats().layoutDirtyTransitions, before);
}

TEST(BindingContractTest, TextBindingSurvivesImperativeFallbackWriteUntilUnbound)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       text = std::make_shared<UIText>("Text");
    auto       ref  = std::make_shared<Reactive<std::string>>("bound");
    text->bindText(ref);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(text->resolvedText(), "bound");

    text->setText("fallback");
    EXPECT_EQ(text->getText(), "fallback");
    EXPECT_EQ(text->resolvedText(), "bound");

    ref->set("next");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(text->resolvedText(), "next");

    text->bindText(nullptr);
    EXPECT_EQ(text->resolvedText(), "fallback");
}

TEST(BindingContractTest, WidgetEnabledGateRemainsAuthoritativeOverButtonDisplayBinding)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button  = std::make_shared<UIButton>("Button");
    auto       enabled = std::make_shared<Reactive<bool>>(true);
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {20.0f, 20.0f};
    buttonSlot.fixedSize = {120.0f, 40.0f};
    button->bindEnabled(enabled);
    button->setEnabled(false);
    int clicks = 0;
    button->_onClick = [&]() { ++clicks; };
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 40.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(40.0f, 40.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 0);

    enabled->set(false);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_FALSE(button->resolvedEnabled());

    enabled->set(true);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_TRUE(button->resolvedEnabled());
    EXPECT_FALSE(button->isEnabledInTree());
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 40.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 0);
}

TEST(BindingContractTest, DisablingPressedButtonStillClearsPressSessionOnRelease)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("Button");
    int        clicks = 0;
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {20.0f, 20.0f};
    buttonSlot.fixedSize = {120.0f, 40.0f};
    button->_onClick = [&]() { ++clicks; };
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 40.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(button->_bPressed.get());
    ASSERT_EQ(tree.getPointerCapture(), button.get());

    button->setEnabled(false);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(40.0f, 40.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed.get());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_EQ(clicks, 0);
}

TEST(BindingContractTest, BehaviorDropHighlightDoesNotOverwritePresenterSelectionState)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       source = std::make_shared<UISelectableRow>("Source");
    auto       target = std::make_shared<UISelectableRow>("Target");
    source->_itemId = "source";
    target->_itemId = "target";
    source->setDraggable(true);
    target->setDraggable(true);
    source->setDragPayload("payload.source");
    target->setDraggable(true);
    target->setSelected(true);
    FCanvasSlotArgs sourceSlot;
    sourceSlot.offset = {20.0f, 20.0f};
    sourceSlot.fixedSize = {160.0f, 24.0f};
    FCanvasSlotArgs targetSlot;
    targetSlot.offset = {220.0f, 20.0f};
    targetSlot.fixedSize = {160.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(80.0f, 32.0f), pointAt(80.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(tree.isDragging());

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(260.0f, 32.0f), pointAt(260.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(target->_bDropHighlighted.get());
    EXPECT_TRUE(target->_bSelected);

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(260.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(tree.isDragging());
    EXPECT_FALSE(target->_bDropHighlighted.get());
    EXPECT_TRUE(target->_bSelected);
}

TEST(BindingContractTest, PresenterSelectionPatchDoesNotClearBehaviorDropHighlight)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       source = std::make_shared<UISelectableRow>("Source");
    auto       target = std::make_shared<UISelectableRow>("Target");
    source->_itemId = "source";
    target->_itemId = "target";
    source->setDraggable(true);
    target->setDraggable(true);
    source->setDragPayload("payload.source");
    FCanvasSlotArgs sourceSlot;
    sourceSlot.offset = {20.0f, 20.0f};
    sourceSlot.fixedSize = {160.0f, 24.0f};
    FCanvasSlotArgs targetSlot;
    targetSlot.offset = {220.0f, 20.0f};
    targetSlot.fixedSize = {160.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(80.0f, 32.0f), pointAt(80.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(tree.isDragging());

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(260.0f, 32.0f), pointAt(260.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(target->_bDropHighlighted.get());

    target->setSelected(true);
    EXPECT_TRUE(target->_bSelected);
    EXPECT_TRUE(target->_bDropHighlighted.get());

    target->setSelected(false);
    EXPECT_FALSE(target->_bSelected);
    EXPECT_TRUE(target->_bDropHighlighted.get());

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(260.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(tree.isDragging());
    EXPECT_FALSE(target->_bDropHighlighted.get());
    EXPECT_FALSE(target->_bSelected);
}

TEST(BindingContractTest, TreeFilterBindingAndManualExpansionCoexistWithoutStickyReexpand)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push(UITreeView::FNode{
        .id = "root",
        .label = "Root",
        .children = {UITreeView::FNode{.id = "child", .label = "Child Node"}},
    });

    auto filterRef = std::make_shared<Reactive<std::string>>("");
    auto treeView  = std::make_shared<UITreeView>("Tree");
    FCanvasSlotArgs treeViewSlot;
    treeViewSlot.offset = {20.0f, 20.0f};
    treeViewSlot.fixedSize = {240.0f, 120.0f};
    treeView->bindData(roots);
    treeView->bindFilter(filterRef);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), treeView, treeViewSlot);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_FALSE(treeView->isExpanded("root"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 1);

    filterRef->set("Child");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_TRUE(treeView->isExpanded("root"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 2);

    treeView->toggleExpanded("root");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_FALSE(treeView->isExpanded("root"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 1);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_FALSE(treeView->isExpanded("root"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 1);

    filterRef->set("");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_FALSE(treeView->isExpanded("root"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 1);

    filterRef->set("Child");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_TRUE(treeView->isExpanded("root"));
    EXPECT_EQ(treeView->getVisibleRowCount(), 2);
}

TEST(BindingContractTest, TableSelectionBindingCoexistsWithHoverTransientState)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       rows = std::make_shared<ReactiveList<UITableGrid::FTableRow>>();
    rows->push(UITableGrid::FTableRow{.id = "header", .cells = {"Name"}});
    rows->push(UITableGrid::FTableRow{.id = "row-a", .cells = {"A"}});
    rows->push(UITableGrid::FTableRow{.id = "row-b", .cells = {"B"}});

    auto       selected = std::make_shared<Reactive<int>>(1);
    auto       table    = std::make_shared<UITableGrid>("Table");
    FCanvasSlotArgs tableSlot;
    tableSlot.offset = {20.0f, 20.0f};
    tableSlot.fixedSize = {220.0f, 96.0f};
    table->bindData(rows);
    table->bindSelection(selected);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), table, tableSlot);
    tree.layout();

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(table->getSelection()->value(), 1);

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(60.0f, 74.0f), pointAt(60.0f, 74.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(selected->value(), 1);

    selected->set(2);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(table->getSelection()->value(), 2);

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(60.0f, 52.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(selected->value(), 1);

    table->clearTransientInputState();
    EXPECT_EQ(selected->value(), 1);
}

TEST(BindingContractTest, MenuBarLabelBindingSurvivesOpenMenuAndHoverRouting)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       bar = std::make_shared<UIMenuBar>("Bar");
    FCanvasSlotArgs barSlot;
    barSlot.anchorMin = {0.0f, 0.0f};
    barSlot.anchorMax = {1.0f, 0.0f};
    barSlot.fixedSize = {0.0f, 30.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bar, barSlot);

    auto label = std::make_shared<Reactive<std::string>>("File");
    auto* item = bar->addItem("Fallback", [] { return UIMenu::create({{"Open", [] {}}, {"Save", [] {}}}); });
    ASSERT_NE(item, nullptr);
    item->bindLabel(label);
    tree.layout();

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(item->resolvedLabel(), "File");
    EXPECT_EQ(item->_label, "Fallback");

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(30.0f, 15.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_NE(bar->getOpenMenu(), nullptr);

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(30.0f, 15.0f), pointAt(30.0f, 15.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_NE(bar->getOpenMenu(), nullptr);

    label->set("Project");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(item->resolvedLabel(), "Project");
    EXPECT_EQ(item->_label, "Fallback");
    EXPECT_NE(bar->getOpenMenu(), nullptr);

    item->bindLabel(nullptr);
    EXPECT_EQ(item->resolvedLabel(), "Fallback");
    EXPECT_NE(bar->getOpenMenu(), nullptr);
}

} // namespace ya

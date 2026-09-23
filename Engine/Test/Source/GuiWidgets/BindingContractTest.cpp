// Binding-layer regression guards (G4.1/G4.2): Reactive now lives under
// GUI/Binding rather than GUI/Widgets. These tests lock the persistent edge
// contract at the binding layer boundary instead of piggybacking only on the
// older snapshot tests.

#include "GUI/Binding/Reactive.h"
#include "GUI/Binding/SelectionModel.h"
#include "GUI/Binding/ActionMap.h"
#include "GUI/Binding/UndoStack.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/KeyedChildReconciler.h"
#include "GUI/Widgets/KeyedVisibleWindow.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

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

KeyPressedEvent makeKeyPress(EKey::T key, uint32_t mod = 0, bool bRepeat = false)
{
    KeyPressedEvent ev;
    ev._keyCode = key;
    ev._mod     = mod;
    ev.bRepeat  = bRepeat;
    return ev;
}

uint32_t primaryMod()
{
#if defined(__APPLE__)
    return EKeyMod::LMeta;
#else
    return EKeyMod::LCtrl;
#endif
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
    EXPECT_FALSE(button->resolvedEnabled());
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

TEST(BindingContractTest, TreeViewFirstPaintRegistersExpandLayoutDependent)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push(UITreeView::FNode{
        .id = "root",
        .label = "Root",
        .children = {UITreeView::FNode{.id = "child", .label = "Child"}},
    });

    auto treeView = std::make_shared<UITreeView>("Tree");
    FCanvasSlotArgs slot;
    slot.offset    = {20.0f, 20.0f};
    slot.fixedSize = {240.0f, 0.0f};
    treeView->bindData(roots);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), treeView, slot);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(treeView->getVisibleRowCount(), 1);
    EXPECT_FLOAT_EQ(treeView->computeDesiredSize().y, treeView->_rowHeight);

    treeView->toggleExpanded("root");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(treeView->getVisibleRowCount(), 2);
    EXPECT_FLOAT_EQ(treeView->computeDesiredSize().y, treeView->_rowHeight * 2.0f);
}

TEST(BindingContractTest, TableSelectionBindingCoexistsWithHoverTransientState)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       rows = std::make_shared<ReactiveList<UITableGrid::FTableRow>>();
    rows->push(UITableGrid::FTableRow{.id = "header", .cells = {"Name"}});
    rows->push(UITableGrid::FTableRow{.id = "row-a", .cells = {"A"}});
    rows->push(UITableGrid::FTableRow{.id = "row-b", .cells = {"B"}});

    auto       selected = std::make_shared<Reactive<std::string>>("row-a");
    auto       table    = std::make_shared<UITableGrid>("Table");
    FCanvasSlotArgs tableSlot;
    tableSlot.offset = {20.0f, 20.0f};
    tableSlot.fixedSize = {220.0f, 96.0f};
    table->bindData(rows);
    table->bindSelection(selected);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), table, tableSlot);
    tree.layout();

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(table->getSelection()->value(), "row-a");

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(60.0f, 74.0f), pointAt(60.0f, 74.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(selected->value(), "row-a");

    selected->set("row-b");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(table->getSelection()->value(), "row-b");

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(60.0f, 52.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(selected->value(), "row-a");

    table->clearTransientInputState();
    EXPECT_EQ(selected->value(), "row-a");
}

TEST(BindingContractTest, TableGridSplitterDragResizesColumnAndRow)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto rows = std::make_shared<ReactiveList<UITableGrid::FTableRow>>();
    rows->push(UITableGrid::FTableRow{.id = "h", .cells = {"A", "B"}});
    rows->push(UITableGrid::FTableRow{.id = "r1", .cells = {"1", "2"}});
    rows->push(UITableGrid::FTableRow{.id = "r2", .cells = {"3", "4"}});

    auto table = std::make_shared<UITableGrid>("Table");
    table->_columnWidths = {100.0f, 100.0f};
    table->bindData(rows);
    FCanvasSlotArgs slot;
    slot.offset    = {20.0f, 20.0f};
    slot.fixedSize = {200.0f, 66.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), table, slot);
    tree.layout();

    const float splitX = 120.0f;
    const float midY   = 31.0f;
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(splitX, midY)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(splitX + 40.0f, midY), pointAt(splitX + 40.0f, midY)),
              EWidgetRouteResult::HandledExclusive);
    tree.layout();
    EXPECT_FLOAT_EQ(table->_columnWidths[0], 140.0f);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(splitX + 40.0f, midY)),
              EWidgetRouteResult::HandledExclusive);

    const float rowSplitY = 42.0f;
    const float midX      = 40.0f;
    const float oldHeight = table->_rowHeight;
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(midX, rowSplitY)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(midX, rowSplitY + 10.0f), pointAt(midX, rowSplitY + 10.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FLOAT_EQ(table->_rowHeight, oldHeight + 10.0f);
}

TEST(BindingContractTest, TableSelectionFollowsKeyedRowAcrossInsertMoveAndRemove)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto rows = std::make_shared<ReactiveList<UITableGrid::FTableRow>>();
    ASSERT_TRUE(rows->replaceKeyed({
        {.id = "a", .cells = {"A"}},
        {.id = "b", .cells = {"B"}},
        {.id = "c", .cells = {"C"}},
    }, [](const UITableGrid::FTableRow& row) { return row.id; }));
    auto selected = std::make_shared<Reactive<std::string>>("b");
    auto table = std::make_shared<UITableGrid>("Table");
    table->bindData(rows);
    table->bindSelection(selected);
    FCanvasSlotArgs slot;
    slot.fixedSize = {240.0f, 120.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), table, slot).valid());

    tree.buildSnapshot({});
    ASSERT_EQ(selected->value(), "b");
    ASSERT_TRUE(rows->insertAt(0, {.id = "x", .cells = {"X"}}));
    tree.buildSnapshot({});
    EXPECT_EQ(selected->value(), "b");

    ASSERT_TRUE(rows->move(2, 3));
    tree.buildSnapshot({});
    EXPECT_EQ(selected->value(), "b");

    ASSERT_TRUE(rows->removeAt(3));
    tree.buildSnapshot({});
    EXPECT_EQ(selected->value(), "");
}

TEST(BindingContractTest, KeyedChildReconcilerPreservesInstancesAndOrder)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto parent = std::make_shared<UIContainer>("Rows");
    FCanvasSlotArgs slot;
    slot.fixedSize = {320.0f, 120.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent, slot).valid());

    int factoryCalls = 0;
    UIKeyedChildReconciler reconciler(tree, *parent,
                                      [&factoryCalls](const std::string& key, size_t) {
                                          ++factoryCalls;
                                          return std::make_shared<UIText>(key);
                                      });
    ASSERT_TRUE(reconciler.reconcile({"a", "b", "c"}));
    ASSERT_EQ(factoryCalls, 3);
    const UIElementRef a = reconciler.find("a");
    const UIElementRef c = reconciler.find("c");

    ASSERT_TRUE(reconciler.reconcile({"c", "a", "d"}));
    EXPECT_EQ(factoryCalls, 4);
    EXPECT_EQ(reconciler.find("a").get(), a.get());
    EXPECT_EQ(reconciler.find("c").get(), c.get());
    EXPECT_EQ(reconciler.find("b"), nullptr);
    ASSERT_EQ(parent->getChildren().size(), 3u);
    EXPECT_EQ(parent->getChildren()[0].get(), c.get());
    EXPECT_EQ(parent->getChildren()[1].get(), a.get());
    EXPECT_EQ(parent->getChildren()[2]->_stableKey, "d");
    EXPECT_TRUE(reconciler.reconcile({"c", "a"}));
    EXPECT_EQ(reconciler.size(), 2u);
    EXPECT_EQ(parent->getChildren().size(), 2u);
}

TEST(BindingContractTest, KeyedChildReconcilerUpdaterRunsOnReusedInstances)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto parent = std::make_shared<UIContainer>("Rows");
    FCanvasSlotArgs slot;
    slot.fixedSize = {320.0f, 120.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent, slot).valid());

    int factoryCalls = 0;
    int updaterCalls = 0;
    UIKeyedChildReconciler reconciler(tree, *parent,
                                      [&factoryCalls](const std::string& key, size_t) {
                                          ++factoryCalls;
                                          auto text = std::make_shared<UIText>(key);
                                          text->setText("init");
                                          return text;
                                      });

    const auto applyLabels = [&updaterCalls](UIElement& child, const std::string& key, size_t index) {
        ++updaterCalls;
        auto* text = dynamic_cast<UIText*>(&child);
        ASSERT_NE(text, nullptr);
        text->setText(key + std::to_string(index));
    };

    ASSERT_TRUE(reconciler.reconcile({"a", "b"}, applyLabels));
    EXPECT_EQ(factoryCalls, 2);
    EXPECT_EQ(updaterCalls, 2);
    auto* a = dynamic_cast<UIText*>(reconciler.find("a").get());
    auto* b = dynamic_cast<UIText*>(reconciler.find("b").get());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->getText(), "a0");
    EXPECT_EQ(b->getText(), "b1");

    ASSERT_TRUE(reconciler.reconcile({"b", "a"}, applyLabels));
    EXPECT_EQ(factoryCalls, 2);
    EXPECT_EQ(updaterCalls, 4);
    EXPECT_EQ(reconciler.find("a").get(), a);
    EXPECT_EQ(reconciler.find("b").get(), b);
    EXPECT_EQ(a->getText(), "a1");
    EXPECT_EQ(b->getText(), "b0");
    ASSERT_EQ(parent->getChildren().size(), 2u);
    EXPECT_EQ(parent->getChildren()[0].get(), b);
    EXPECT_EQ(parent->getChildren()[1].get(), a);
}

TEST(BindingContractTest, KeyedChildReconcilerUpdaterRefreshesSelectableState)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto parent = std::make_shared<UIContainer>("Rows");
    FCanvasSlotArgs slot;
    slot.fixedSize = {320.0f, 120.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent, slot).valid());

    int factoryCalls = 0;
    UIKeyedChildReconciler reconciler(tree, *parent,
                                      [&factoryCalls](const std::string& key, size_t) {
                                          ++factoryCalls;
                                          auto row = std::make_shared<UISelectableRow>(key);
                                          row->_itemId = key;
                                          return row;
                                      });

    const auto applySelection = [](const std::string& selected) {
        return [selected](UIElement& child, const std::string& key, size_t) {
            auto* row = dynamic_cast<UISelectableRow*>(&child);
            ASSERT_NE(row, nullptr);
            row->setSelected(key == selected);
        };
    };

    ASSERT_TRUE(reconciler.reconcile({"dir-a", "dir-b"}, applySelection("dir-a")));
    EXPECT_EQ(factoryCalls, 2);
    auto* a = dynamic_cast<UISelectableRow*>(reconciler.find("dir-a").get());
    auto* b = dynamic_cast<UISelectableRow*>(reconciler.find("dir-b").get());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(a->_bSelected);
    EXPECT_FALSE(b->_bSelected);

    ASSERT_TRUE(reconciler.reconcile({"dir-a", "dir-b"}, applySelection("dir-b")));
    EXPECT_EQ(factoryCalls, 2);
    EXPECT_EQ(reconciler.find("dir-a").get(), a);
    EXPECT_EQ(reconciler.find("dir-b").get(), b);
    EXPECT_FALSE(a->_bSelected);
    EXPECT_TRUE(b->_bSelected);
}

TEST(BindingContractTest, KeyedVisibleWindowCoversViewportAndClampsOverscan)
{
    const FKeyedVisibleWindow empty = computeKeyedVisibleWindow(0, 22.0f, 2.0f, 80.0f, 0.0f, 2);
    EXPECT_TRUE(empty.empty());
    EXPECT_FLOAT_EQ(empty.contentExtent, 0.0f);

    const FKeyedVisibleWindow unlaidOut = computeKeyedVisibleWindow(10, 22.0f, 2.0f, 0.0f, 0.0f, 2);
    EXPECT_EQ(unlaidOut.first, 0u);
    EXPECT_EQ(unlaidOut.count, 10u);
    EXPECT_FLOAT_EQ(unlaidOut.leadingExtent, 0.0f);
    EXPECT_FLOAT_EQ(unlaidOut.trailingExtent, 0.0f);
    EXPECT_FLOAT_EQ(unlaidOut.contentExtent, 10.0f * 22.0f + 9.0f * 2.0f);

    const FKeyedVisibleWindow firstPage = computeKeyedVisibleWindow(10, 22.0f, 2.0f, 80.0f, 0.0f, 1);
    EXPECT_EQ(firstPage.first, 0u);
    EXPECT_EQ(firstPage.count, 5u);
    EXPECT_FLOAT_EQ(firstPage.leadingExtent, 0.0f);
    EXPECT_FLOAT_EQ(firstPage.trailingExtent, 5.0f * 24.0f);
    EXPECT_FLOAT_EQ(firstPage.leadingExtent + (5.0f * 22.0f + 4.0f * 2.0f) + firstPage.trailingExtent,
                    firstPage.contentExtent);

    const FKeyedVisibleWindow scrolled = computeKeyedVisibleWindow(10, 22.0f, 2.0f, 80.0f, 48.0f, 1);
    EXPECT_EQ(scrolled.first, 1u);
    EXPECT_EQ(scrolled.count, 6u);
    EXPECT_FLOAT_EQ(scrolled.leadingExtent, 24.0f);
    EXPECT_EQ(scrolled.end(), 7u);
    EXPECT_FLOAT_EQ(scrolled.trailingExtent, 3.0f * 24.0f);

    const std::vector<std::string> keys{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    EXPECT_EQ(sliceKeyedVisibleWindow(keys, firstPage),
              (std::vector<std::string>{"a", "b", "c", "d", "e"}));
    EXPECT_EQ(sliceKeyedVisibleWindow(keys, scrolled),
              (std::vector<std::string>{"b", "c", "d", "e", "f", "g"}));
}

TEST(BindingContractTest, KeyedVisibleWindowSpacersPreserveContentExtent)
{
    WidgetTree tree({.width = 320, .height = 400});
    auto leading = std::make_shared<UISizeBox>("Leading");
    auto rows = std::make_shared<UIContainer>("Rows");
    auto trailing = std::make_shared<UISizeBox>("Trailing");
    auto host = std::make_shared<UIContainer>("Host");
    host->setDirection(EWidgetBoxLayout::Vertical);
    host->setSpacing(0.0f);
    rows->setDirection(EWidgetBoxLayout::Vertical);
    rows->setSpacing(2.0f);

    FCanvasSlotArgs slot;
    slot.fixedSize = {320.0f, 400.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), host, slot).valid());
    ASSERT_TRUE(tree.attach(*host, leading).valid());
    ASSERT_TRUE(tree.attach(*host, rows).valid());
    ASSERT_TRUE(tree.attach(*host, trailing).valid());

    int factoryCalls = 0;
    UIKeyedChildReconciler reconciler(tree, *rows,
                                      [&factoryCalls](const std::string& key, size_t) {
                                          ++factoryCalls;
                                          auto row = std::make_shared<UISizeBox>(key);
                                          row->setHeightOverride(22.0f);
                                          return row;
                                      });
    const auto bindHeight = [](UISlot& edge, const std::string&, size_t) {
        if (auto* box = edge.as<UIBoxSlot>()) {
            box->setPreferredSize({0.0f, 22.0f});
        }
    };

    std::vector<std::string> keys;
    keys.reserve(10);
    for (int i = 0; i < 10; ++i) {
        keys.push_back(std::to_string(i));
    }

    auto applyWindow = [&](float scrollOffset) {
        const FKeyedVisibleWindow window =
            computeKeyedVisibleWindow(keys.size(), 22.0f, 2.0f, 80.0f, scrollOffset, 1);
        leading->setHeightOverride(window.leadingExtent);
        trailing->setHeightOverride(window.trailingExtent);
        EXPECT_TRUE(reconciler.reconcile(sliceKeyedVisibleWindow(keys, window), {}, bindHeight));
        tree.layout();
        EXPECT_EQ(reconciler.size(), window.count);
        EXPECT_EQ(rows->getChildren().size(), window.count);
        const float visibleExtent = window.count == 0
                                        ? 0.0f
                                        : static_cast<float>(window.count) * 22.0f +
                                              static_cast<float>(window.count - 1) * 2.0f;
        EXPECT_FLOAT_EQ(leading->computeDesiredSize().y, window.leadingExtent);
        EXPECT_FLOAT_EQ(trailing->computeDesiredSize().y, window.trailingExtent);
        EXPECT_FLOAT_EQ(rows->computeDesiredSize().y, visibleExtent);
        EXPECT_FLOAT_EQ(host->computeDesiredSize().y, window.contentExtent);
        return window;
    };

    const FKeyedVisibleWindow first = applyWindow(0.0f);
    EXPECT_EQ(first.first, 0u);
    const int firstFactoryCalls = factoryCalls;
    EXPECT_EQ(firstFactoryCalls, static_cast<int>(first.count));

    const UIElementRef kept = reconciler.find("2");
    const FKeyedVisibleWindow scrolled = applyWindow(48.0f);
    EXPECT_GT(scrolled.first, 0u);
    EXPECT_LT(scrolled.count, keys.size());
    EXPECT_EQ(reconciler.find("2").get(), kept.get());
    EXPECT_LT(factoryCalls, static_cast<int>(keys.size()));
    EXPECT_EQ(reconciler.find("0"), nullptr);
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

TEST(BindingContractTest, SelectionModelSingleMultiPrimaryAndTransientRoles)
{
    SelectionModel model;
    EXPECT_TRUE(model.selected().empty());
    EXPECT_TRUE(model.primary().empty());

    model.select("a");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"a"}));
    EXPECT_EQ(model.primary(), "a");
    EXPECT_TRUE(model.contains("a"));
    EXPECT_FALSE(model.contains("b"));

    model.add("b");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(model.primary(), "b");

    model.add("b");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(model.primary(), "b");

    model.toggle("a");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"b"}));
    EXPECT_EQ(model.primary(), "b");

    model.toggle("a");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"b", "a"}));
    EXPECT_EQ(model.primary(), "a");

    model.remove("a");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"b"}));
    EXPECT_EQ(model.primary(), "b");

    model.setHovered("h");
    model.setActive("k");
    model.setFocused("f");
    EXPECT_EQ(model.hovered(), "h");
    EXPECT_EQ(model.active(), "k");
    EXPECT_EQ(model.focused(), "f");

    model.clear();
    EXPECT_TRUE(model.selected().empty());
    EXPECT_TRUE(model.primary().empty());
    EXPECT_EQ(model.hovered(), "h");
    EXPECT_EQ(model.active(), "k");
    EXPECT_EQ(model.focused(), "f");

    model.clearTransient();
    EXPECT_TRUE(model.hovered().empty());
    EXPECT_TRUE(model.active().empty());
    EXPECT_TRUE(model.focused().empty());

    model.select("");
    EXPECT_TRUE(model.selected().empty());
}

TEST(BindingContractTest, SelectionModelReplaceSetsOrderedSetAndPrimary)
{
    SelectionModel model;
    model.replace({"b", "a", "b"}, "a");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(model.primary(), "a");

    model.replace({"c", "d"});
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"c", "d"}));
    EXPECT_EQ(model.primary(), "c");

    const uint64_t revision = model.revision();
    model.replace({"c", "d"}, "c");
    EXPECT_EQ(model.revision(), revision);

    model.replace({}, "ghost");
    EXPECT_EQ(model.selected(), (std::vector<std::string>{"ghost"}));
    EXPECT_EQ(model.primary(), "ghost");

    model.replace({});
    EXPECT_TRUE(model.selected().empty());
    EXPECT_TRUE(model.primary().empty());
}

TEST(BindingContractTest, SharedSelectionModelReplaceUpdatesBoundTreeViews)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push(UITreeView::FNode{.id = "a", .label = "A"});
    roots->push(UITreeView::FNode{.id = "b", .label = "B"});

    auto model = std::make_shared<SelectionModel>();
    auto left  = std::make_shared<UITreeView>("Left");
    auto right = std::make_shared<UITreeView>("Right");
    left->bindData(roots);
    right->bindData(roots);
    left->bindSelection(model->primaryRef());
    right->bindSelection(model->primaryRef());

    FCanvasSlotArgs leftSlot;
    leftSlot.offset    = {20.0f, 20.0f};
    leftSlot.fixedSize = {200.0f, 80.0f};
    FCanvasSlotArgs rightSlot;
    rightSlot.offset    = {240.0f, 20.0f};
    rightSlot.fixedSize = {200.0f, 80.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), left, leftSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), right, rightSlot);
    tree.buildSnapshot(UIFrameBuildContext{});

    model->replace({"b", "a"}, "b");
    EXPECT_EQ(left->getSelection()->value(), "b");
    EXPECT_EQ(right->getSelection()->value(), "b");
    EXPECT_EQ(model->selected(), (std::vector<std::string>{"b", "a"}));
}

TEST(BindingContractTest, SharedSelectionModelDrivesTwoTreeViews)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push(UITreeView::FNode{.id = "a", .label = "A"});
    roots->push(UITreeView::FNode{.id = "b", .label = "B"});

    auto model = std::make_shared<SelectionModel>();
    auto left  = std::make_shared<UITreeView>("Left");
    auto right = std::make_shared<UITreeView>("Right");
    left->bindData(roots);
    right->bindData(roots);
    left->bindSelection(model->primaryRef());
    right->bindSelection(model->primaryRef());
    left->_onSelectionChanged  = [&](const std::string& id) { model->select(id); };
    right->_onSelectionChanged = [&](const std::string& id) { model->select(id); };

    FCanvasSlotArgs leftSlot;
    leftSlot.offset    = {20.0f, 20.0f};
    leftSlot.fixedSize = {200.0f, 80.0f};
    FCanvasSlotArgs rightSlot;
    rightSlot.offset    = {240.0f, 20.0f};
    rightSlot.fixedSize = {200.0f, 80.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), left, leftSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), right, rightSlot);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(40.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(model->primary(), "a");
    EXPECT_EQ(model->selected(), (std::vector<std::string>{"a"}));
    EXPECT_EQ(left->getSelection()->value(), "a");
    EXPECT_EQ(right->getSelection()->value(), "a");

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(260.0f, 56.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(model->primary(), "b");
    EXPECT_EQ(left->getSelection()->value(), "b");
    EXPECT_EQ(right->getSelection()->value(), "b");
}

TEST(BindingContractTest, ActionMapExecuteAndShortcutShareOneHandler)
{
    ActionMap actions;
    int       saved = 0;
    int       savedAs = 0;
    ASSERT_TRUE(actions.define({
        .id      = "scene.save",
        .label   = "Save Scene",
        .chord   = FActionChord::primary(EKey::K_S),
        .execute = [&]() { ++saved; },
    }));
    ASSERT_TRUE(actions.define({
        .id      = "scene.saveAs",
        .label   = "Save Scene As",
        .chord   = FActionChord::primary(EKey::K_S, true),
        .execute = [&]() { ++savedAs; },
    }));
    EXPECT_FALSE(actions.define({
        .id      = "scene.save",
        .label   = "Dup",
        .execute = []() {},
    }));
    EXPECT_FALSE(actions.define({
        .id      = "other.save",
        .label   = "Dup chord",
        .chord   = FActionChord::primary(EKey::K_S),
        .execute = []() {},
    }));

    EXPECT_EQ(actions.execute("scene.save"), EActionResult::Ran);
    EXPECT_EQ(saved, 1);
    EXPECT_EQ(actions.execute("missing"), EActionResult::Missing);

    EXPECT_TRUE(actions.dispatchKey(makeKeyPress(EKey::K_S, primaryMod()), false));
    EXPECT_EQ(saved, 2);
    EXPECT_TRUE(actions.dispatchKey(makeKeyPress(EKey::K_S, primaryMod() | EKeyMod::Shift), false));
    EXPECT_EQ(savedAs, 1);
    EXPECT_EQ(saved, 2);

    EXPECT_FALSE(actions.dispatchKey(makeKeyPress(EKey::K_S, primaryMod(), true), false));
    EXPECT_FALSE(actions.dispatchKey(makeKeyPress(EKey::K_S), false));
    EXPECT_FALSE(actions.dispatchKey(makeKeyPress(EKey::K_N, primaryMod()), false));
}

TEST(BindingContractTest, ActionMapMenuItemAndDisabledShortcutShareExecute)
{
    ActionMap actions;
    int       ran = 0;
    bool      bEnabled = true;
    ASSERT_TRUE(actions.define({
        .id         = "edit.undo",
        .label      = "Undo",
        .chord      = FActionChord::primary(EKey::K_Z),
        .execute    = [&]() { ++ran; },
        .canExecute = [&]() { return bEnabled; },
    }));

    UIMenu::FItem item = UIMenu::FItem::fromAction(actions, "edit.undo");
    EXPECT_EQ(item.label, "Undo");
    EXPECT_FALSE(item.shortcut.empty());
    ASSERT_TRUE(item.action);
    item.action();
    EXPECT_EQ(ran, 1);

    bEnabled = false;
    EXPECT_EQ(actions.execute("edit.undo"), EActionResult::Disabled);
    EXPECT_FALSE(actions.dispatchKey(makeKeyPress(EKey::K_Z, primaryMod()), false));
    EXPECT_EQ(ran, 1);

    EXPECT_FALSE(actions.dispatchKey(makeKeyPress(EKey::K_Z), true));
    UIMenu::FItem missing = UIMenu::FItem::fromAction(actions, "edit.missing");
    EXPECT_FALSE(missing.bEnabled);
    EXPECT_FALSE(missing.action);
}

TEST(BindingContractTest, UndoStackPushUndoRedoAndRejectsIncompleteCommands)
{
    UndoStack stack;
    int       value = 0;
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
    EXPECT_FALSE(stack.push({.label = "noop"}));
    EXPECT_TRUE(stack.push({
        .label = "Set",
        .undo  = [&]() { value = 0; },
        .redo  = [&]() { value = 4; },
    }));
    value = 4;
    EXPECT_TRUE(stack.canUndo());
    EXPECT_EQ(stack.undoLabel(), "Set");
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(stack.canRedo());
    EXPECT_TRUE(stack.redo());
    EXPECT_EQ(value, 4);
    EXPECT_FALSE(stack.canRedo());
}

TEST(BindingContractTest, UndoStackMergesOnlyWithinOneDragSession)
{
    UndoStack stack;
    int       value = 0;
    auto pushValue = [&](int next) {
        const int before = value;
        value            = next;
        return stack.push({
            .label    = "Drag",
            .mergeKey = "position.x",
            .undo     = [&value, before]() { value = before; },
            .redo     = [&value, next]() { value = next; },
        });
    };

    stack.beginMerge();
    EXPECT_TRUE(pushValue(1));
    EXPECT_TRUE(pushValue(5));
    EXPECT_TRUE(pushValue(9));
    stack.endMerge();
    EXPECT_EQ(value, 9);
    EXPECT_EQ(stack.undoCount(), 1u);
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value, 0);

    stack.beginMerge();
    EXPECT_TRUE(pushValue(2));
    stack.endMerge();
    EXPECT_EQ(stack.undoCount(), 1u);
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(value, 0);
}

TEST(BindingContractTest, UndoStackGroupBatchesNestedPushesAsOneStep)
{
    UndoStack stack;
    int       x = 0;
    int       y = 0;
    {
        UndoTransaction tx(stack, "Paste");
        EXPECT_TRUE(stack.push({
            .label = "X",
            .undo  = [&]() { x = 0; },
            .redo  = [&]() { x = 1; },
        }));
        x = 1;
        EXPECT_TRUE(stack.push({
            .label = "Y",
            .undo  = [&]() { y = 0; },
            .redo  = [&]() { y = 2; },
        }));
        y = 2;
        EXPECT_FALSE(stack.canUndo());
        {
            UndoTransaction inner(stack, "Inner");
            EXPECT_TRUE(stack.push({
                .label = "X2",
                .undo  = [&]() { x = 1; },
                .redo  = [&]() { x = 3; },
            }));
            x = 3;
        }
    }
    EXPECT_EQ(x, 3);
    EXPECT_EQ(y, 2);
    EXPECT_EQ(stack.undoCount(), 1u);
    EXPECT_EQ(stack.undoLabel(), "Paste");
    EXPECT_TRUE(stack.undo());
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_TRUE(stack.redo());
    EXPECT_EQ(x, 3);
    EXPECT_EQ(y, 2);
}

TEST(BindingContractTest, UndoStackActionMapUndoRedoShareExecute)
{
    UndoStack stack;
    ActionMap actions;
    int       value = 0;
    ASSERT_TRUE(actions.define({
        .id         = "edit.undo",
        .label      = "Undo",
        .chord      = FActionChord::primary(EKey::K_Z),
        .execute    = [&]() { (void)stack.undo(); },
        .canExecute = [&]() { return stack.canUndo(); },
    }));
    ASSERT_TRUE(actions.define({
        .id         = "edit.redo",
        .label      = "Redo",
        .chord      = FActionChord::primary(EKey::K_Z, true),
        .execute    = [&]() { (void)stack.redo(); },
        .canExecute = [&]() { return stack.canRedo(); },
    }));

    value = 7;
    ASSERT_TRUE(stack.push({
        .label = "Set",
        .undo  = [&]() { value = 0; },
        .redo  = [&]() { value = 7; },
    }));
    EXPECT_EQ(actions.execute("edit.undo"), EActionResult::Ran);
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(actions.dispatchKey(makeKeyPress(EKey::K_Z, primaryMod() | EKeyMod::Shift), false));
    EXPECT_EQ(value, 7);
    EXPECT_EQ(actions.execute("edit.redo"), EActionResult::Disabled);
}

} // namespace ya

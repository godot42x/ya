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

namespace ya
{

namespace
{

WidgetEventContext pointAt(float x, float y)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {x, y};
    return ctx;
}

} // namespace

TEST(BindingContractTest, PersistentLayoutBindingOnDetachedWidgetDoesNotInvalidateTree)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    auto       ratio = std::make_shared<Reactive<float>>(0.5f);
    split->bindSplitRatio(ratio);
    tree.attachToLayer(WidgetTree::ELayer::Content, split);

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
    tree.attachToLayer(WidgetTree::ELayer::Content, split);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.detach(*split);
    tree.attachToLayer(WidgetTree::ELayer::Content, split);
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
    tree.attachToLayer(WidgetTree::ELayer::Content, text);

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
    button->setPosition({20.0f, 20.0f});
    button->setSize({120.0f, 40.0f});
    button->bindEnabled(enabled);
    button->setEnabled(false);
    int clicks = 0;
    button->_onClick = [&]() { ++clicks; };
    tree.attachToLayer(WidgetTree::ELayer::Content, button);
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
    button->setPosition({20.0f, 20.0f});
    button->setSize({120.0f, 40.0f});
    button->_onClick = [&]() { ++clicks; };
    tree.attachToLayer(WidgetTree::ELayer::Content, button);
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
    source->setPosition({20.0f, 20.0f});
    source->setSize({160.0f, 24.0f});
    target->setPosition({220.0f, 20.0f});
    target->setSize({160.0f, 24.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, source);
    tree.attachToLayer(WidgetTree::ELayer::Content, target);
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
    source->setPosition({20.0f, 20.0f});
    source->setSize({160.0f, 24.0f});
    target->setPosition({220.0f, 20.0f});
    target->setSize({160.0f, 24.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, source);
    tree.attachToLayer(WidgetTree::ELayer::Content, target);
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
    treeView->setPosition({20.0f, 20.0f});
    treeView->setSize({240.0f, 120.0f});
    treeView->bindData(roots);
    treeView->bindFilter(filterRef);
    tree.attachToLayer(WidgetTree::ELayer::Content, treeView);

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
    table->setPosition({20.0f, 20.0f});
    table->setSize({220.0f, 96.0f});
    table->bindData(rows);
    table->bindSelection(selected);
    tree.attachToLayer(WidgetTree::ELayer::Content, table);
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
    bar->_anchorMin = {0.0f, 0.0f};
    bar->_anchorMax = {1.0f, 0.0f};
    bar->setSize({0.0f, 30.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, bar);

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

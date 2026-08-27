// Binding-layer regression guards (G4.1/G4.2): Reactive now lives under
// GUI/Binding rather than GUI/Widgets. These tests lock the persistent edge
// contract at the binding layer boundary instead of piggybacking only on the
// older snapshot tests.

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
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

} // namespace ya

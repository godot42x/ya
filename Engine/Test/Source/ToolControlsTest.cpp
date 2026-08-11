// Tool-GUI primitive regression tests (gui-app-bootstrap Phase 2). The
// target links ONLY the GUI closure, proving the stack/split/scroll/row
// primitives have no Scene/ECS/Render3D/Host dependency.

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SplitPane.h"

#include <gtest/gtest.h>

#include <memory>

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

KeyPressedEvent makeKeyPress(EKey::T key, uint32_t mod = 0, bool bRepeat = false)
{
    KeyPressedEvent ev;
    ev._keyCode = key;
    ev._mod     = mod;
    ev.bRepeat  = bRepeat;
    return ev;
}

} // namespace

// === Stack (UIContainer) ===

TEST(ToolControlsTest, StackLaysOutChildrenWithGapAndPadding)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       stack = std::make_shared<UIContainer>("Stack");
    stack->_direction = EWidgetBoxLayout::Vertical;
    stack->_position  = {20.0f, 20.0f};
    stack->_size      = {200.0f, 200.0f};
    stack->_padding   = 10.0f;
    stack->_spacing   = 8.0f;

    auto a = std::make_shared<UIPanel>("A");
    a->_size = {100.0f, 20.0f};
    auto b = std::make_shared<UIPanel>("B");
    b->_size = {120.0f, 30.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, stack);
    tree.attach(*stack, a);
    tree.attach(*stack, b);
    tree.layout();

    // Content starts at (30, 30); children pack vertically with 8px gap.
    EXPECT_EQ(a->_layoutRect.pos, glm::vec2(30.0f, 30.0f));
    EXPECT_EQ(a->_layoutRect.extent, glm::vec2(180.0f, 20.0f)); // cross axis stretches
    EXPECT_EQ(b->_layoutRect.pos, glm::vec2(30.0f, 58.0f));
    EXPECT_EQ(b->_layoutRect.extent, glm::vec2(180.0f, 30.0f));
}

TEST(ToolControlsTest, StackCollapsedSkipsSpaceHiddenKeepsSpace)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       stack = std::make_shared<UIContainer>("Stack");
    stack->_direction = EWidgetBoxLayout::Vertical;
    stack->_size      = {200.0f, 200.0f};
    stack->_spacing   = 4.0f;

    auto collapsed = std::make_shared<UIPanel>("Collapsed");
    collapsed->_size = {100.0f, 20.0f};
    collapsed->_visibility = EWidgetVisibility::Collapsed;
    auto hidden = std::make_shared<UIPanel>("Hidden");
    hidden->_size = {100.0f, 20.0f};
    hidden->_visibility = EWidgetVisibility::Hidden;
    auto visible = std::make_shared<UIPanel>("Visible");
    visible->_size = {100.0f, 20.0f};

    tree.attachToLayer(WidgetTree::ELayer::Content, stack);
    tree.attach(*stack, collapsed);
    tree.attach(*stack, hidden);
    tree.attach(*stack, visible);
    tree.layout();

    // Collapsed takes no layout slot; Hidden keeps its slot (but does not
    // render); Visible follows the Hidden slot + gap.
    EXPECT_EQ(hidden->_layoutRect.pos, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(visible->_layoutRect.pos, glm::vec2(0.0f, 24.0f));
}

TEST(ToolControlsTest, StackMainAxisAlignmentOffsetsThePack)
{
    WidgetTree tree({.width = 400, .height = 300});

    auto makeStack = [&](EWidgetMainAxisAlignment alignment) {
        auto stack = std::make_shared<UIContainer>("Stack");
        stack->_direction = EWidgetBoxLayout::Horizontal;
        stack->_size      = {300.0f, 50.0f};
        stack->_mainAxisAlignment = alignment;
        auto a = std::make_shared<UIPanel>("A");
        a->_size = {100.0f, 20.0f};
        auto b = std::make_shared<UIPanel>("B");
        b->_size = {100.0f, 20.0f};
        tree.attachToLayer(WidgetTree::ELayer::Content, stack);
        tree.attach(*stack, a);
        tree.attach(*stack, b);
        return std::make_pair(stack, a);
    };

    {
        auto [stack, first] = makeStack(EWidgetMainAxisAlignment::Start);
        tree.layout();
        EXPECT_EQ(first->_layoutRect.pos.x, 0.0f);
        tree.detach(*stack);
    }
    {
        // 200px packed inside 300px -> 50px offset per side for Center.
        auto [stack, first] = makeStack(EWidgetMainAxisAlignment::Center);
        tree.layout();
        // Packed extent is 204 (100 + 4 spacing + 100): (300-204)/2 = 48.
        EXPECT_EQ(first->_layoutRect.pos.x, 48.0f);
        tree.detach(*stack);
    }
    {
        auto [stack, first] = makeStack(EWidgetMainAxisAlignment::End);
        tree.layout();
        EXPECT_EQ(first->_layoutRect.pos.x, 96.0f);
        tree.detach(*stack);
    }
}

TEST(ToolControlsTest, StackDesiredSizeAggregatesChildren)
{
    auto stack = std::make_shared<UIContainer>("Stack");
    stack->_direction = EWidgetBoxLayout::Vertical;
    stack->_padding   = 10.0f;
    stack->_spacing   = 4.0f;
    auto a = std::make_shared<UIPanel>("A");
    a->_size = {100.0f, 20.0f};
    auto b = std::make_shared<UIPanel>("B");
    b->_size = {120.0f, 30.0f};
    stack->addDetachedChild(a);
    stack->addDetachedChild(b);

    // {main-axis packed (20+4+30) + 2*padding, cross max (120) + 2*padding}
    EXPECT_EQ(stack->computeDesiredSize(), glm::vec2(74.0f, 140.0f));
}

// === Split pane ===

TEST(ToolControlsTest, SplitPaneLaysOutTwoPanesAroundDivider)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->_position = {0.0f, 0.0f};
    split->_size     = {300.0f, 200.0f};
    split->_splitRatio      = 0.5f;
    split->_dividerThickness = 6.0f;
    auto left  = std::make_shared<UIPanel>("Left");
    auto right = std::make_shared<UIPanel>("Right");
    tree.attachToLayer(WidgetTree::ELayer::Content, split);
    tree.attach(*split, left);
    tree.attach(*split, right);
    tree.layout();

    EXPECT_EQ(left->_layoutRect.pos, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(left->_layoutRect.extent, glm::vec2(147.0f, 200.0f));
    EXPECT_EQ(right->_layoutRect.pos, glm::vec2(153.0f, 0.0f));
    EXPECT_EQ(right->_layoutRect.extent, glm::vec2(147.0f, 200.0f));

    const Rect2D divider = split->getDividerRect();
    EXPECT_EQ(divider.pos, glm::vec2(147.0f, 0.0f));
    EXPECT_EQ(divider.extent, glm::vec2(6.0f, 200.0f));
}

TEST(ToolControlsTest, SplitPaneDividerDragChangesRatioAndEndsSession)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->_size      = {300.0f, 200.0f};
    split->_splitRatio = 0.5f;
    auto left  = std::make_shared<UIPanel>("Left");
    auto right = std::make_shared<UIPanel>("Right");
    tree.attachToLayer(WidgetTree::ELayer::Content, split);
    tree.attach(*split, left);
    tree.attach(*split, right);
    tree.layout();

    // Press on the divider center: drag session owns focus + capture.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(150.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(split->_bDraggingDivider);
    EXPECT_EQ(tree.getPointerCapture(), split.get());
    EXPECT_EQ(tree.getFocused(), split.get());

    // Drag right by 30px: ratio 0.5 -> 0.6 and layout is invalidated.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(180.0f, 100.0f), pointAt(180.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_NEAR(split->_splitRatio, 0.6f, 1e-4f);
    EXPECT_FALSE(tree.isLayoutValid());

    // Drag past the first pane minimum: ratio clamps at 40/300.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(5.0f, 100.0f), pointAt(5.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_NEAR(split->_splitRatio, 40.0f / 300.0f, 1e-4f);

    // Release anywhere (capture): session ends, ratio persists.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(5.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(split->_bDraggingDivider);
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_NEAR(split->_splitRatio, 40.0f / 300.0f, 1e-4f);
}

TEST(ToolControlsTest, SplitPanePressOnPaneFallsThroughToChild)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->_size      = {300.0f, 200.0f};
    split->_splitRatio = 0.5f;
    auto left = std::make_shared<UIContainer>("Left");
    auto button = std::make_shared<UIButton>("Button");
    button->_position = {10.0f, 10.0f};
    button->_size     = {60.0f, 24.0f};
    auto right = std::make_shared<UIPanel>("Right");
    tree.attachToLayer(WidgetTree::ELayer::Content, split);
    tree.attach(*split, left);
    tree.attach(*split, right);
    tree.attach(*left, button);
    tree.layout();

    int clicks = 0;
    button->_onClick = [&] { ++clicks; };

    // Click inside the left pane over the button: the button consumes it and
    // the split never starts a divider drag.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(20.0f, 20.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(20.0f, 20.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);
    EXPECT_FALSE(split->_bDraggingDivider);
    EXPECT_NE(tree.getPointerCapture(), split.get());
}

// === Scroll viewport ===

TEST(ToolControlsTest, ScrollViewportShiftsContentByOffset)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       viewport = std::make_shared<UIScrollViewport>("Scroll");
    viewport->_size        = {200.0f, 60.0f};
    viewport->_scrollOffset = 30.0f;
    auto content = std::make_shared<UIPanel>("Content");
    content->_size = {200.0f, 100.0f}; // taller than the viewport
    tree.attachToLayer(WidgetTree::ELayer::Content, viewport);
    tree.attach(*viewport, content);
    tree.layout();

    EXPECT_EQ(content->_layoutRect.pos, glm::vec2(0.0f, -30.0f));
    EXPECT_EQ(content->_layoutRect.extent, glm::vec2(200.0f, 100.0f));
    EXPECT_NEAR(viewport->getMaxScrollOffset(), 40.0f, 1e-4f);
    EXPECT_TRUE(viewport->isScrollable());
}

TEST(ToolControlsTest, ScrollViewportWheelConsumesWhenScrollableBubblesAtLimit)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       viewport = std::make_shared<UIScrollViewport>("Scroll");
    viewport->_size = {200.0f, 60.0f};
    viewport->_scrollStep = 40.0f;
    auto content = std::make_shared<UIPanel>("Content");
    content->_size = {200.0f, 100.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, viewport);
    tree.attach(*viewport, content);
    tree.layout();

    const auto at = pointAt(100.0f, 30.0f);
    // Wheel down (negative y) scrolls toward the content end.
    EXPECT_EQ(tree.dispatchEvent(MouseScrolledEvent(0.0f, -1.0f), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(viewport->_scrollOffset, 40.0f);
    EXPECT_EQ(tree.dispatchEvent(MouseScrolledEvent(0.0f, -1.0f), at),
              EWidgetRouteResult::NotHandled); // at the limit: bubbles out
    EXPECT_EQ(viewport->_scrollOffset, 40.0f);
    // Wheel up scrolls back.
    EXPECT_EQ(tree.dispatchEvent(MouseScrolledEvent(0.0f, 1.0f), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(viewport->_scrollOffset, 0.0f);
}

TEST(ToolControlsTest, ScrollViewportCullsChildHitsOutsideViewport)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       viewport = std::make_shared<UIScrollViewport>("Scroll");
    viewport->_size = {200.0f, 60.0f};
    auto content = std::make_shared<UIPanel>("Content");
    content->_size = {200.0f, 100.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, viewport);
    tree.attach(*viewport, content);
    tree.layout();

    // Content rect spans y 0..100; the viewport only covers y 0..60. A point
    // below the viewport must not hit the content even though the content
    // rect contains it.
    EXPECT_EQ(tree.pickAt({100.0f, 80.0f}), nullptr);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(100.0f, 80.0f)),
              EWidgetRouteResult::NotHandled);
    // Inside the viewport the content is reachable.
    EXPECT_EQ(tree.pickAt({100.0f, 30.0f}), content.get());
}

TEST(ToolControlsTest, ScrollViewportNestedInsideSplitKeepsCustomLayout)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->_size      = {300.0f, 200.0f};
    split->_splitRatio = 0.5f;
    auto scroll = std::make_shared<UIScrollViewport>("Scroll");
    scroll->_size = {100.0f, 60.0f};
    auto content = std::make_shared<UIPanel>("Content");
    content->_size = {100.0f, 120.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, split);
    tree.attach(*split, scroll);
    tree.attach(*scroll, content);
    tree.layout();

    // The split assigns the scroll its pane rect; the scroll still applies
    // its own offset/clamp instead of falling back to anchor layout.
    EXPECT_EQ(scroll->_layoutRect.extent, glm::vec2(147.0f, 200.0f));
    EXPECT_NEAR(scroll->getMaxScrollOffset(), 0.0f, 1e-4f); // content fits after stretch
    EXPECT_EQ(content->_layoutRect.extent, glm::vec2(147.0f, 200.0f));
}

// === Selectable row ===

TEST(ToolControlsTest, SelectableRowPressSelectsReleaseActivates)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       row = std::make_shared<UISelectableRow>("Row");
    row->_itemId = "item.1";
    row->_position = {0.0f, 0.0f};
    row->_size     = {200.0f, 24.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, row);
    tree.layout();

    std::vector<std::string> selected;
    std::vector<std::string> activated;
    row->_onSelect   = [&](const std::string& id) { selected.push_back(id); };
    row->_onActivate = [&](const std::string& id) { activated.push_back(id); };

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(50.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(selected, std::vector<std::string>{"item.1"});
    EXPECT_EQ(tree.getFocused(), row.get());
    EXPECT_EQ(tree.getPointerCapture(), row.get());

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(50.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(activated, std::vector<std::string>{"item.1"});
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
}

TEST(ToolControlsTest, SelectableRowEnterActivatesFocusedRow)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       row = std::make_shared<UISelectableRow>("Row");
    row->_itemId = "item.2";
    row->_position = {0.0f, 0.0f};
    row->_size     = {200.0f, 24.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, row);
    tree.layout();

    std::vector<std::string> selected;
    std::vector<std::string> activated;
    row->_onSelect   = [&](const std::string& id) { selected.push_back(id); };
    row->_onActivate = [&](const std::string& id) { activated.push_back(id); };
    tree.setFocus(row.get());

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Space), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(selected.empty()); // keyboard activation never selects
    EXPECT_EQ(activated, std::vector<std::string>({"item.2", "item.2"}));
}

TEST(ToolControlsTest, SelectableRowParticipatesInTabTraversal)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       first  = std::make_shared<UISelectableRow>("First");
    auto       second = std::make_shared<UISelectableRow>("Second");
    first->_size  = {200.0f, 24.0f};
    second->_size = {200.0f, 24.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, first);
    tree.attachToLayer(WidgetTree::ELayer::Content, second);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Tab), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), first.get());
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Tab), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), second.get());
}

} // namespace ya

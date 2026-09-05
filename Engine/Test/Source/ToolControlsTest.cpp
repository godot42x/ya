// Tool-GUI primitive regression tests (gui-app-bootstrap Phase 2). The
// target links ONLY the GUI closure, proving the stack/split/scroll/row
// primitives have no Scene/ECS/Render3D/Host dependency.

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/WidgetTreeDump.h"
#include "GUI/Widgets/Theme.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/Controls/DragFloat.h"
#include "GUI/Widgets/Controls/SpinBox.h"
#include "GUI/Widgets/Controls/ColorEdit.h"
#include "Render/Resources/FontManager.h"

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

uint32_t primaryMod()
{
#if defined(__APPLE__)
    return EKeyMod::LMeta;
#else
    return EKeyMod::LCtrl;
#endif
}

// Synthetic font: every ASCII glyph advances 8px at the given size (no GPU
// needed). Menus measure labels through the font manager.
std::shared_ptr<Font> registerMenuFont(float fontSize = 13.0f, float advance = 8.0f)
{
    auto font        = std::make_shared<Font>();
    font->fontSize   = fontSize;
    font->lineHeight = 17.0f;
    font->ascent     = 13.0f;
    font->descent    = 3.0f;
    for (uint32_t cp = 32; cp < 127; ++cp) {
        Character ch;
        ch.size     = {static_cast<int>(advance), static_cast<int>(fontSize)};
        ch.bearing  = {0, 0};
        ch.advance  = {advance, 0.0f};
        ch.bInAtlas = true;
        font->characters[cp] = ch;
    }
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, static_cast<uint32_t>(fontSize), font);
    return font;
}

void attachPreferredSize(UIElement& parent, const UIElementRef& child, glm::vec2 size)
{
    parent.addDetachedChild(child, [size](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize(size);
        }
        else if (auto* overlay = dynamic_cast<UIOverlaySlot*>(&slot)) {
            overlay->setPreferredSize(size);
        }
        else if (auto* single = dynamic_cast<UIOverlaySlot*>(&slot)) {
            single->setPreferredSize(size);
        }
        else if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
            canvas->setFixedSize(size);
        }
    });
}

} // namespace

// === Stack (UIContainer) ===

TEST(ToolControlsTest, StackLaysOutChildrenWithGapAndPadding)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       stack = std::make_shared<UIContainer>("Stack");
    stack->setDirection(EWidgetBoxLayout::Vertical);
    stack->setPadding({10.0f, 10.0f});
    stack->setSpacing(8.0f);

    auto a = std::make_shared<UIPanel>("A");
    auto b = std::make_shared<UIPanel>("B");
    FCanvasSlotArgs stackSlot;
    stackSlot.offset = {20.0f, 20.0f};
    stackSlot.fixedSize = {200.0f, 200.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), stack, stackSlot);
    attachPreferredSize(*stack, a, {100.0f, 20.0f});
    attachPreferredSize(*stack, b, {120.0f, 30.0f});
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
    stack->setDirection(EWidgetBoxLayout::Vertical);
    stack->setSpacing(4.0f);

    auto collapsed = std::make_shared<UIPanel>("Collapsed");
    collapsed->setVisibility(EWidgetVisibility::Collapsed);
    auto hidden = std::make_shared<UIPanel>("Hidden");
    hidden->setVisibility(EWidgetVisibility::Hidden);
    auto visible = std::make_shared<UIPanel>("Visible");
    FCanvasSlotArgs stackSlot;
    stackSlot.fixedSize = {200.0f, 200.0f};

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), stack, stackSlot);
    attachPreferredSize(*stack, collapsed, {100.0f, 20.0f});
    attachPreferredSize(*stack, hidden, {100.0f, 20.0f});
    attachPreferredSize(*stack, visible, {100.0f, 20.0f});
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
        stack->setDirection(EWidgetBoxLayout::Horizontal);
        stack->setMainAxisAlignment(alignment);
        auto a = std::make_shared<UIPanel>("A");
        auto b = std::make_shared<UIPanel>("B");
        FCanvasSlotArgs stackSlot;
        stackSlot.fixedSize = {300.0f, 50.0f};
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), stack, stackSlot);
        attachPreferredSize(*stack, a, {100.0f, 20.0f});
        attachPreferredSize(*stack, b, {100.0f, 20.0f});
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
    stack->setDirection(EWidgetBoxLayout::Vertical);
    stack->setPadding({10.0f, 10.0f});
    stack->setSpacing(4.0f);
    auto a = std::make_shared<UIPanel>("A");
    auto b = std::make_shared<UIPanel>("B");
    attachPreferredSize(*stack, a, {100.0f, 20.0f});
    attachPreferredSize(*stack, b, {120.0f, 30.0f});

    // Vertical stack: width = cross max (120) + 2*padding, height = packed
    // main axis (20+4+30) + 2*padding.
    EXPECT_EQ(stack->computeDesiredSize(), glm::vec2(140.0f, 74.0f));
}

TEST(ToolControlsTest, ContainerStretchLastChildFillsRemainingSpace)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       box = std::make_shared<UIContainer>("Box");
    box->setDirection(EWidgetBoxLayout::Vertical);
    box->setSpacing(4.0f);
    box->setStretchLastChild(true);
    FCanvasSlotArgs boxSlot;
    boxSlot.fixedSize = {200.0f, 200.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), box, boxSlot);

    auto header = std::make_shared<UIPanel>("Header");
    auto content = std::make_shared<UIPanel>("Content");
    attachPreferredSize(*box, header, {200.0f, 30.0f});
    attachPreferredSize(*box, content, {200.0f, 50.0f});
    tree.layout();

    // Header keeps its 30px; content absorbs the remainder (200 - 30 - 4).
    EXPECT_FLOAT_EQ(content->_layoutRect.pos.y, 34.0f);
    EXPECT_FLOAT_EQ(content->_layoutRect.extent.y, 166.0f);
}

// === Split pane ===

TEST(ToolControlsTest, SplitPaneLaysOutTwoPanesAroundDivider)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->setSplitRatio(0.5f);
    split->setDividerThickness(6.0f);
    auto left  = std::make_shared<UIPanel>("Left");
    auto right = std::make_shared<UIPanel>("Right");
    FCanvasSlotArgs splitSlot;
    splitSlot.fixedSize = {300.0f, 200.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
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
    split->setSplitRatio(0.5f);
    auto left  = std::make_shared<UIPanel>("Left");
    auto right = std::make_shared<UIPanel>("Right");
    FCanvasSlotArgs splitSlot;
    splitSlot.fixedSize = {300.0f, 200.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
    tree.attach(*split, left);
    tree.attach(*split, right);
    tree.layout();

    // Press on the divider center: drag session owns focus + capture.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(150.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(split->_bDraggingDivider);
    EXPECT_EQ(tree.getPointerCapture(), split.get());
    EXPECT_EQ(tree.getFocused(), split.get());

    // Drag right by 30px: ratio 0.5 -> 0.6 and layout is invalidated.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(180.0f, 100.0f), pointAt(180.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_NEAR(split->getSplitRatio(), 0.6f, 1e-4f);
    EXPECT_FALSE(tree.isLayoutValid());

    // Drag past the first pane minimum: ratio clamps at 40/300.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(5.0f, 100.0f), pointAt(5.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_NEAR(split->getSplitRatio(), 40.0f / 300.0f, 1e-4f);

    // Release anywhere (capture): session ends, ratio persists.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(5.0f, 100.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(split->_bDraggingDivider);
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_NEAR(split->getSplitRatio(), 40.0f / 300.0f, 1e-4f);
}

TEST(ToolControlsTest, SplitPanePressOnPaneFallsThroughToChild)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    FCanvasSlotArgs splitSlot; splitSlot.fixedSize = {300.0f, 200.0f};
    split->setSplitRatio(0.5f);
    auto left = std::make_shared<UIContainer>("Left");
    auto button = std::make_shared<UIButton>("Button");
    auto right = std::make_shared<UIPanel>("Right");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
    tree.attach(*split, left);
    tree.attach(*split, right);
    left->addDetachedChild(button, [](UIElement&, UISlot& edge) {
        if (auto* slot = edge.as<UIBoxSlot>()) slot->setPreferredSize({60.0f, 24.0f});
    });
    tree.layout();

    int clicks = 0;
    button->_onClick = [&] { ++clicks; };

    // Click inside the left pane over the button: the button consumes it and
    // the split never starts a divider drag.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(20.0f, 20.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(20.0f, 20.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);
    EXPECT_FALSE(split->_bDraggingDivider);
    EXPECT_NE(tree.getPointerCapture(), split.get());
}

TEST(ToolControlsTest, SplitPaneDividerHoverRequestsResizeCursor)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->setSplitRatio(0.5f);
    FCanvasSlotArgs splitSlot;
    splitSlot.fixedSize = {300.0f, 200.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
    tree.attach(*split, std::make_shared<UIPanel>("Left"));
    tree.attach(*split, std::make_shared<UIPanel>("Right"));
    tree.layout();

    // Away from the divider: no resize cursor requested.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(10.0f, 100.0f), pointAt(10.0f, 100.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(split->getCursor(), ECursorType::Arrow);

    // Over the divider: hover owned by the splitter, resize cursor requested.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(150.0f, 100.0f), pointAt(150.0f, 100.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_TRUE(split->_bHoveredDivider);
    EXPECT_EQ(tree.getHovered(), split.get());
    EXPECT_EQ(split->getCursor(), ECursorType::ResizeEastWest);

    // Leaving the divider drops the resize cursor request.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(10.0f, 100.0f), pointAt(10.0f, 100.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_FALSE(split->_bHoveredDivider);
    EXPECT_EQ(split->getCursor(), ECursorType::Arrow);
}

TEST(ToolControlsTest, ButtonHoverClearsOnPointerLeave)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       button = std::make_shared<UIButton>("Button");
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {10.0f, 10.0f};
    buttonSlot.fixedSize = {60.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.layout();

    // Hover in: button becomes the hover owner (Stop hit filter).
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(20.0f, 20.0f), pointAt(20.0f, 20.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bHovered);
    EXPECT_EQ(tree.getHovered(), button.get());

    // Hover out: enter/leave lifecycle clears the button's hover flag.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(200.0f, 200.0f), pointAt(200.0f, 200.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_FALSE(button->_bHovered);
    EXPECT_EQ(tree.getHovered(), nullptr);
}

TEST(ToolControlsTest, ToolbarSiblingHoverSwitchesAndClears)
{
    // Reproduce the GUIWorkbench Editor toolbar: a horizontal box with several
    // buttons separated by spacing/padding.
    WidgetTree tree({.width = 400, .height = 300});
    auto       toolbar = std::make_shared<UIContainer>("Toolbar");
    toolbar->setDirection(EWidgetBoxLayout::Horizontal);
    toolbar->setSpacing(8.0f);
    toolbar->setPadding({8.0f, 4.0f});

    auto add    = std::make_shared<UIButton>("Add");
    auto remove    = std::make_shared<UIButton>("Remove");
    FCanvasSlotArgs toolbarSlot;
    toolbarSlot.offset = {10.0f, 10.0f};
    toolbarSlot.fixedSize = {300.0f, 32.0f};

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), toolbar, toolbarSlot);
    attachPreferredSize(*toolbar, add, {44.0f, 24.0f});
    attachPreferredSize(*toolbar, remove, {60.0f, 24.0f});
    tree.layout();

    // Hover Add (content origin 18,14; Add spans x[18,62]).
    tree.dispatchEvent(MouseMoveEvent(40.0f, 26.0f), pointAt(40.0f, 26.0f));
    EXPECT_TRUE(add->_bHovered);
    EXPECT_EQ(tree.getHovered(), add.get());

    // Move to Remove (Remove spans x[70,130]): Add must clear, Remove highlight.
    tree.dispatchEvent(MouseMoveEvent(100.0f, 26.0f), pointAt(100.0f, 26.0f));
    EXPECT_FALSE(add->_bHovered);
    EXPECT_TRUE(remove->_bHovered);
    EXPECT_EQ(tree.getHovered(), remove.get());

    // Move to empty space (between/after buttons): no hover owner remains.
    tree.dispatchEvent(MouseMoveEvent(200.0f, 26.0f), pointAt(200.0f, 26.0f));
    EXPECT_FALSE(remove->_bHovered);
    EXPECT_EQ(tree.getHovered(), nullptr);
}

TEST(ToolControlsTest, ToolbarAutoSizeButtonWithLabelHoverClears)
{
    // Reproduce the exact Editor toolbar: a horizontally-stretched container
    // packing SizeToContent buttons that each carry a centered text label.
    registerMenuFont(14.0f, 8.0f);
    WidgetTree tree({.width = 400, .height = 300});

    auto toolbar = std::make_shared<UIContainer>("Toolbar");
    toolbar->setDirection(EWidgetBoxLayout::Horizontal);
    toolbar->setSpacing(8.0f);
    toolbar->setPadding({8.0f, 4.0f});

    auto makeButton = [](const std::string& name, const std::string& label) {
        auto button = std::make_shared<UIButton>(name);
        button->setContentPadding({10.0f, 4.0f});
        auto text          = std::make_shared<UIText>(name + "_Label");
        text->_fontSize    = 14;
        text->setText(label);
        text->setVisibility(EWidgetVisibility::SelfHitTestInvisible);
        text->_hAlign      = EWidgetAlignH::Center;
        text->_vAlign      = EWidgetAlignV::Center;
        button->addDetachedChild(text);
        return button;
    };

    auto add    = makeButton("Add", "Add");
    auto remove = makeButton("Remove", "Remove");

    FCanvasSlotArgs toolbarSlot;
    toolbarSlot.anchorMin = {0.0f, 0.0f};
    toolbarSlot.anchorMax = {1.0f, 0.0f};
    toolbarSlot.offset = {0.0f, 6.0f};
    toolbarSlot.fixedSize = {0.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), toolbar, toolbarSlot);
    tree.attach(*toolbar, add);
    tree.attach(*toolbar, remove);
    tree.layout();

    const auto center = [](const UIElement* e) {
        return e->_layoutRect.pos + e->_layoutRect.extent * 0.5f;
    };

    // Hover Add (over its label child: hover resolves through the text to the
    // button).
    const glm::vec2 addCenter = center(add.get());
    tree.dispatchEvent(MouseMoveEvent(addCenter.x, addCenter.y), pointAt(addCenter.x, addCenter.y));
    EXPECT_TRUE(add->_bHovered);
    EXPECT_EQ(tree.getHovered(), add.get());

    // Move to Remove: Add clears, Remove highlights.
    const glm::vec2 removeCenter = center(remove.get());
    tree.dispatchEvent(MouseMoveEvent(removeCenter.x, removeCenter.y), pointAt(removeCenter.x, removeCenter.y));
    EXPECT_FALSE(add->_bHovered);
    EXPECT_TRUE(remove->_bHovered);
    EXPECT_EQ(tree.getHovered(), remove.get());

    // Move to the toolbar's empty stretch (past the last button): no hover.
    const glm::vec2 empty = {toolbar->_layoutRect.pos.x + toolbar->_layoutRect.extent.x - 20.0f,
                             center(toolbar.get()).y};
    tree.dispatchEvent(MouseMoveEvent(empty.x, empty.y), pointAt(empty.x, empty.y));
    EXPECT_FALSE(remove->_bHovered);
    EXPECT_EQ(tree.getHovered(), nullptr);
}

TEST(ToolControlsTest, SplitPaneDoesNotStealHoverFromOverlappingButton)
{
    // Reproduce the Editor toolbar bug: a full-region hoverable split pane
    // (top padding reserves the toolbar strip) overlaps a toolbar button. Both
    // hit the same point; the deeper button must own hover, not the split.
    WidgetTree tree({.width = 400, .height = 300});

    auto panel = std::make_shared<UIContainer>("Panel");
    FCanvasSlotArgs panelSlot;
    panelSlot.anchorMin = {0.0f, 0.0f};
    panelSlot.anchorMax = {1.0f, 1.0f};

    // The parent is a box container (path A), so child intent goes on the slot:
    // stretch across the cross axis at the container's own main extent. The
    // anchors and position this used to set were dropped by the box layout.
    auto toolbar = std::make_shared<UIContainer>("Toolbar");
    toolbar->setDirection(EWidgetBoxLayout::Horizontal);
    toolbar->setSpacing(8.0f);
    toolbar->setPadding({8.0f, 4.0f});

    auto add    = std::make_shared<UIButton>("Add");

    // Auto size rule (the box default) is what this pane actually got: the
    // stretch anchors were ignored under a path-A parent.
    auto split = std::make_shared<UISplitPane>("Split");
    split->setPadding({0.0f, 42.0f}); // top padding overlaps the toolbar strip

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);
    tree.attach(*panel, toolbar);
    tree.attach(*toolbar, add);
    tree.attach(*panel, split);
    if (auto* slot = panel->getSlotForChild(*toolbar)) {
        if (auto* box = slot->as<UIBoxSlot>()) {
            box->setPreferredSize({0.0f, 32.0f});
        }
    }
    if (auto* slot = toolbar->getSlotForChild(*add)) {
        if (auto* box = slot->as<UIBoxSlot>()) {
            box->setPreferredSize({45.0f, 24.0f});
        }
    }
    tree.layout();

    const glm::vec2 addCenter = add->_layoutRect.pos + add->_layoutRect.extent * 0.5f;
    tree.dispatchEvent(MouseMoveEvent(addCenter.x, addCenter.y), pointAt(addCenter.x, addCenter.y));

    EXPECT_TRUE(add->_bHovered);
    EXPECT_EQ(tree.getHovered(), add.get());

    // Over the split's divider (not the button): the split owns hover for its
    // resize cursor as before.
    const glm::vec2 dividerCenter = split->getDividerRect().pos + split->getDividerRect().extent * 0.5f;
    tree.dispatchEvent(MouseMoveEvent(dividerCenter.x, dividerCenter.y), pointAt(dividerCenter.x, dividerCenter.y));
    EXPECT_FALSE(add->_bHovered);
    EXPECT_TRUE(split->_bHoveredDivider);
    EXPECT_EQ(tree.getHovered(), split.get());
}

// === Scroll viewport ===

TEST(ToolControlsTest, ScrollViewportShiftsContentByOffset)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       viewport = std::make_shared<UIScrollViewport>("Scroll");
    viewport->setScrollOffset(30.0f);
    auto content = std::make_shared<UIPanel>("Content");
    FCanvasSlotArgs viewportSlot;
    viewportSlot.fixedSize = {200.0f, 60.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), viewport, viewportSlot);
    attachPreferredSize(*viewport, content, {200.0f, 100.0f});
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
    viewport->setScrollStep(40.0f);
    auto content = std::make_shared<UIPanel>("Content");
    FCanvasSlotArgs viewportSlot;
    viewportSlot.fixedSize = {200.0f, 60.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), viewport, viewportSlot);
    attachPreferredSize(*viewport, content, {200.0f, 100.0f});
    tree.layout();

    const auto at = pointAt(100.0f, 30.0f);
    // Wheel down (negative y) scrolls toward the content end.
    EXPECT_EQ(tree.dispatchEvent(MouseScrolledEvent(0.0f, -1.0f), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(viewport->getScrollOffset(), 40.0f);
    EXPECT_EQ(tree.dispatchEvent(MouseScrolledEvent(0.0f, -1.0f), at),
              EWidgetRouteResult::NotHandled); // at the limit: bubbles out
    EXPECT_EQ(viewport->getScrollOffset(), 40.0f);
    // Wheel up scrolls back.
    EXPECT_EQ(tree.dispatchEvent(MouseScrolledEvent(0.0f, 1.0f), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(viewport->getScrollOffset(), 0.0f);
}

TEST(ToolControlsTest, ScrollViewportCullsChildHitsOutsideViewport)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       viewport = std::make_shared<UIScrollViewport>("Scroll");
    auto content = std::make_shared<UIPanel>("Content");
    FCanvasSlotArgs viewportSlot;
    viewportSlot.fixedSize = {200.0f, 60.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), viewport, viewportSlot);
    attachPreferredSize(*viewport, content, {200.0f, 100.0f});
    tree.layout();

    // Content rect spans y 0..100; the viewport only covers y 0..60. A point
    // below the viewport must not hit the content even though the content
    // rect contains it.
    EXPECT_EQ(tree.pickAt({100.0f, 80.0f}), nullptr);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(100.0f, 80.0f)),
              EWidgetRouteResult::NotHandled);
    // Inside the viewport the content is reachable.
    EXPECT_EQ(tree.pickAt({100.0f, 30.0f}), content.get());
}

TEST(ToolControlsTest, ScrollViewportNestedInsideSplitKeepsCustomLayout)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->setSplitRatio(0.5f);
    auto scroll = std::make_shared<UIScrollViewport>("Scroll");
    auto content = std::make_shared<UIPanel>("Content");
    FCanvasSlotArgs splitSlot;
    splitSlot.fixedSize = {300.0f, 200.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
    tree.attach(*split, scroll);
    tree.attach(*scroll, content);
    tree.layout();

    // The split assigns the scroll its pane rect; the scroll still applies
    // its own offset/clamp instead of falling back to anchor layout.
    EXPECT_EQ(scroll->_layoutRect.extent, glm::vec2(147.0f, 200.0f));
    EXPECT_NEAR(scroll->getMaxScrollOffset(), 0.0f, 1e-4f); // content fits after stretch
    EXPECT_EQ(content->_layoutRect.extent, glm::vec2(147.0f, 200.0f));
}

TEST(ToolControlsTest, SpecializedLayoutsAppearInTreeDump)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto split = std::make_shared<UISplitPane>("Split");
    FCanvasSlotArgs splitSlot;
    splitSlot.fixedSize = {300.0f, 200.0f};
    split->setSplitRatio(0.5f);
    auto scroll = std::make_shared<UIScrollViewport>("Scroll");
    auto content = std::make_shared<UIPanel>("Content");
    auto button = std::make_shared<UIButton>("Button");
    button->setContentPadding({7.0f, 3.0f});

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
    tree.attach(*split, scroll);
    tree.attach(*scroll, content);
    tree.attach(*split, button);
    if (auto* slot = scroll->getSlotForChild(*content)) {
        if (auto* canvas = slot->as<UICanvasSlot>()) {
            canvas->setFixedSize({100.0f, 300.0f});
            canvas->setWidthSizeMode(EWidgetSizeMode::Fixed);
            canvas->setHeightSizeMode(EWidgetSizeMode::Fixed);
        }
    }
    tree.layout();

    const nlohmann::json dump = dumpWidgetTree(tree);
    const auto* splitNode = findWidgetNode(dump, "Split");
    const auto* scrollNode = findWidgetNode(dump, "Scroll");
    const auto* buttonNode = findWidgetNode(dump, "Button");
    ASSERT_NE(splitNode, nullptr);
    ASSERT_NE(scrollNode, nullptr);
    ASSERT_NE(buttonNode, nullptr);
    EXPECT_EQ((*splitNode)["layout"]["type"], "split");
    EXPECT_EQ((*scrollNode)["layout"]["type"], "scroll");
    EXPECT_EQ((*buttonNode)["layout"]["type"], "singleChild");
    EXPECT_EQ((*buttonNode)["layout"]["padding"]["x"], 7.0f);
}

// === Selectable row ===

TEST(ToolControlsTest, SelectableRowPressSelectsReleaseActivates)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       row = std::make_shared<UISelectableRow>("Row");
    row->_itemId = "item.1";
    FCanvasSlotArgs rowSlot;
    rowSlot.fixedSize = {200.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), row, rowSlot);
    tree.layout();

    std::vector<std::string> selected;
    std::vector<std::string> activated;
    row->_onSelect   = [&](const std::string& id) { selected.push_back(id); };
    row->_onActivate = [&](const std::string& id) { activated.push_back(id); };

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(50.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(selected, std::vector<std::string>{"item.1"});
    EXPECT_EQ(tree.getFocused(), row.get());
    EXPECT_EQ(tree.getPointerCapture(), row.get());

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(50.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(activated, std::vector<std::string>{"item.1"});
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
}

TEST(ToolControlsTest, SelectableRowEnterActivatesFocusedRow)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       row = std::make_shared<UISelectableRow>("Row");
    row->_itemId = "item.2";
    FCanvasSlotArgs rowSlot;
    rowSlot.fixedSize = {200.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), row, rowSlot);
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
    FCanvasSlotArgs firstSlot;
    firstSlot.fixedSize = {200.0f, 24.0f};
    FCanvasSlotArgs secondSlot;
    secondSlot.offset = {0.0f, 24.0f};
    secondSlot.fixedSize = {200.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), first, firstSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), second, secondSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Tab), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), first.get());
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Tab), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), second.get());
}

TEST(ToolControlsTest, SelectableRowDraggableRowsUseBehaviorBackedDragDrop)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       source = std::make_shared<UISelectableRow>("Source");
    auto       target = std::make_shared<UISelectableRow>("Target");
    source->_itemId = "item.source";
    target->_itemId = "item.target";
    source->_bDraggable = true;
    source->_dragPayload = "payload.source";
    source->_dragGhostLabel = "Source Ghost";
    target->_bDraggable = true;
    FCanvasSlotArgs sourceSlot;
    sourceSlot.fixedSize = {180.0f, 24.0f};
    FCanvasSlotArgs targetSlot;
    targetSlot.offset = {220.0f, 0.0f};
    targetSlot.fixedSize = {180.0f, 24.0f};
    std::string droppedPayload;
    target->_onDropped = [&](const std::string& payload) { droppedPayload = payload; };
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getPointerCapture(), source.get());
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(80.0f, 12.0f), pointAt(80.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(tree.isDragging());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    const auto* sourceOp = tree.getDragOperation() ? tree.getDragOperation()->as<UIStringDragDropOperation>() : nullptr;
    ASSERT_NE(sourceOp, nullptr);
    EXPECT_EQ(sourceOp->text, "payload.source");

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(260.0f, 12.0f), pointAt(260.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(260.0f, 12.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(droppedPayload, "payload.source");
    EXPECT_FALSE(tree.isDragging());
}

TEST(ToolControlsTest, TreeViewReorderUsesBehaviorBackedDragDrop)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       view = std::make_shared<UITreeView>("Tree");
    FCanvasSlotArgs viewSlot;
    viewSlot.offset = {20.0f, 20.0f};
    viewSlot.fixedSize = {220.0f, 96.0f};
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push({.id = "node.1", .label = "Node 1"});
    roots->push({.id = "node.2", .label = "Node 2"});
    view->bindData(roots);
    view->setReorderable(true);

    std::string fromId;
    std::string toId;
    int         mode = -1;
    view->setOnReorderHandler([&](const std::string& from, const std::string& to, int dropMode)
    {
        fromId = from;
        toId   = to;
        mode   = dropMode;
    });

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), view, viewSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(80.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getPointerCapture(), view.get());
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(88.0f, 32.0f), pointAt(88.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(tree.isDragging());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    const auto* reorderOp = tree.getDragOperation() ? tree.getDragOperation()->as<FTreeReorderDragDropOp>() : nullptr;
    ASSERT_NE(reorderOp, nullptr);
    EXPECT_EQ(reorderOp->rowId, "node.1");

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(80.0f, 48.0f), pointAt(80.0f, 48.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(80.0f, 48.0f)),
              EWidgetRouteResult::HandledExclusive);

    EXPECT_EQ(fromId, "node.1");
    EXPECT_EQ(toId, "node.2");
    EXPECT_EQ(mode, 0);
    EXPECT_FALSE(tree.isDragging());
}

TEST(ToolControlsTest, TreeViewRightClickSelectsRowAndFiresContextMenu)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       view = std::make_shared<UITreeView>("Tree");
    FCanvasSlotArgs viewSlot;
    viewSlot.offset    = {20.0f, 20.0f};
    viewSlot.fixedSize = {220.0f, 96.0f};
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push({.id = "node.1", .label = "Node 1"});
    roots->push({.id = "node.2", .label = "Node 2"});
    view->bindData(roots);

    std::string selected;
    std::string menuId;
    glm::vec2   menuAt{0.0f, 0.0f};
    view->_onSelectionChanged = [&](const std::string& id) { selected = id; };
    view->setOnContextMenu([&](const std::string& id, const glm::vec2& point)
    {
        menuId = id;
        menuAt = point;
    });

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), view, viewSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Right), pointAt(80.0f, 32.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(selected, "node.1");
    EXPECT_EQ(menuId, "node.1");
    EXPECT_EQ(menuAt, glm::vec2(80.0f, 32.0f));
}

TEST(ToolControlsTest, TreeViewRightClickEmptySpaceFiresContextMenuWithEmptyId)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       view = std::make_shared<UITreeView>("Tree");
    FCanvasSlotArgs viewSlot;
    viewSlot.offset    = {20.0f, 20.0f};
    viewSlot.fixedSize = {220.0f, 96.0f};
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push({.id = "node.1", .label = "Node 1"});
    view->bindData(roots);

    std::string menuId = "unset";
    view->setOnContextMenu([&](const std::string& id, const glm::vec2&) { menuId = id; });

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), view, viewSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Right), pointAt(80.0f, 90.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(menuId, "");
}

TEST(ToolControlsTest, TreeViewVirtualizesPaintInsideScrollViewport)
{
    WidgetTree tree({.width = 200, .height = 120});
    auto viewport = std::make_shared<UIScrollViewport>("Scroll");
    auto view     = std::make_shared<UITreeView>("Tree");
    view->_rowHeight = 20.0f;

    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    for (int i = 0; i < 50; ++i) {
        roots->push(UITreeView::FNode{
            .id    = std::to_string(i),
            .label = "Node " + std::to_string(i),
        });
    }
    view->bindData(roots);

    FCanvasSlotArgs viewportSlot;
    viewportSlot.fixedSize = {200.0f, 100.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), viewport, viewportSlot);
    tree.attach(*viewport, view);
    tree.layout();
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(view->getVisibleRowCount(), 50);
    EXPECT_LT(view->getPaintedRowCount(), 50u);
    EXPECT_GE(view->getPaintedRowCount(), 5u);
}

// === Text field ===

TEST(ToolControlsTest, TextFieldTypedTextAppendsAndFiresChanged)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       field = std::make_shared<UITextField>("Name");
    FCanvasSlotArgs fieldSlot;
    fieldSlot.fixedSize = {200.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, fieldSlot);
    tree.layout();

    std::vector<std::string> changes;
    field->_onTextChanged = [&](const std::string& text) { changes.push_back(text); };
    tree.setFocus(field.get());

    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("YA"), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent(" Workbench"), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "YA Workbench");
    EXPECT_EQ(changes, std::vector<std::string>({"YA", "YA Workbench"}));
}

TEST(ToolControlsTest, TextFieldBackspaceAndCursorNavigation)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       field = std::make_shared<UITextField>("Name");
    FCanvasSlotArgs fieldSlot;
    fieldSlot.fixedSize = {200.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, fieldSlot);
    tree.layout();
    tree.setFocus(field.get());

    const auto at = pointAt(0.0f, 0.0f);
    // Build the buffer through typing so the caret follows the input.
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("abcd"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "abcd");

    // Cursor starts at the end: backspace removes the last character.
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Backspace), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "abc");

    // Left moves one code point; backspace removes the character before the
    // caret, not the one after.
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Left), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Backspace), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "ac");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Home), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Right), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Backspace), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "c");

    // Insertion happens at the caret.
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Home), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("X"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->_text, "Xc");
}

TEST(ToolControlsTest, TextFieldEnterAndFocusLossCommit)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       field = std::make_shared<UITextField>("Name");
    FCanvasSlotArgs fieldSlot;
    fieldSlot.fixedSize = {200.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, fieldSlot);
    tree.layout();

    std::vector<std::string> commits;
    field->_onCommit = [&](const std::string& text) { commits.push_back(text); };
    tree.setFocus(field.get());
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("v1"), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);

    // Enter commits the current buffer and keeps focus.
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(commits, std::vector<std::string>({"v1"}));

    // Focus loss commits too.
    tree.setFocus(nullptr);
    EXPECT_EQ(commits, std::vector<std::string>({"v1", "v1"}));
}

TEST(ToolControlsTest, TextFieldPressRequestsFocusAndPlacesCaret)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       field = std::make_shared<UITextField>("Name");
    FCanvasSlotArgs fieldSlot;
    fieldSlot.fixedSize = {200.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, fieldSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(150.0f, 14.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), field.get());
    // No font atlas in the closure test: the caret falls back to the end.
    EXPECT_EQ(field->_text.size(), 0u);
}

TEST(ToolControlsTest, TextFieldDoesNotConsumeForeignKeys)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       field = std::make_shared<UITextField>("Name");
    FCanvasSlotArgs fieldSlot;
    fieldSlot.fixedSize = {200.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, fieldSlot);
    tree.layout();
    tree.setFocus(field.get());

    // A-Z keys are not text-field commands: they bubble as NotHandled so the
    // app layer can observe/route them.
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_A), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::NotHandled);
}

TEST(ToolControlsTest, TextFieldShowsIBeamCursor)
{
    UITextField field("Name");
    EXPECT_EQ(field.getCursor(), ECursorType::IBeam);
}

TEST(ToolControlsTest, TextFieldSelectionShiftArrowsCopyCutPaste)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       field = std::make_shared<UITextField>("Name");
    FCanvasSlotArgs fieldSlot;
    fieldSlot.fixedSize = {200.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), field, fieldSlot);
    tree.layout();
    tree.setFocus(field.get());

    const auto at = pointAt(0.0f, 0.0f);
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("hello"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(field->hasSelection());

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Left, EKeyMod::Shift), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Left, EKeyMod::Shift), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(field->hasSelection());
    EXPECT_EQ(field->getText(), "hello");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_C, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getClipboardText(), "lo");
    EXPECT_EQ(field->getText(), "hello");

    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("X"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->getText(), "helX");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_A, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(field->hasSelection());
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_X, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->getText(), "");
    EXPECT_EQ(tree.getClipboardText(), "helX");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_V, primaryMod()), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(field->getText(), "helX");
}

TEST(ToolControlsTest, DragFloatEditReusesTextSelection)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       drag = std::make_shared<UIDragFloat>("Value");
    drag->setValue(3.50f);
    FCanvasSlotArgs slot;
    slot.fixedSize = {160.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), drag, slot);
    tree.layout();

    const auto at = pointAt(80.0f, 12.0f);
    MouseButtonPressedEvent first(EMouse::Left);
    first.setTimestampMs(1000);
    MouseButtonPressedEvent second(EMouse::Left);
    second.setTimestampMs(1100);
    EXPECT_EQ(tree.dispatchEvent(first, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(second, at), EWidgetRouteResult::HandledExclusive);

    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("9"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_FLOAT_EQ(drag->_value, 9.0f);
    EXPECT_EQ(drag->getCursor(), ECursorType::Arrow);
}

TEST(ToolControlsTest, SpinBoxEditReusesTextSelection)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       spin = std::make_shared<UISpinBox>("Count");
    spin->setValue(8.0f);
    FCanvasSlotArgs slot;
    slot.fixedSize = {180.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), spin, slot);
    tree.layout();

    const auto at = pointAt(90.0f, 12.0f);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), at),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(spin->getCursor(), ECursorType::IBeam);

    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("2"), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter), at), EWidgetRouteResult::HandledExclusive);
    EXPECT_FLOAT_EQ(spin->_value, 2.0f);
}

TEST(ToolControlsTest, ColorEditSwatchOpensSvHuePicker)
{
    WidgetTree tree({.width = 400, .height = 400});
    auto       edit = std::make_shared<UIColorEdit>("Tint");
    edit->setColor({1.0f, 1.0f, 1.0f, 1.0f});
    FCanvasSlotArgs slot;
    slot.fixedSize = {180.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), edit, slot);
    tree.layout();

    EXPECT_FALSE(edit->isPickerOpen());
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(12.0f, 14.0f)),
              EWidgetRouteResult::HandledExclusive);
    tree.layout();
    EXPECT_TRUE(edit->isPickerOpen());

    const glm::vec4 before = edit->_color;
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(140.0f, 50.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(140.0f, 50.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_NE(edit->_color, before);
    EXPECT_TRUE(edit->isPickerOpen());

    // Hex row is below SV (160) + pads + hue bar; commit replaces the live color.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(80.0f, 237.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(KeyTypedEvent("#FF0000FF"), pointAt(80.0f, 237.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter), pointAt(80.0f, 237.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_NEAR(edit->_color.r, 1.0f, 1e-4f);
    EXPECT_NEAR(edit->_color.g, 0.0f, 1e-4f);
    EXPECT_NEAR(edit->_color.b, 0.0f, 1e-4f);
    EXPECT_TRUE(edit->isPickerOpen());
}

// === Popup menu ===

TEST(ToolControlsTest, MenuSizesPanelFromItemLabels)
{
    const auto font = registerMenuFont(13.0f, 8.0f);

    WidgetTree tree({.width = 400, .height = 300});
    auto       menu = UIMenu::create({
        {"New Document", [] {}},
        {"Save", [] {}},
    });
    menu->openAt(tree, {10.0f, 20.0f});
    tree.layout();

    // maxLabelWidth = 12 glyphs x 8px = 96. Rows are label + 20 (10px each
    // side); the panel wraps the rows with _panelPadding (4) on both sides.
    const auto items = menu->menuItems();
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(menu->getChildren().size(), 1u);
    const auto* slot = dynamic_cast<const UICanvasSlot*>(menu->getSlotForChild(*menu->getChildren()[0]));
    ASSERT_NE(slot, nullptr);
    const Rect2D& panelRect = menu->getChildren()[0]->_layoutRect;
    EXPECT_EQ(slot->getOffset(), glm::vec2(10.0f, 20.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(96.0f + 20.0f + 8.0f, 2.0f * 26.0f + 8.0f));
    EXPECT_FLOAT_EQ(panelRect.pos.x, 10.0f);
    EXPECT_FLOAT_EQ(panelRect.pos.y, 20.0f);
    EXPECT_FLOAT_EQ(panelRect.extent.x, 96.0f + 20.0f + 8.0f);
    EXPECT_FLOAT_EQ(panelRect.extent.y, 2.0f * 26.0f + 8.0f);

    // First row: full panel width minus the padding, packed from the top.
    const Rect2D& row = items[0]->_layoutRect;
    EXPECT_FLOAT_EQ(row.pos.x, 14.0f);
    EXPECT_FLOAT_EQ(row.pos.y, 24.0f);
    EXPECT_FLOAT_EQ(row.extent.x, 96.0f + 20.0f);
    EXPECT_FLOAT_EQ(row.extent.y, 26.0f);
    const UIElement* list = items[0]->getParent();
    ASSERT_NE(list, nullptr);
    const auto* rowSlot = dynamic_cast<const UIBoxSlot*>(list->getSlotForChild(*items[0]));
    ASSERT_NE(rowSlot, nullptr);
    EXPECT_EQ(rowSlot->getPreferredSize(), glm::vec2(96.0f + 20.0f, 26.0f));
    // The label fits inside the row's 10px side padding.
    EXPECT_GE(row.extent.x - 20.0f, font->measureText("New Document"));
    // Second row packs directly below (no spacing between menu rows).
    EXPECT_FLOAT_EQ(items[1]->_layoutRect.pos.y, 50.0f);
}

TEST(ToolControlsTest, MenuBarHoverSwitchesOpenMenu)
{
    registerMenuFont(13.0f, 8.0f);

    WidgetTree tree({.width = 800, .height = 600});
    auto       bar = std::make_shared<UIMenuBar>("Bar");
    FCanvasSlotArgs barSlot;
    barSlot.anchorMin = {0.0f, 0.0f};
    barSlot.anchorMax = {1.0f, 0.0f};
    barSlot.fixedSize = {0.0f, 30.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bar, barSlot);

    bar->addItem("File", [] { return UIMenu::create({{"New Document", [] {}}, {"Save", [] {}}}); });
    bar->addItem("Edit", [] { return UIMenu::create({{"Undo", [] {}}, {"Redo", [] {}}}); });
    tree.layout();

    // Synthetic font: "File" = 32px + 20 -> 52px wide at x 4..56; "Edit"
    // starts at x 58. Click File: its menu opens below the bar.
    ASSERT_EQ(tree.pickAt({30.0f, 15.0f}), bar->getChildren().front().get());
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(30.0f, 15.0f)),
              EWidgetRouteResult::HandledExclusive);
    // The bar item has no press session: releases are not consumed. The
    // menu opened by the press stays open.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(30.0f, 15.0f)),
              EWidgetRouteResult::NotHandled);
    ASSERT_NE(bar->getOpenMenu(), nullptr);
    EXPECT_EQ(bar->getOpenMenu()->menuItems().front()->_label, "New Document");
    // The host lays out every frame; tests must too, or the freshly attached
    // overlay keeps an empty rect and never receives hits.
    tree.layout();

    // Hover over Edit without clicking: the open menu switches to Edit's.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(84.0f, 15.0f), pointAt(84.0f, 15.0f)),
              EWidgetRouteResult::HandledExclusive);
    // The move routes through the hover-transparent shield to the Edit item
    // (HitTest policy, not Popup — the shield only swallows presses). The
    // hover-switch closes the old menu only AFTER routing, so no hit target
    // is ever dereferenced after destruction.
    EXPECT_EQ(tree.getLastRouteTrace().policy, EWidgetRoutePolicy::HitTest);
    ASSERT_NE(bar->getOpenMenu(), nullptr);
    EXPECT_EQ(bar->getOpenMenu()->menuItems().front()->_label, "Undo");
    {
        const nlohmann::json dump = dumpWidgetTree(tree);
        const auto* fileItem = findWidgetNode(dump, "MenuBar_File");
        const auto* editItem = findWidgetNode(dump, "MenuBar_Edit");
        ASSERT_NE(fileItem, nullptr);
        ASSERT_NE(editItem, nullptr);
        EXPECT_FALSE((*fileItem)["hovered"]);
        EXPECT_TRUE((*editItem)["hovered"]);
    }

    // Hovering back over File switches back (classic menu-bar hover: any
    // hovered entry owns the open menu), and the old highlight must retire.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(30.0f, 15.0f), pointAt(30.0f, 15.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_NE(bar->getOpenMenu(), nullptr);
    EXPECT_EQ(bar->getOpenMenu()->menuItems().front()->_label, "New Document");
    {
        const nlohmann::json dump = dumpWidgetTree(tree);
        const auto* fileItem = findWidgetNode(dump, "MenuBar_File");
        const auto* editItem = findWidgetNode(dump, "MenuBar_Edit");
        ASSERT_NE(fileItem, nullptr);
        ASSERT_NE(editItem, nullptr);
        EXPECT_TRUE((*fileItem)["hovered"]);
        EXPECT_FALSE((*editItem)["hovered"]);
    }
    tree.layout();

    // A press anywhere outside the menu dismisses it (popup shield).
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(300.0f, 300.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getLastRouteTrace().policy, EWidgetRoutePolicy::Popup);
    EXPECT_EQ(bar->getOpenMenu(), nullptr);
}

TEST(ToolControlsTest, MenuBarPaintsBottomSeparator)
{
    registerMenuFont(13.0f, 8.0f);

    WidgetTree tree({.width = 320, .height = 120});
    auto       bar = std::make_shared<UIMenuBar>("Bar");
    FCanvasSlotArgs barSlot;
    barSlot.offset = {12.0f, 8.0f};
    barSlot.fixedSize = {100.0f, 26.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bar, barSlot);

    auto* item = bar->addItem("File", nullptr);
    if (auto* slot = dynamic_cast<UIBoxSlot*>(bar->getSlotForChild(*item))) {
        slot->setPreferredSize({52.0f, 26.0f});
    }

    auto theme = std::make_shared<UITheme>();
    auto style = FMenuBarItemStyle{};
    style.separatorColor = {0.91f, 0.21f, 0.37f, 1.0f};
    theme->define<FMenuBarItemStyle>("menubar", style);
    tree.setTheme(theme.get());

    tree.layout();
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});

    bool bFoundSeparator = false;
    for (const auto& draw : snap.items) {
        if (draw.kind != UIFrameDrawItem::EKind::Line) {
            continue;
        }
        if (std::abs(draw.lineFrom.x - 16.0f) > 0.01f ||
            std::abs(draw.lineTo.x - 68.0f) > 0.01f ||
            std::abs(draw.lineFrom.y - 29.5f) > 0.01f ||
            std::abs(draw.lineTo.y - 29.5f) > 0.01f) {
            continue;
        }
        EXPECT_EQ(draw.color, style.separatorColor);
        bFoundSeparator = true;
        break;
    }
    EXPECT_TRUE(bFoundSeparator);
}

TEST(ToolControlsTest, MenuSupportsIconsAndHoverOpenedSubmenus)
{
    registerMenuFont(13.0f, 8.0f);

    bool bLeafActivated = false;
    WidgetTree tree({.width = 480, .height = 320});
    auto       menu = UIMenu::create({
        UIMenu::FItem{
            .label = "Recent",
            .action = nullptr,
            .icon = "~",
            .submenuFactory = [&bLeafActivated] {
                return UIMenu::create({
                    UIMenu::FItem{.label = "Nested", .action = [&bLeafActivated]() { bLeafActivated = true; }},
                });
            },
        },
        UIMenu::FItem{.label = "Save", .action = [] {}},
    });
    menu->openAt(tree, {10.0f, 20.0f});
    tree.layout();

    const auto items = menu->menuItems();
    ASSERT_EQ(items.size(), 2u);
    const Rect2D& panelRect = menu->getChildren()[0]->_layoutRect;
    EXPECT_FLOAT_EQ(panelRect.extent.x, 48.0f + 20.0f + 22.0f + 18.0f + 8.0f);
    EXPECT_FLOAT_EQ(items[0]->_layoutRect.extent.x, 48.0f + 20.0f + 22.0f + 18.0f);

    const UIFrameSnapshot beforeHover = tree.buildSnapshot(UIFrameBuildContext{});
    bool bFoundIconText = false;
    bool bFoundArrowText = false;
    for (const auto& draw : beforeHover.items) {
        if (draw.kind != UIFrameDrawItem::EKind::Text) {
            continue;
        }
        bFoundIconText = bFoundIconText || draw.text == "~";
        bFoundArrowText = bFoundArrowText || draw.text == ">";
    }
    EXPECT_TRUE(bFoundIconText);
    EXPECT_TRUE(bFoundArrowText);

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(24.0f, 30.0f), pointAt(24.0f, 30.0f)),
              EWidgetRouteResult::HandledExclusive);
    tree.layout();

    UIElement* submenuHit = tree.pickAt({132.0f, 30.0f});
    ASSERT_NE(submenuHit, nullptr);
    auto* submenuItem = dynamic_cast<UIMenuItem*>(submenuHit);
    ASSERT_NE(submenuItem, nullptr);
    EXPECT_EQ(submenuItem->_label, "Nested");

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(132.0f, 30.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(bLeafActivated);
    EXPECT_EQ(tree.pickAt({132.0f, 30.0f}), nullptr);
}

TEST(ToolControlsTest, MenuSeparatorUsesDedicatedApiAndPaintsRule)
{
    registerMenuFont(13.0f, 8.0f);

    WidgetTree tree({.width = 320, .height = 180});
    auto       menu = UIMenu::create({
        UIMenu::FItem{.label = "Open", .action = [] {}},
        UIMenu::FItem::separator(),
        UIMenu::FItem{.label = "Quit", .action = [] {}},
    });
    menu->openAt(tree, {10.0f, 20.0f});
    tree.layout();

    const auto items = menu->menuItems();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_TRUE(items[1]->_bSeparator);
    EXPECT_FLOAT_EQ(items[1]->_layoutRect.extent.y, UIMenu::kSeparatorHeight);

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    bool bFoundSeparatorLine = false;
    bool bRenderedLiteralDashes = false;
    for (const auto& draw : snap.items) {
        if (draw.kind == UIFrameDrawItem::EKind::Line) {
            bFoundSeparatorLine = true;
        }
        if (draw.kind == UIFrameDrawItem::EKind::Text && draw.text == "---") {
            bRenderedLiteralDashes = true;
        }
    }
    EXPECT_TRUE(bFoundSeparatorLine);
    EXPECT_FALSE(bRenderedLiteralDashes);
}

TEST(ToolControlsTest, MenuKeyboardNavigatesSubmenuAndSkipsSeparators)
{
    registerMenuFont(13.0f, 8.0f);

    WidgetTree tree({.width = 480, .height = 320});
    auto       menu = UIMenu::create({
        UIMenu::FItem::separator(),
        UIMenu::FItem{
            .label = "Recent",
            .submenuFactory = []() {
                return UIMenu::create({
                    UIMenu::FItem{.label = "Nested A", .action = [] {}},
                    UIMenu::FItem{.label = "Nested B", .action = [] {}},
                });
            },
        },
        UIMenu::FItem{.label = "Quit", .action = [] {}},
    });
    menu->openAt(tree, {10.0f, 20.0f});
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Down), pointAt(0.0f, 0.0f)), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(menu->getHighlightIndex(), 1);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Right), pointAt(0.0f, 0.0f)), EWidgetRouteResult::HandledExclusive);
    tree.layout();
    UIElement* nestedHit = tree.pickAt({132.0f, 40.0f});
    ASSERT_NE(nestedHit, nullptr);
    auto* nestedItem = dynamic_cast<UIMenuItem*>(nestedHit);
    ASSERT_NE(nestedItem, nullptr);
    EXPECT_EQ(nestedItem->_label, "Nested A");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Left), pointAt(0.0f, 0.0f)), EWidgetRouteResult::HandledExclusive);
    tree.layout();
    UIElement* afterCloseHit = tree.pickAt({132.0f, 40.0f});
    EXPECT_TRUE(afterCloseHit == nullptr || dynamic_cast<UIMenuItem*>(afterCloseHit) == nullptr ||
                dynamic_cast<UIMenuItem*>(afterCloseHit)->_label != "Nested A");

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Down), pointAt(0.0f, 0.0f)), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(menu->getHighlightIndex(), 2);
}

TEST(ToolControlsTest, MenuDisabledItemsDoNotActivateAndAreSkippedByKeyboard)
{
    registerMenuFont(13.0f, 8.0f);

    bool bDisabledActivated = false;
    bool bEnabledActivated  = false;
    WidgetTree tree({.width = 400, .height = 260});
    auto       menu = UIMenu::create({
        UIMenu::FItem{.label = "Disabled", .action = [&]() { bDisabledActivated = true; }, .bEnabled = false},
        UIMenu::FItem{.label = "Enabled", .action = [&]() { bEnabledActivated = true; }},
    });
    menu->openAt(tree, {10.0f, 20.0f});
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(24.0f, 30.0f), pointAt(24.0f, 30.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(24.0f, 30.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(bDisabledActivated);
    ASSERT_NE(menu->getTree(), nullptr);

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Down), pointAt(0.0f, 0.0f)), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(menu->getHighlightIndex(), 1);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter), pointAt(0.0f, 0.0f)), EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(bEnabledActivated);
}

TEST(ToolControlsTest, MenuReservesColumnsForCheckmarkIconAndShortcut)
{
    const auto font = registerMenuFont(13.0f, 8.0f);

    WidgetTree tree({.width = 480, .height = 320});
    auto       menu = UIMenu::create({
        UIMenu::FItem{
            .label = "Recent",
            .icon = "~",
            .shortcut = "Cmd+R",
            .bChecked = true,
            .submenuFactory = []() { return UIMenu::create({UIMenu::FItem{.label = "Nested", .action = [] {}}}); },
        },
        UIMenu::FItem{.label = "Save", .action = [] {}},
    });
    menu->openAt(tree, {10.0f, 20.0f});
    tree.layout();

    const auto items = menu->menuItems();
    ASSERT_EQ(items.size(), 2u);
    const float labelWidth = font->measureText("Recent");
    const float shortcutWidth = font->measureText("Cmd+R");
    const float expectedRowWidth = UIMenu::kItemHorizontalPadding * 2.0f +
                                   (UIMenu::kCheckmarkColumnWidth + UIMenu::kCheckmarkColumnGap) +
                                   (UIMenu::kIconColumnWidth + UIMenu::kIconColumnGap) +
                                   labelWidth +
                                   (UIMenu::kShortcutColumnGap + shortcutWidth) +
                                   (UIMenu::kSubmenuColumnGap + UIMenu::kSubmenuColumnWidth);
    EXPECT_FLOAT_EQ(items[0]->_layoutRect.extent.x, expectedRowWidth);

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    bool bFoundCheck = false;
    bool bFoundIcon = false;
    bool bFoundShortcut = false;
    for (const auto& draw : snap.items) {
        if (draw.kind != UIFrameDrawItem::EKind::Text) {
            continue;
        }
        bFoundCheck = bFoundCheck || draw.text == "v";
        bFoundIcon = bFoundIcon || draw.text == "~";
        bFoundShortcut = bFoundShortcut || draw.text == "Cmd+R";
    }
    EXPECT_TRUE(bFoundCheck);
    EXPECT_TRUE(bFoundIcon);
    EXPECT_TRUE(bFoundShortcut);
}

TEST(ToolControlsTest, SelectableRowHoverRepaintsWithHoveredColor)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       row = std::make_shared<UISelectableRow>("Row");
    FCanvasSlotArgs rowSlot;
    rowSlot.fixedSize = {240.0f, 22.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), row, rowSlot);
    tree.layout();
    tree.buildSnapshot(UIFrameBuildContext{}); // cold start: normal color (alpha 0)

    // Hover the row: the MouseMoved route flips the row's hover flag.
    tree.dispatchEvent(MouseMoveEvent(120.0f, 11.0f), pointAt(120.0f, 11.0f));

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, FSelectableRowStyle{}.hoveredFill.tintColor);

    // Leave: the tree hover lifecycle clears the flag, back to transparent.
    tree.dispatchEvent(MouseMoveEvent(300.0f, 200.0f), pointAt(300.0f, 200.0f));
    const UIFrameSnapshot snapAfterLeave = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_TRUE(snapAfterLeave.items.empty());
}

TEST(ToolControlsTest, SelectableRowWithLabelChildHoverStillHighlightsRow)
{
    // Reproduce the workbench row shape: a text label child sits on top of the
    // row. The hover owner must still resolve to the row (not the label), and
    // the row must re-paint with its hovered color.
    WidgetTree tree({.width = 400, .height = 300});
    auto       row = std::make_shared<UISelectableRow>("Row");
    FCanvasSlotArgs rowSlot;
    rowSlot.fixedSize = {240.0f, 22.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), row, rowSlot);

    auto label = std::make_shared<UIText>("Label");
    label->_fontSize = 13;
    label->setText("Item 1");
    label->setColor({0.9f, 0.9f, 0.9f, 1.0f});
    row->addDetachedChild(label, [](UIElement&, UISlot& edge) {
        if (auto* slot = edge.as<UIOverlaySlot>()) {
            slot->setPreferredSize({240.0f, 22.0f});
        }
    });
    tree.layout();
    tree.buildSnapshot(UIFrameBuildContext{});

    tree.dispatchEvent(MouseMoveEvent(120.0f, 11.0f), pointAt(120.0f, 11.0f));
    EXPECT_EQ(tree.getHovered(), row.get());

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    // The row sprite must carry the hovered color (the label may add a text
    // item after it when a font is available).
    ASSERT_GE(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, FSelectableRowStyle{}.hoveredFill.tintColor);
}

TEST(ToolControlsTest, SelectableRowHoverUsesThemeFill)
{
    auto theme = std::make_shared<UITheme>();
    FSelectableRowStyle style;
    style.hoveredFill = FBrush::solid({1.0f, 0.2f, 0.1f, 1.0f});
    theme->define<FSelectableRowStyle>("selectable", style);

    WidgetTree tree({.width = 400, .height = 300});
    tree.setTheme(theme.get());
    auto row = std::make_shared<UISelectableRow>("Row");
    FCanvasSlotArgs rowSlot;
    rowSlot.fixedSize = {240.0f, 22.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), row, rowSlot);
    tree.layout();

    tree.dispatchEvent(MouseMoveEvent(120.0f, 11.0f), pointAt(120.0f, 11.0f));
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, glm::vec4(1.0f, 0.2f, 0.1f, 1.0f));
}


} // namespace ya

#include "Core/Event.h"
#include "UI/UIBase.h"
#include "UI/UISceneRenderer.h"
#include "UI/Scene/Node2D.h"
#include "Scene/Scene.h"

#include <gtest/gtest.h>

#include <cmath>

namespace ya
{

namespace
{

Rect2D makeRect(float x, float y, float w, float h)
{
    return Rect2D{.pos = {x, y}, .extent = {w, h}};
}

bool rectEq(const Rect2D& a, const Rect2D& b, float eps = 0.001f)
{
    return std::fabs(a.pos.x - b.pos.x) < eps &&
           std::fabs(a.pos.y - b.pos.y) < eps &&
           std::fabs(a.extent.x - b.extent.x) < eps &&
           std::fabs(a.extent.y - b.extent.y) < eps;
}

/// Mirror UISceneRenderer::render's layout pass without a Render2D session.
void layoutRoots(Node* node, const Rect2D& canvasRect)
{
    for (Node* child : node->getChildren()) {
        if (child->is2D()) {
            static_cast<Node2D*>(child)->layout(canvasRect);
        }
        else {
            layoutRoots(child, canvasRect);
        }
    }
}

UIAppCtx makeUICtx(float mouseX, float mouseY)
{
    UIAppCtx ctx;
    ctx.lastMousePos = {mouseX, mouseY};
    ctx.bInViewport  = true;
    ctx.viewportRect = {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}};
    return ctx;
}

} // namespace

// ============================================================================
// Layout pass: anchors, containers, text-driven sizes, zOrder paint order.
// ============================================================================

TEST(Node2DLayoutTest, DefaultAnchorsMatchAbsoluteLayout)
{
    Node2D node;
    node._position = {10.0f, 20.0f};
    node._size     = {100.0f, 50.0f};
    node.layout(makeRect(0.0f, 0.0f, 800.0f, 600.0f));
    EXPECT_TRUE(rectEq(node._layoutRect, makeRect(10.0f, 20.0f, 100.0f, 50.0f)));
}

TEST(Node2DLayoutTest, AnchorPointOffsetsFromParentEdge)
{
    Node2D parent;
    parent._size = {800.0f, 600.0f};
    parent.layout(makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    Node2D child;
    child._anchorMin = {1.0f, 0.0f};
    child._anchorMax = {1.0f, 0.0f};
    child._position  = {-120.0f, 20.0f};
    child._size      = {100.0f, 40.0f};
    child.layout(parent._layoutRect);

    // Anchored to the parent's right edge, 20px away from it.
    EXPECT_TRUE(rectEq(child._layoutRect, makeRect(800.0f - 120.0f, 20.0f, 100.0f, 40.0f)));
}

TEST(Node2DLayoutTest, StretchAnchorsFillParent)
{
    Node2D parent;
    parent._size = {800.0f, 600.0f};
    parent.layout(makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    Node2D child;
    child._anchorMin = {0.0f, 0.0f};
    child._anchorMax = {1.0f, 1.0f};
    child.layout(parent._layoutRect);

    EXPECT_TRUE(rectEq(child._layoutRect, makeRect(0.0f, 0.0f, 800.0f, 600.0f)));
}

TEST(Node2DLayoutTest, HBoxArrangesChildrenInPaintOrder)
{
    Scene scene("LayoutTest");
    auto* container = scene.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_padding   = 5.0f;
    container->_size      = {300.0f, 80.0f};

    auto* a = scene.createUINode<UIPanelNode>("A", container);
    a->_size = {100.0f, 30.0f};
    auto* b = scene.createUINode<UIPanelNode>("B", container);
    b->_size = {50.0f, 40.0f};

    container->layout(makeRect(0.0f, 0.0f, 300.0f, 80.0f));

    // Content rect (5,5,290,70); cross-axis stretches to the content height.
    EXPECT_TRUE(rectEq(a->_layoutRect, makeRect(5.0f, 5.0f, 100.0f, 70.0f)));
    EXPECT_TRUE(rectEq(b->_layoutRect, makeRect(115.0f, 5.0f, 50.0f, 70.0f)));
}

TEST(Node2DLayoutTest, VBoxArrangesChildrenTopToBottom)
{
    Scene scene("LayoutTest");
    auto* container = scene.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Vertical;
    container->_spacing   = 6.0f;
    container->_size      = {200.0f, 200.0f};

    auto* a = scene.createUINode<UIPanelNode>("A", container);
    a->_size = {30.0f, 40.0f};
    auto* b = scene.createUINode<UIPanelNode>("B", container);
    b->_size = {50.0f, 20.0f};

    container->layout(makeRect(0.0f, 0.0f, 200.0f, 200.0f));

    EXPECT_TRUE(rectEq(a->_layoutRect, makeRect(0.0f, 0.0f, 200.0f, 40.0f)));
    EXPECT_TRUE(rectEq(b->_layoutRect, makeRect(0.0f, 46.0f, 200.0f, 20.0f)));
}

TEST(Node2DLayoutTest, ContainerDesiredSizeAggregatesChildren)
{
    Scene scene("LayoutTest");
    auto* container = scene.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_padding   = 5.0f;

    auto* a = scene.createUINode<UIPanelNode>("A", container);
    a->_size = {100.0f, 30.0f};
    auto* b = scene.createUINode<UIPanelNode>("B", container);
    b->_size = {50.0f, 40.0f};

    const glm::vec2 desired = container->computeDesiredSize();
    EXPECT_NEAR(desired.x, 100.0f + 10.0f + 50.0f + 5.0f * 2.0f, 0.001f);
    EXPECT_NEAR(desired.y, 40.0f + 5.0f * 2.0f, 0.001f);
}

TEST(Node2DLayoutTest, SiblingZOrderDefinesPaintOrder)
{
    Scene scene("LayoutTest");
    auto* parent = scene.createUINode<UICanvasNode>("Canvas");
    auto* low    = scene.createUINode<UIPanelNode>("Low", parent);
    low->_zOrder = 5;
    auto* high = scene.createUINode<UIPanelNode>("High", parent);
    high->_zOrder = 1;
    auto* mid = scene.createUINode<UIPanelNode>("Mid", parent);
    mid->_zOrder = 3;

    const auto order = parent->getChildrenInPaintOrder();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], high); // zOrder 1 first
    EXPECT_EQ(order[1], mid);  // zOrder 3
    EXPECT_EQ(order[2], low);  // zOrder 5 last (drawn on top)
}

TEST(Node2DLayoutTest, CanvasFillsAssignedArea)
{
    Scene scene("LayoutTest");
    auto* canvas = scene.createUINode<UICanvasNode>("Canvas");
    canvas->_position = {10.0f, 10.0f};
    canvas->_size     = {100.0f, 50.0f};

    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));
    EXPECT_TRUE(rectEq(canvas->_layoutRect, makeRect(0.0f, 0.0f, 800.0f, 600.0f)));
}

TEST(Node2DLayoutTest, TextAutoSizeFallsBackToFixedSizeWithoutFont)
{
    UITextNode text;
    text._text     = "hello";
    text._bAutoSize = false;
    EXPECT_TRUE(rectEq(Rect2D{.pos = {0.0f, 0.0f}, .extent = text.computeDesiredSize()},
                       makeRect(0.0f, 0.0f, 100.0f, 50.0f)));

    // Auto-size without a loaded font must not crash; falls back to _size.
    text._bAutoSize = true;
    const glm::vec2 autoSize = text.computeDesiredSize();
    EXPECT_GT(autoSize.x, 0.0f);
    EXPECT_GT(autoSize.y, 0.0f);
}

// ============================================================================
// Picking / events via the shared topmost-first walker.
// ============================================================================

TEST(Node2DLayoutTest, PickNodeAtReturnsTopmostVisibleNode)
{
    Scene scene("PickTest");
    auto* back = scene.createUINode<UIButtonNode>("Back");
    back->_position = {10.0f, 10.0f};
    back->_size     = {100.0f, 50.0f};
    back->_zOrder   = 0;
    auto* front = scene.createUINode<UIButtonNode>("Front");
    front->_position = {10.0f, 10.0f};
    front->_size     = {100.0f, 50.0f};
    front->_zOrder   = 10;

    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    EXPECT_EQ(UISceneRenderer::pickNodeAt(scene.getRootNode(), {50.0f, 30.0f}), front);
    EXPECT_EQ(UISceneRenderer::pickNodeAt(scene.getRootNode(), {500.0f, 500.0f}), nullptr);
}

TEST(Node2DLayoutTest, MouseMovedClearsHoverWhenLeavingButton)
{
    Scene scene("HoverTest");
    auto* button = scene.createUINode<UIButtonNode>("Btn");
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(120.0f, 110.0f),
                                           makeUICtx(120.0f, 110.0f),
                                           scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bHovered);

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(10.0f, 10.0f),
                                           makeUICtx(10.0f, 10.0f),
                                           scene.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bHovered);
}

// ============================================================================
// Hit-filter routing (Godot mouse_filter semantics).
// ============================================================================

TEST(Node2DLayoutTest, PassHitRespondsButFallsThrough)
{
    Scene scene("FilterTest");
    auto* button = scene.createUINode<UIButtonNode>("Btn");
    button->_position  = {100.0f, 100.0f};
    button->_size      = {80.0f, 32.0f};
    button->_hitFilter = EUIHitFilter::Pass;

    int clickCount = 0;
    button->_onClick = [&] { ++clickCount; };
    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           scene.getRootNode()),
              EUIRouteResult::HandledPass);
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonReleasedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           scene.getRootNode()),
              EUIRouteResult::HandledPass);
    EXPECT_EQ(clickCount, 1);
}

TEST(Node2DLayoutTest, HitTestInvisibleSkipsSelfButChildrenStillHit)
{
    Scene scene("FilterTest");
    auto* panel = scene.createUINode<UIPanelNode>("Overlay");
    panel->_position   = {0.0f, 0.0f};
    panel->_size       = {400.0f, 300.0f};
    panel->_visibility = EUIVisibility::HitTestInvisible;

    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    // The HitTestInvisible overlay does not receive hits itself, and its
    // Stop child still does.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bPressed);

    // A click on the overlay itself (outside the child) is not consumed.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(300.0f, 250.0f),
                                           scene.getRootNode()),
              EUIRouteResult::NotHandled);
}

TEST(Node2DLayoutTest, PassOverlayDoesNotBlockStopChild)
{
    Scene scene("FilterTest");
    auto* panel = scene.createUINode<UIPanelNode>("Overlay");
    panel->_position = {0.0f, 0.0f};
    panel->_size     = {400.0f, 300.0f}; // Pass by default (panel)

    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    // Topmost-first: panel is visited first but a passive Pass node does not
    // respond, so the walk continues to the button (Stop, exclusive).
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bPressed);
}

TEST(Node2DLayoutTest, SelfHitTestInvisibleBlocksWholeSubtree)
{
    Scene scene("FilterTest");
    auto* panel = scene.createUINode<UIPanelNode>("Overlay");
    panel->_position   = {0.0f, 0.0f};
    panel->_size       = {400.0f, 300.0f};
    panel->_visibility = EUIVisibility::SelfHitTestInvisible;

    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    // Neither the overlay nor its Stop child receive hits.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           scene.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);
}

TEST(Node2DLayoutTest, HiddenKeepsLayoutSpace)
{
    Scene scene("LayoutTest");
    auto* container = scene.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_size      = {300.0f, 80.0f};

    auto* a = scene.createUINode<UIPanelNode>("A", container);
    a->_size = {100.0f, 30.0f};
    auto* b = scene.createUINode<UIPanelNode>("B", container);
    b->_size       = {50.0f, 40.0f};
    b->_visibility = EUIVisibility::Hidden;

    container->layout(makeRect(0.0f, 0.0f, 300.0f, 80.0f));

    // Hidden keeps its space (UMG semantics): B still occupies 50px.
    EXPECT_TRUE(rectEq(a->_layoutRect, makeRect(0.0f, 0.0f, 100.0f, 80.0f)));
    EXPECT_TRUE(rectEq(b->_layoutRect, makeRect(110.0f, 0.0f, 50.0f, 80.0f)));
}

TEST(Node2DLayoutTest, CollapsedTakesNoLayoutSpace)
{
    Scene scene("LayoutTest");
    auto* container = scene.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_size      = {300.0f, 80.0f};

    auto* a = scene.createUINode<UIPanelNode>("A", container);
    a->_size = {100.0f, 30.0f};
    auto* b = scene.createUINode<UIPanelNode>("B", container);
    b->_size       = {50.0f, 40.0f};
    b->_visibility = EUIVisibility::Collapsed;

    container->layout(makeRect(0.0f, 0.0f, 300.0f, 80.0f));

    // Collapsed is removed from layout: A stays at the start, no gap for B.
    EXPECT_TRUE(rectEq(a->_layoutRect, makeRect(0.0f, 0.0f, 100.0f, 80.0f)));

    // Desired size excludes the collapsed child.
    EXPECT_NEAR(container->computeDesiredSize().x, 100.0f, 0.001f);
}

// ============================================================================
// Effective state queries (isVisibleInTree / isHitTestableInTree).
// ============================================================================

TEST(Node2DLayoutTest, VisibleChildUnderHiddenParentIsNotEffective)
{
    Scene scene("VisibilityTest");
    auto* panel = scene.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::Hidden;
    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);

    // The child itself is Visible, but the hidden ancestor culls the subtree.
    EXPECT_TRUE(button->isVisibleForRender());
    EXPECT_FALSE(button->isVisibleInTree());
    EXPECT_FALSE(button->isHitTestableInTree());

    // The walker agrees: no hit can land in the subtree.
    layoutRoots(scene.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(50.0f, 50.0f),
                                           scene.getRootNode()),
              EUIRouteResult::NotHandled);
}

TEST(Node2DLayoutTest, VisibleChildUnderVisibleAncestorsIsEffective)
{
    Scene scene("VisibilityTest");
    auto* panel = scene.createUINode<UIPanelNode>("Panel");
    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);
    button->_position = {10.0f, 10.0f};
    button->_size     = {80.0f, 32.0f};

    EXPECT_TRUE(button->isVisibleInTree());
    EXPECT_TRUE(button->isHitTestableInTree());
}

TEST(Node2DLayoutTest, HitTestInvisibleAncestorKeepsChildrenEffective)
{
    Scene scene("VisibilityTest");
    auto* panel = scene.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::HitTestInvisible;
    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);
    button->_position = {10.0f, 10.0f};
    button->_size     = {80.0f, 32.0f};

    // The panel itself is not hittable, but the walker still descends into
    // its subtree, so the child remains effective.
    EXPECT_FALSE(panel->isHitTestableSelf());
    EXPECT_TRUE(panel->isHitTestableInTree());
    EXPECT_TRUE(button->isHitTestableInTree());
}

TEST(Node2DLayoutTest, SelfHitTestInvisibleAncestorCullsChildren)
{
    Scene scene("VisibilityTest");
    auto* panel = scene.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::SelfHitTestInvisible;
    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);

    // Still rendered, but the whole subtree is not hittable.
    EXPECT_TRUE(button->isVisibleInTree());
    EXPECT_FALSE(button->isHitTestableInTree());
}

TEST(Node2DLayoutTest, CollapsedAncestorCullsRenderAndHit)
{
    Scene scene("VisibilityTest");
    auto* panel = scene.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::Collapsed;
    auto* button = scene.createUINode<UIButtonNode>("Btn", panel);

    EXPECT_FALSE(button->isVisibleInTree());
    EXPECT_FALSE(button->isHitTestableInTree());
}

TEST(Node2DLayoutTest, Node3DAncestorsAreTransparentToVisibility)
{
    Scene scene("VisibilityTest");
    Node3D* holder = scene.createNode3D("Holder");
    auto*   button = scene.createUINode<UIButtonNode>("Btn", holder);

    // Node3D has no UI visibility concept: the chain stays effective.
    EXPECT_TRUE(button->isVisibleInTree());
    EXPECT_TRUE(button->isHitTestableInTree());
}

} // namespace ya

#include "Core/Event.h"
#include "GUI/Runtime/UIBase.h"
#include "GUI/Runtime/Scene/UISceneRenderer.h"
#include "GUI/Runtime/Scene/Node2D.h"

#include <gtest/gtest.h>

#include <memory>

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

/// Minimal UI tree root replacing the engine Scene container: GUI closure
/// tests build the tree directly on Node/Node2D and keep nodes alive via
/// shared ownership (the engine Scene owns UI nodes in _entityLessNodes).
struct TestUIRoot
{
    Node root{"SceneRoot", nullptr};

    template <typename T, typename... Args>
        requires(sizeof...(Args) < 2 || !std::is_convertible_v<std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>, Node*>)
    std::shared_ptr<T> createUINode(Args&&... args)
    {
        auto node = std::make_shared<T>(std::forward<Args>(args)...);
        root.addChild(node.get());
        return node;
    }

    template <typename T>
    std::shared_ptr<T> createUINode(const std::string& name, Node* parent)
    {
        auto node = std::make_shared<T>(name);
        parent->addChild(node.get());
        return node;
    }

    Node* getRootNode() { return &root; }
    void  addChild(Node* node) { root.addChild(node); }
};

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
    TestUIRoot root;
    auto container = root.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_padding   = 5.0f;
    container->_size      = {300.0f, 80.0f};

    auto a = root.createUINode<UIPanelNode>("A", container.get());
    a->_size = {100.0f, 30.0f};
    auto b = root.createUINode<UIPanelNode>("B", container.get());
    b->_size = {50.0f, 40.0f};

    container->layout(makeRect(0.0f, 0.0f, 300.0f, 80.0f));

    // Content rect (5,5,290,70); cross-axis stretches to the content height.
    EXPECT_TRUE(rectEq(a->_layoutRect, makeRect(5.0f, 5.0f, 100.0f, 70.0f)));
    EXPECT_TRUE(rectEq(b->_layoutRect, makeRect(115.0f, 5.0f, 50.0f, 70.0f)));
}

TEST(Node2DLayoutTest, VBoxArrangesChildrenTopToBottom)
{
    TestUIRoot root;
    auto container = root.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Vertical;
    container->_spacing   = 6.0f;
    container->_size      = {200.0f, 200.0f};

    auto a = root.createUINode<UIPanelNode>("A", container.get());
    a->_size = {30.0f, 40.0f};
    auto b = root.createUINode<UIPanelNode>("B", container.get());
    b->_size = {50.0f, 20.0f};

    container->layout(makeRect(0.0f, 0.0f, 200.0f, 200.0f));

    EXPECT_TRUE(rectEq(a->_layoutRect, makeRect(0.0f, 0.0f, 200.0f, 40.0f)));
    EXPECT_TRUE(rectEq(b->_layoutRect, makeRect(0.0f, 46.0f, 200.0f, 20.0f)));
}

TEST(Node2DLayoutTest, ContainerDesiredSizeAggregatesChildren)
{
    TestUIRoot root;
    auto container = root.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_padding   = 5.0f;

    auto a = root.createUINode<UIPanelNode>("A", container.get());
    a->_size = {100.0f, 30.0f};
    auto b = root.createUINode<UIPanelNode>("B", container.get());
    b->_size = {50.0f, 40.0f};

    const glm::vec2 desired = container->computeDesiredSize();
    EXPECT_NEAR(desired.x, 100.0f + 10.0f + 50.0f + 5.0f * 2.0f, 0.001f);
    EXPECT_NEAR(desired.y, 40.0f + 5.0f * 2.0f, 0.001f);
}

TEST(Node2DLayoutTest, SiblingZOrderDefinesPaintOrder)
{
    TestUIRoot root;
    auto parent = root.createUINode<UICanvasNode>("Canvas");
    auto low     = root.createUINode<UIPanelNode>("Low", parent.get());
    low->_zOrder = 5;
    auto high = root.createUINode<UIPanelNode>("High", parent.get());
    high->_zOrder = 1;
    auto mid = root.createUINode<UIPanelNode>("Mid", parent.get());
    mid->_zOrder = 3;

    const auto order = parent->getChildrenInPaintOrder();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], high.get()); // zOrder 1 first
    EXPECT_EQ(order[1], mid.get());  // zOrder 3
    EXPECT_EQ(order[2], low.get());  // zOrder 5 last (drawn on top)
}

TEST(Node2DLayoutTest, CanvasFillsAssignedArea)
{
    TestUIRoot root;
    auto canvas = root.createUINode<UICanvasNode>("Canvas");
    canvas->_position = {10.0f, 10.0f};
    canvas->_size     = {100.0f, 50.0f};

    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));
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
    TestUIRoot root;
    auto back = root.createUINode<UIButtonNode>("Back");
    back->_position = {10.0f, 10.0f};
    back->_size     = {100.0f, 50.0f};
    back->_zOrder   = 0;
    auto front = root.createUINode<UIButtonNode>("Front");
    front->_position = {10.0f, 10.0f};
    front->_size     = {100.0f, 50.0f};
    front->_zOrder   = 10;

    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    EXPECT_EQ(UISceneRenderer::pickNodeAt(root.getRootNode(), {50.0f, 30.0f}), front.get());
    EXPECT_EQ(UISceneRenderer::pickNodeAt(root.getRootNode(), {500.0f, 500.0f}), nullptr);
}

TEST(Node2DLayoutTest, MouseMovedClearsHoverWhenLeavingButton)
{
    TestUIRoot root;
    auto button = root.createUINode<UIButtonNode>("Btn");
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(120.0f, 110.0f),
                                           makeUICtx(120.0f, 110.0f),
                                           root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bHovered);

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(10.0f, 10.0f),
                                           makeUICtx(10.0f, 10.0f),
                                           root.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bHovered);
}

// ============================================================================
// Hit-filter routing (Godot mouse_filter semantics).
// ============================================================================

TEST(Node2DLayoutTest, PassHitRespondsButFallsThrough)
{
    TestUIRoot root;
    auto button = root.createUINode<UIButtonNode>("Btn");
    button->_position  = {100.0f, 100.0f};
    button->_size      = {80.0f, 32.0f};
    button->_hitFilter = EUIHitFilter::Pass;

    int clickCount = 0;
    button->_onClick = [&] { ++clickCount; };
    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           root.getRootNode()),
              EUIRouteResult::HandledPass);
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonReleasedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           root.getRootNode()),
              EUIRouteResult::HandledPass);
    EXPECT_EQ(clickCount, 1);
}

TEST(Node2DLayoutTest, HitTestInvisibleSkipsSelfButChildrenStillHit)
{
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Overlay");
    panel->_position   = {0.0f, 0.0f};
    panel->_size       = {400.0f, 300.0f};
    panel->_visibility = EUIVisibility::HitTestInvisible;

    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    // The HitTestInvisible overlay does not receive hits itself, and its
    // Stop child still does.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bPressed);

    // A click on the overlay itself (outside the child) is not consumed.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(300.0f, 250.0f),
                                           root.getRootNode()),
              EUIRouteResult::NotHandled);
}

TEST(Node2DLayoutTest, PassOverlayDoesNotBlockStopChild)
{
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Overlay");
    panel->_position = {0.0f, 0.0f};
    panel->_size     = {400.0f, 300.0f}; // Pass by default (panel)

    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    // Topmost-first: panel is visited first but a passive Pass node does not
    // respond, so the walk continues to the button (Stop, exclusive).
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bPressed);
}

TEST(Node2DLayoutTest, SelfHitTestInvisibleBlocksWholeSubtree)
{
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Overlay");
    panel->_position   = {0.0f, 0.0f};
    panel->_size       = {400.0f, 300.0f};
    panel->_visibility = EUIVisibility::SelfHitTestInvisible;

    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));

    // Neither the overlay nor its Stop child receive hits.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(120.0f, 110.0f),
                                           root.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);
}

TEST(Node2DLayoutTest, HiddenKeepsLayoutSpace)
{
    TestUIRoot root;
    auto container = root.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_size      = {300.0f, 80.0f};

    auto a = root.createUINode<UIPanelNode>("A", container.get());
    a->_size = {100.0f, 30.0f};
    auto b = root.createUINode<UIPanelNode>("B", container.get());
    b->_size       = {50.0f, 40.0f};
    b->_visibility = EUIVisibility::Hidden;

    container->layout(makeRect(0.0f, 0.0f, 300.0f, 80.0f));

    // Hidden keeps its space (UMG semantics): B still occupies 50px.
    EXPECT_TRUE(rectEq(a->_layoutRect, makeRect(0.0f, 0.0f, 100.0f, 80.0f)));
    EXPECT_TRUE(rectEq(b->_layoutRect, makeRect(110.0f, 0.0f, 50.0f, 80.0f)));
}

TEST(Node2DLayoutTest, CollapsedTakesNoLayoutSpace)
{
    TestUIRoot root;
    auto container = root.createUINode<UIContainerNode>("Box");
    container->_direction = EUIBoxLayout::Horizontal;
    container->_spacing   = 10.0f;
    container->_size      = {300.0f, 80.0f};

    auto a = root.createUINode<UIPanelNode>("A", container.get());
    a->_size = {100.0f, 30.0f};
    auto b = root.createUINode<UIPanelNode>("B", container.get());
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
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::Hidden;
    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());

    // The child itself is Visible, but the hidden ancestor culls the subtree.
    EXPECT_TRUE(button->isVisibleForRender());
    EXPECT_FALSE(button->isVisibleInTree());
    EXPECT_FALSE(button->isHitTestableInTree());

    // The walker agrees: no hit can land in the subtree.
    layoutRoots(root.getRootNode(), makeRect(0.0f, 0.0f, 800.0f, 600.0f));
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0),
                                           makeUICtx(50.0f, 50.0f),
                                           root.getRootNode()),
              EUIRouteResult::NotHandled);
}

TEST(Node2DLayoutTest, VisibleChildUnderVisibleAncestorsIsEffective)
{
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Panel");
    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());
    button->_position = {10.0f, 10.0f};
    button->_size     = {80.0f, 32.0f};

    EXPECT_TRUE(button->isVisibleInTree());
    EXPECT_TRUE(button->isHitTestableInTree());
}

TEST(Node2DLayoutTest, HitTestInvisibleAncestorKeepsChildrenEffective)
{
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::HitTestInvisible;
    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());
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
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::SelfHitTestInvisible;
    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());

    // Still rendered, but the whole subtree is not hittable.
    EXPECT_TRUE(button->isVisibleInTree());
    EXPECT_FALSE(button->isHitTestableInTree());
}

TEST(Node2DLayoutTest, CollapsedAncestorCullsRenderAndHit)
{
    TestUIRoot root;
    auto panel = root.createUINode<UIPanelNode>("Panel");
    panel->_visibility = EUIVisibility::Collapsed;
    auto button = root.createUINode<UIButtonNode>("Btn", panel.get());

    EXPECT_FALSE(button->isVisibleInTree());
    EXPECT_FALSE(button->isHitTestableInTree());
}

TEST(Node2DLayoutTest, NodeAncestorsAreTransparentToVisibility)
{
    TestUIRoot root;
    auto holder = std::make_shared<Node>("Holder", nullptr);
    root.addChild(holder.get());
    auto button = root.createUINode<UIButtonNode>("Btn", holder.get());

    // Non-UI ancestors (Node3D in the engine scene) have no UI visibility: the chain stays effective.
    EXPECT_TRUE(button->isVisibleInTree());
    EXPECT_TRUE(button->isHitTestableInTree());
}

} // namespace ya

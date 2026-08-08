#include "Foundation/Core/Event.h"
#include "Framework/GUI/Runtime/UIBase.h"
#include "Framework/GUI/Runtime/Scene/UISceneRenderer.h"
#include "Framework/GUI/Runtime/Scene/Node2D.h"

#include <gtest/gtest.h>

#include <memory>

namespace ya
{

namespace
{

UIAppCtx makeUICtx(float mouseX, float mouseY)
{
    UIAppCtx ctx;
    ctx.lastMousePos  = {mouseX, mouseY};
    ctx.bInViewport   = true;
    ctx.viewportRect  = {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}};
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

} // namespace

TEST(UISceneRendererTest, ButtonClickInsideConsumesAndFires)
{
    TestUIRoot root;
    auto canvas = root.createUINode<UICanvasNode>("Canvas");
    ASSERT_NE(canvas, nullptr);

    auto button = root.createUINode<UIButtonNode>("OK", canvas.get());
    ASSERT_NE(button, nullptr);
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};

    int clickCount = 0;
    button->_onClick = [&] { ++clickCount; };
    layoutRoots(root.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    // Move outside: not consumed, no hover.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(10.0f, 10.0f), makeUICtx(10.0f, 10.0f), root.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bHovered);

    // Move onto the button: consumed + hovered.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(120.0f, 110.0f), makeUICtx(120.0f, 110.0f), root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bHovered);

    // Press inside: consumed + pressed.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(120.0f, 110.0f), root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bPressed);

    // Release inside: consumed + click fired.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonReleasedEvent(0), makeUICtx(120.0f, 110.0f), root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_FALSE(button->_bPressed);
    EXPECT_EQ(clickCount, 1);
}

TEST(UISceneRendererTest, PressOutsideDoesNotArmButton)
{
    TestUIRoot root;
    auto button = root.createUINode<UIButtonNode>("OK");
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};

    int clickCount = 0;
    button->_onClick = [&] { ++clickCount; };
    layoutRoots(root.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    // Press outside: not consumed and the button is not armed.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(10.0f, 10.0f), root.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);

    // Move onto the button then release: no click (was never pressed).
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(120.0f, 110.0f), makeUICtx(120.0f, 110.0f), root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonReleasedEvent(0), makeUICtx(120.0f, 110.0f), root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_EQ(clickCount, 0);
}

TEST(UISceneRendererTest, InvisibleNodeDoesNotConsume)
{
    TestUIRoot root;
    auto button = root.createUINode<UIButtonNode>("OK");
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    button->_visibility = EUIVisibility::Hidden;
    layoutRoots(root.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(120.0f, 110.0f), root.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);
}

TEST(UISceneRendererTest, TopmostZOrderConsumesFirst)
{
    TestUIRoot root;
    auto behind = root.createUINode<UIButtonNode>("Behind");
    behind->_position = {100.0f, 100.0f};
    behind->_size     = {80.0f, 32.0f};
    behind->_zOrder   = 0;

    auto front = root.createUINode<UIButtonNode>("Front", behind.get());
    front->_position = {0.0f, 0.0f};
    front->_size     = {80.0f, 32.0f};
    front->_zOrder   = 10;
    layoutRoots(root.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(120.0f, 110.0f), root.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_FALSE(behind->_bPressed);
    EXPECT_TRUE(front->_bPressed);
}

} // namespace ya

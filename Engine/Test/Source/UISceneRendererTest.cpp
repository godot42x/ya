#include "Core/Event.h"
#include "Core/UI/UIBase.h"
#include "Runtime/Rendering/Common/UISceneRenderer.h"
#include "Scene/Node2D.h"
#include "Scene/Scene.h"

#include <gtest/gtest.h>

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
    Scene scene("UITestScene");
    auto* canvas = scene.createUINode<UICanvasNode>("Canvas");
    ASSERT_NE(canvas, nullptr);

    auto* button = scene.createUINode<UIButtonNode>("OK", canvas);
    ASSERT_NE(button, nullptr);
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};

    int clickCount = 0;
    button->_onClick = [&] { ++clickCount; };
    layoutRoots(scene.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    // Move outside: not consumed, no hover.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(10.0f, 10.0f), makeUICtx(10.0f, 10.0f), scene.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bHovered);

    // Move onto the button: consumed + hovered.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(120.0f, 110.0f), makeUICtx(120.0f, 110.0f), scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bHovered);

    // Press inside: consumed + pressed.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(120.0f, 110.0f), scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bPressed);

    // Release inside: consumed + click fired.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonReleasedEvent(0), makeUICtx(120.0f, 110.0f), scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_FALSE(button->_bPressed);
    EXPECT_EQ(clickCount, 1);
}

TEST(UISceneRendererTest, PressOutsideDoesNotArmButton)
{
    Scene scene("UITestScene");
    auto* button = scene.createUINode<UIButtonNode>("OK");
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};

    int clickCount = 0;
    button->_onClick = [&] { ++clickCount; };
    layoutRoots(scene.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    // Press outside: not consumed and the button is not armed.
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(10.0f, 10.0f), scene.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);

    // Move onto the button then release: no click (was never pressed).
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseMoveEvent(120.0f, 110.0f), makeUICtx(120.0f, 110.0f), scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonReleasedEvent(0), makeUICtx(120.0f, 110.0f), scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_EQ(clickCount, 0);
}

TEST(UISceneRendererTest, InvisibleNodeDoesNotConsume)
{
    Scene scene("UITestScene");
    auto* button = scene.createUINode<UIButtonNode>("OK");
    button->_position = {100.0f, 100.0f};
    button->_size     = {80.0f, 32.0f};
    button->_visibility = EUIVisibility::Hidden;
    layoutRoots(scene.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(120.0f, 110.0f), scene.getRootNode()),
              EUIRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);
}

TEST(UISceneRendererTest, TopmostZOrderConsumesFirst)
{
    Scene scene("UITestScene");
    auto* behind = scene.createUINode<UIButtonNode>("Behind");
    behind->_position = {100.0f, 100.0f};
    behind->_size     = {80.0f, 32.0f};
    behind->_zOrder   = 0;

    auto* front = scene.createUINode<UIButtonNode>("Front", behind);
    front->_position = {0.0f, 0.0f};
    front->_size     = {80.0f, 32.0f};
    front->_zOrder   = 10;
    layoutRoots(scene.getRootNode(), {.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});

    EXPECT_EQ(UISceneRenderer::handleEvent(MouseButtonPressedEvent(0), makeUICtx(120.0f, 110.0f), scene.getRootNode()),
              EUIRouteResult::HandledExclusive);
    EXPECT_FALSE(behind->_bPressed);
    EXPECT_TRUE(front->_bPressed);
}

} // namespace ya

#include "Core/Input/InputManager.h"
#include "Core/Input/InputRouter.h"
#include "Runtime/Application/App.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

struct RecordingInputNode final : IInputNode
{
    FInputReply                      nextReply{};
    int                              routeCount  = 0;
    int                              cancelCount = 0;
    std::vector<EInputCancelReason>  cancelReasons;

    [[nodiscard]] FInputReply route(FInputRouteContext& context, const FInputEvent& event) override
    {
        (void)context;
        (void)event;
        ++routeCount;
        return nextReply;
    }

    void cancelInput(FInputRouteContext& context, EInputCancelReason reason) override
    {
        (void)context;
        ++cancelCount;
        cancelReasons.push_back(reason);
    }
};

TEST(GameInputNodeTest, ForwardsEventsAndCancelsInputState)
{
    InputManager inputManager;
    GameInputNode root(inputManager);
    App          app;
    InputRouter  router;
    router.setApp(app);
    FInputRouteContext context{.app = app, .router = router};

    KeyPressedEvent pressed;
    pressed._keyCode = EKey::K_W;
    pressed._mod     = 0;

    root.route(context, pressed);
    EXPECT_TRUE(inputManager.isKeyPressed(EKey::K_W));

    root.cancelInput(context, EInputCancelReason::WindowFocusLost);
    EXPECT_FALSE(inputManager.isKeyPressed(EKey::K_W));
}

TEST(GameInputNodeTest, ForwardsMouseDeltaFromRoutedEvents)
{
    InputManager inputManager;
    GameInputNode root(inputManager);
    App          app;
    InputRouter  router;
    router.setApp(app);
    FInputRouteContext context{.app = app, .router = router};

    inputManager.preUpdate();
    root.route(context, MouseMoveEvent(100.0f, 200.0f, 7.0f, -3.0f));
    root.route(context, MouseMoveEvent(107.0f, 197.0f, 2.0f, 5.0f));

    const glm::vec2 delta = inputManager.getMouseDelta();
    EXPECT_FLOAT_EQ(delta.x, 9.0f);
    EXPECT_FLOAT_EQ(delta.y, 2.0f);
}

TEST(InputRouterTest, AppliesCaptureReplyAndCancelsOnRelease)
{
    RecordingInputNode root;
    App             app;
    InputRouter router;
    router.setApp(app);
    router.setDefaultNode(root);

    root.nextReply = FInputReply{
        .handled = true,
        .pointerCapture = FPointerCaptureRequest{
            .relative   = true,
            .hideCursor = true,
            .confine    = true,
            .confinement = Rect2D{
                .pos    = {16.0f, 24.0f},
                .extent = {320.0f, 180.0f},
            },
        },
    };

    MouseButtonPressedEvent pressed(EMouse::Left);
    EXPECT_TRUE(router.routeEvent(pressed));
    EXPECT_TRUE(router.isMouseCaptured());

    router.cancelInput(EInputCancelReason::WindowFocusLost);
    EXPECT_FALSE(router.isMouseCaptured());
    EXPECT_EQ(root.cancelCount, 2);
    EXPECT_EQ(root.cancelReasons[0], EInputCancelReason::CaptureReleased);
    EXPECT_EQ(root.cancelReasons[1], EInputCancelReason::WindowFocusLost);
}

TEST(InputRouterTest, RestoresPreviousRootWhenRegistrationEnds)
{
    RecordingInputNode defaultNode;
    RecordingInputNode overrideNode;
    App               app;
    InputRouter        router;
    router.setApp(app);
    router.setDefaultNode(defaultNode);

    {
        auto registration = router.registerNode(overrideNode);
        EXPECT_EQ(defaultNode.cancelCount, 1);

        MouseButtonPressedEvent pressed(EMouse::Left);
        overrideNode.nextReply = FInputReply{.handled = true};
        EXPECT_TRUE(router.routeEvent(pressed));
        EXPECT_EQ(overrideNode.routeCount, 1);
    }

    EXPECT_EQ(overrideNode.cancelCount, 1);
    EXPECT_EQ(overrideNode.cancelReasons[0], EInputCancelReason::NodeChanged);

    MouseButtonPressedEvent pressed(EMouse::Left);
    defaultNode.nextReply = FInputReply{.handled = true};
    EXPECT_TRUE(router.routeEvent(pressed));
    EXPECT_EQ(defaultNode.routeCount, 1);
}

} // namespace
} // namespace ya

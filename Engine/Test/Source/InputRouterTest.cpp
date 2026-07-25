#include "Core/Input/InputManager.h"
#include "Core/Input/InputRouter.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

struct RecordingInputRoot final : IHostInputRoot
{
    FInputReply                      nextReply{};
    int                              routeCount  = 0;
    int                              cancelCount = 0;
    std::vector<EInputCancelReason>  cancelReasons;

    [[nodiscard]] FInputReply route(const FInputEvent& event) override
    {
        (void)event;
        ++routeCount;
        return nextReply;
    }

    void cancelInput(EInputCancelReason reason) override
    {
        ++cancelCount;
        cancelReasons.push_back(reason);
    }
};

TEST(GameInputRootTest, ForwardsEventsAndCancelsInputState)
{
    InputManager inputManager;
    GameInputRoot root(inputManager);

    KeyPressedEvent pressed;
    pressed._keyCode = EKey::K_W;
    pressed._mod     = 0;

    root.route(pressed);
    EXPECT_TRUE(inputManager.isKeyPressed(EKey::K_W));

    root.cancelInput(EInputCancelReason::WindowFocusLost);
    EXPECT_FALSE(inputManager.isKeyPressed(EKey::K_W));
}

TEST(InputRouterTest, AppliesCaptureReplyAndCancelsOnRelease)
{
    RecordingInputRoot root;
    InputRouter router;
    router.setDefaultRoot(root);

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
    RecordingInputRoot defaultRoot;
    RecordingInputRoot overrideRoot;
    InputRouter        router;
    router.setDefaultRoot(defaultRoot);

    {
        auto registration = router.registerRoot(overrideRoot);
        EXPECT_EQ(defaultRoot.cancelCount, 1);

        MouseButtonPressedEvent pressed(EMouse::Left);
        overrideRoot.nextReply = FInputReply{.handled = true};
        EXPECT_TRUE(router.routeEvent(pressed));
        EXPECT_EQ(overrideRoot.routeCount, 1);
    }

    EXPECT_EQ(overrideRoot.cancelCount, 1);
    EXPECT_EQ(overrideRoot.cancelReasons[0], EInputCancelReason::RootChanged);

    MouseButtonPressedEvent pressed(EMouse::Left);
    defaultRoot.nextReply = FInputReply{.handled = true};
    EXPECT_TRUE(router.routeEvent(pressed));
    EXPECT_EQ(defaultRoot.routeCount, 1);
}

} // namespace
} // namespace ya

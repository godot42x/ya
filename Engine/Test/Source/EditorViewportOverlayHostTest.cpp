#include "GameEditor/UI/EditorViewportHost.h"

#include "Core/Event.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

struct FRecordingViewportOverlay final : IEditorViewportOverlay
{
    FEditorViewportHostState lastHost{};
    int                      dispatchCount = 0;

    void syncHost(const FEditorViewportHostState& host) override { lastHost = host; }

    EWidgetRouteResult dispatchEvent(const Event& event, const glm::vec2& localPoint) override
    {
        (void)event;
        (void)localPoint;
        ++dispatchCount;
        return EWidgetRouteResult::HandledExclusive;
    }

    bool isActive() const override { return true; }
};

} // namespace

TEST(EditorViewportOverlayHostTest, SyncsHostToInstalledOverlay)
{
    EditorViewportOverlayHost host;
    auto                      overlay = std::make_shared<FRecordingViewportOverlay>();
    host.setOverlay(overlay);

    FEditorViewportHostState state{};
    state.widgetRect  = Rect2D{glm::vec2{10.0f, 20.0f}, glm::vec2{800.0f, 600.0f}};
    state.extent      = {800.0f, 600.0f};
    state.bHovered    = true;
    state.bFocused    = true;
    host.syncHost(state);

    EXPECT_EQ(overlay->lastHost.widgetRect.pos.x, 10.0f);
    EXPECT_EQ(overlay->lastHost.extent.x, 800.0f);
    EXPECT_TRUE(overlay->lastHost.bHovered);
}

TEST(EditorViewportOverlayHostTest, DispatchForwardsToOverlayAndReportsActive)
{
    EditorViewportOverlayHost host;
    auto                      overlay = std::make_shared<FRecordingViewportOverlay>();
    host.setOverlay(overlay);

    MouseMoveEvent move(32.0f, 48.0f);
    EXPECT_EQ(host.dispatchEvent(move, {32.0f, 48.0f}), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(overlay->dispatchCount, 1);
    EXPECT_TRUE(host.isActive());
    EXPECT_FALSE(host.wantsPointerCapture());
}

TEST(EditorViewportOverlayHostTest, ClearOverlayStopsDispatch)
{
    EditorViewportOverlayHost host;
    host.setOverlay(std::make_shared<FRecordingViewportOverlay>());
    host.clearOverlay();

    MouseMoveEvent move(0.0f, 0.0f);
    EXPECT_EQ(host.dispatchEvent(move, {}), EWidgetRouteResult::NotHandled);
    EXPECT_FALSE(host.isActive());
}

} // namespace ya

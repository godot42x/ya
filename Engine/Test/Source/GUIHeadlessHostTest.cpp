#include "GUI/Host/GUIHeadlessHost.h"
#include "GUI/Widgets/Controls/Panel.h"

#include <gtest/gtest.h>

#include <memory>

namespace ya
{
namespace
{

struct ResizeAfterFirstFrameEventSource final : IAppEventSource
{
    uint32_t polls = 0;

    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        if (++polls == 2) {
            emit(WindowResizeEvent(0, 320, 200));
        }
    }
};

struct HeadlessDelegate final : IGUIAppDelegate
{
    int updateCount = 0;

    void buildUI(WidgetTree& tree) override
    {
        auto panel = std::make_shared<UIPanel>("HeadlessPanel");
        panel->setPosition({8.0f, 12.0f});
        panel->setSize({96.0f, 48.0f});
        tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    }

    void updateUI() override
    {
        ++updateCount;
    }
};

} // namespace

TEST(GUIHeadlessHostTest, ReusesAppKernelAndBuildsSnapshotsWithoutWindowOrRhi)
{
    ResizeAfterFirstFrameEventSource eventSource;
    HeadlessDelegate                 delegate;
    uint32_t                         snapshotCount = 0;
    Extent2D                         lastExtent{};
    size_t                           lastItemCount = 0;

    GUIHeadlessHost host(
        FGUIHeadlessHostConfig{
            .logicalExtent = {160, 100},
            .eventSource   = &eventSource,
            .automation    = {.exitAfterFrame = 3},
            .onSnapshot    = [&](const UIFrameSnapshot& snapshot) {
                ++snapshotCount;
                lastExtent    = snapshot.logicalExtent;
                lastItemCount = snapshot.items.size();
            },
        },
        delegate);

    ASSERT_TRUE(host.init());
    EXPECT_EQ(host.run(), 0);
    EXPECT_EQ(delegate.updateCount, 3);
    EXPECT_EQ(snapshotCount, 3u);
    EXPECT_EQ(lastExtent.width, 320u);
    EXPECT_EQ(lastExtent.height, 200u);
    EXPECT_GT(lastItemCount, 0u);
}

} // namespace ya

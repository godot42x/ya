#include "GUI/Host/GUIHeadlessHost.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Theme.h"

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
        auto panel = std::make_shared<UICanvasPanel>("HeadlessPanel");
        FCanvasSlotArgs slot;
        slot.offset = {8.0f, 12.0f};
        slot.fixedSize = {96.0f, 48.0f};
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);
    }

    void updateUI() override
    {
        ++updateCount;
    }
};

} // namespace

/// Sample the top-most sprite color at a logical point from a snapshot.
static glm::vec4 sampleSpriteColorAt(const ya::UIFrameSnapshot& snap, float x, float y)
{
    glm::vec4 best{0.0f};
    float     bestArea = -1.0f;
    for (const auto& item : snap.items) {
        if (item.kind != ya::UIFrameDrawItem::EKind::Sprite) {
            continue;
        }
        const float x0 = item.pos.x;
        const float y0 = item.pos.y;
        const float x1 = x0 + item.size.x;
        const float y1 = y0 + item.size.y;
        if (x < x0 || x >= x1 || y < y0 || y >= y1) {
            continue;
        }
        const float area = item.size.x * item.size.y;
        if (area > bestArea) {
            bestArea = area;
            best     = item.color;
        }
    }
    return best;
}

TEST(GUIHeadlessHostTest, MenuBarItemHoverRepaintsWithHoveredColor)
{
    // Drives a real UIMenuBar through AppKernel -> WidgetTree -> snapshot and
    // asserts the *visible* effect of hover (the menubar item sprite must
    // switch from its normal color to its hovered color). This guards the
    // exact regression class the workbench hit: a hover state that flips but
    // whose two colors are too close to see.
    struct HoverEventSource final : IAppEventSource
    {
        uint32_t polls = 0;
        void pollEvents(const std::function<void(const Event&)>& emit) override
        {
            // Frame 2: move the pointer onto the File item (top-left of bar).
            if (++polls == 2) {
                emit(MouseMoveEvent(5.0f, 5.0f));
            }
        }
    };

    struct MenuBarDelegate final : IGUIAppDelegate
    {
        // Owned by the delegate: WidgetTree keeps a raw UITheme* after
        // setTheme, so the theme must outlive buildUI (style-system Phase 3
        // cleanup: menu items resolve the "menubar" key instead of bare
        // color fields).
        std::shared_ptr<ya::UITheme> theme;

        void buildUI(ya::WidgetTree& tree) override
        {
            auto barOwned   = std::make_shared<ya::UIMenuBar>("TestMenuBar");
            ya::FCanvasSlotArgs slot;
            slot.anchorMin = {0.0f, 0.0f};
            slot.anchorMax = {1.0f, 0.0f};
            slot.fixedSize = {0.0f, 30.0f};
            tree.attach(*tree.getLayer(ya::WidgetTree::ELayer::Content), barOwned, slot);
            barOwned->addItem("File", [] { return ya::UIMenu::create({{"New", nullptr}}); });

            // Mirror the workbench theme: lift both stops above the window
            // background (0.075) and widen the gap so hover is visible.
            theme             = std::make_shared<ya::UITheme>();
            auto style        = ya::FMenuBarItemStyle{};
            style.normalFill  = ya::FBrush::solid({0.16f, 0.18f, 0.22f, 1.0f});
            style.hoveredFill = ya::FBrush::solid({0.30f, 0.33f, 0.40f, 1.0f});
            theme->define<ya::FMenuBarItemStyle>("menubar", style);
            tree.setTheme(theme.get());
        }
        void updateUI() override {}
    };

    HoverEventSource eventSource;
    MenuBarDelegate  delegate;
    glm::vec4        hovered{0.0f};
    GUIHeadlessHost  host(
        FGUIHeadlessHostConfig{
            .logicalExtent = {320, 200},
            .eventSource   = &eventSource,
            .automation    = {.exitAfterTick = 4},
            .onSnapshot    = [&](const ya::UIFrameSnapshot& snap) {
                // Capture the last frame's color at the File item after hover.
                hovered = sampleSpriteColorAt(snap, 5.0f, 5.0f);
            },
        },
        delegate);
    ASSERT_TRUE(host.init());
    EXPECT_EQ(host.run(), 0);

    // The hovered color must land on the themed hovered stop, clearly above
    // the normal stop (the exact workbench regression: a hover that flips but
    // whose two colors were too close to see).
    EXPECT_NEAR(hovered.r, 0.30f, 0.05f);
    EXPECT_NEAR(hovered.g, 0.33f, 0.05f);
    EXPECT_NEAR(hovered.b, 0.40f, 0.05f);
}

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
            .automation    = {.exitAfterTick = 3},
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

TEST(GUIHeadlessHostTest, UnthemedFallbackThenThemeSwitchRepaintsSnapshot)
{
    struct FallbackDelegate final : IGUIAppDelegate
    {
        WidgetTree*              tree  = nullptr;
        std::shared_ptr<UITheme> theme;
        int                      ticks = 0;

        void buildUI(WidgetTree& inTree) override
        {
            tree = &inTree;
            auto panel = std::make_shared<UICanvasPanel>("P");
            FCanvasSlotArgs panelSlot;
            panelSlot.offset    = {8.0f, 8.0f};
            panelSlot.fixedSize = {64.0f, 32.0f};
            inTree.attach(*inTree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);

            auto image = std::make_shared<UIImage>("Img");
            FCanvasSlotArgs imageSlot;
            imageSlot.offset    = {80.0f, 8.0f};
            imageSlot.fixedSize = {32.0f, 32.0f};
            inTree.attach(*inTree.getLayer(WidgetTree::ELayer::Content), image, imageSlot);
        }

        void updateUI() override
        {
            ++ticks;
            if (ticks == 2 && tree) {
                theme = std::make_shared<UITheme>();
                FPanelStyle panelStyle;
                panelStyle.fillColor = FBrush::solid({0.15f, 0.80f, 0.20f, 1.0f});
                theme->define<FPanelStyle>("panel", panelStyle);
                FImageStyle imageStyle;
                imageStyle.placeholderFill = FBrush::solid({0.80f, 0.20f, 0.10f, 1.0f});
                theme->define<FImageStyle>("image", imageStyle);
                tree->setTheme(theme.get());
            }
        }
    };

    FallbackDelegate delegate;
    glm::vec4        panelColor{0.0f};
    glm::vec4        imageColor{0.0f};
    uint32_t         themedFrame = 0;
    GUIHeadlessHost  host(
        FGUIHeadlessHostConfig{
            .logicalExtent = {160, 80},
            .automation    = {.exitAfterTick = 3},
            .onSnapshot    = [&](const UIFrameSnapshot& snap) {
                const glm::vec4 panelSample = sampleSpriteColorAt(snap, 10.0f, 10.0f);
                const glm::vec4 imageSample = sampleSpriteColorAt(snap, 90.0f, 10.0f);
                if (delegate.ticks == 1) {
                    EXPECT_EQ(panelSample, FPanelStyle{}.fillColor.tintColor);
                    EXPECT_EQ(imageSample, FImageStyle{}.placeholderFill.tintColor);
                }
                if (delegate.ticks >= 2) {
                    panelColor  = panelSample;
                    imageColor  = imageSample;
                    themedFrame = delegate.ticks;
                }
            },
        },
        delegate);

    ASSERT_TRUE(host.init());
    EXPECT_EQ(host.run(), 0);
    EXPECT_GE(themedFrame, 2u);
    EXPECT_EQ(panelColor, glm::vec4(0.15f, 0.80f, 0.20f, 1.0f));
    EXPECT_EQ(imageColor, glm::vec4(0.80f, 0.20f, 0.10f, 1.0f));
}

} // namespace ya

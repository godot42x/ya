// GPO-001: opt-in rebuilt-rect capture behind YA_PROFILING_*. Cheap
// GuiPerfStats.rebuiltWidgets stay in every build; rect names are only
// recorded when the runtime inspector toggle is on.

#include "Core/Profiling/Profiling.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "GUI/Compose/GuiFrameInspectorOverlay.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

struct InspectorScope
{
    bool previousEnabled  = false;
    uint8_t previousChannels = 0;

    explicit InspectorScope(bool enable)
        : previousEnabled(profiling::isGuiFrameInspectorEnabled())
        , previousChannels(profiling::getGuiFrameInspectorChannels())
    {
        profiling::setGuiFrameInspectorEnabled(enable);
    }

    ~InspectorScope()
    {
        profiling::setGuiFrameInspectorEnabled(previousEnabled);
        profiling::setGuiFrameInspectorChannels(previousChannels);
    }
};

WidgetTree makeTree()
{
    return WidgetTree({.width = 800, .height = 600});
}

std::shared_ptr<UIBorder> attachProbe(WidgetTree& tree, const char* name, const Rect2D& rect)
{
    auto panel = std::make_shared<UIBorder>(name);
    FCanvasSlotArgs slot;
    slot.offset    = rect.pos;
    slot.fixedSize = rect.extent;
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);
    return panel;
}

} // namespace

TEST(GuiFrameInspectorTest, RuntimeToggleFollowsCompileMode)
{
    InspectorScope restore(false);
    EXPECT_FALSE(YA_GUI_INSPECTOR_IS_ENABLED());

    profiling::setGuiFrameInspectorEnabled(true);
    if constexpr (profiling::isCompiledOut()) {
        EXPECT_FALSE(profiling::isGuiFrameInspectorEnabled());
        EXPECT_FALSE(YA_GUI_INSPECTOR_IS_ENABLED());
    }
    else {
        EXPECT_TRUE(profiling::isGuiFrameInspectorEnabled());
        EXPECT_TRUE(YA_GUI_INSPECTOR_IS_ENABLED());
    }
}

TEST(GuiFrameInspectorTest, DisabledLeavesRebuildRectsEmpty)
{
    InspectorScope restore(false);
    WidgetTree     tree = makeTree();
    auto           probe = attachProbe(tree, "Probe", Rect2D{.pos = {10.0f, 20.0f}, .extent = {100.0f, 40.0f}});

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    probe->markPaintDirty();
    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    EXPECT_TRUE(tree.getFrameInspectorRecord().rebuiltRects.empty());
    EXPECT_FALSE(snapshot.items.empty());
}

TEST(GuiFrameInspectorTest, DirtyLeafRecordsOneRectWithoutChangingSnapshot)
{
    if constexpr (profiling::isCompiledOut()) {
        GTEST_SKIP() << "GUI Frame Inspector compiled out";
    }

    InspectorScope restore(true);
    WidgetTree     tree = makeTree();
    auto           probe = attachProbe(tree, "Probe", Rect2D{.pos = {10.0f, 20.0f}, .extent = {100.0f, 40.0f}});

    tree.buildSnapshot(UIFrameBuildContext{});
    const UIFrameSnapshot clean = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_TRUE(tree.getFrameInspectorRecord().rebuiltRects.empty());

    probe->markPaintDirty();
    const UIFrameSnapshot dirty = tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    EXPECT_EQ(dirty.items.size(), clean.items.size());

    const FGuiFrameInspectorRecord& record = tree.getFrameInspectorRecord();
    ASSERT_EQ(record.rebuiltRects.size(), 1u);
    EXPECT_EQ(record.rebuiltRects[0].name, "Probe");
    EXPECT_EQ(record.rebuiltRects[0].layoutRect.pos, glm::vec2(10.0f, 20.0f));
    EXPECT_EQ(record.rebuiltRects[0].layoutRect.extent, glm::vec2(100.0f, 40.0f));
    EXPECT_GE(record.paintDirtyDelta, 1u);
}

TEST(GuiFrameInspectorTest, ApplySpecWarnsWhenCompiledOut)
{
    InspectorScope restore(false);
    if constexpr (profiling::isCompiledOut()) {
        EXPECT_FALSE(applyGuiFrameInspectorSpec("hud,rebuild"));
        EXPECT_FALSE(profiling::isGuiFrameInspectorEnabled());
    }
    else {
        EXPECT_TRUE(applyGuiFrameInspectorSpec("hud,rebuild"));
        EXPECT_TRUE(profiling::isGuiFrameInspectorEnabled());
        const uint8_t channels = profiling::getGuiFrameInspectorChannels();
        EXPECT_NE(channels & guiFrameInspectorChannelMask(EGuiFrameInspectorChannel::Hud), 0);
        EXPECT_NE(channels & guiFrameInspectorChannelMask(EGuiFrameInspectorChannel::Rebuild), 0);
    }
}

TEST(GuiFrameInspectorTest, ComposeModelFlushMatchesClipRun)
{
    const Rect2D clip{.pos = {10.0f, 20.0f}, .extent = {200.0f, 80.0f}};
    UIFrameSnapshot snapshot;
    snapshot.logicalExtent = {.width = 800, .height = 600};
    for (uint32_t i = 0; i < 8; ++i) {
        UIFrameDrawItem item;
        item.kind     = UIFrameDrawItem::EKind::Sprite;
        item.pos      = {0.0f, static_cast<float>(i) * 10.0f};
        item.size     = {8.0f, 8.0f};
        item.bClipped = true;
        item.clip     = clip;
        snapshot.items.push_back(item);
    }

    FGuiFrameInspectorRecord record;
    captureGuiComposeInspector(record, snapshot, {});
    EXPECT_EQ(record.modelScreenFlush, 1u);
}

TEST(GuiFrameInspectorTest, OverdrawFactorCountsStackedCoverage)
{
    UIFrameSnapshot snapshot;
    snapshot.logicalExtent = {.width = 100, .height = 100};
    for (int i = 0; i < 2; ++i) {
        UIFrameDrawItem item;
        item.kind = UIFrameDrawItem::EKind::Sprite;
        item.pos  = {0.0f, 0.0f};
        item.size = {100.0f, 100.0f};
        snapshot.items.push_back(item);
    }

    FGuiFrameInspectorRecord record;
    captureGuiOverdrawInspector(record, snapshot, snapshot.logicalExtent);
    EXPECT_NEAR(record.overdrawFactor, 2.0f, 0.05f);
    EXPECT_GE(record.maxCoverage, 2.0f);
    EXPECT_GE(record.meanCoverage, 1.9f);
}

TEST(GuiFrameInspectorTest, ChannelToggleShowsOnQuery)
{
    if constexpr (profiling::isCompiledOut()) {
        GTEST_SKIP() << "GUI Frame Inspector compiled out";
    }

    InspectorScope restore(false);
    EXPECT_FALSE(isGuiFrameInspectorChannelOn(EGuiFrameInspectorChannel::Hud));
    EXPECT_FALSE(isGuiFrameInspectorChannelOn(EGuiFrameInspectorChannel::Rebuild));

    toggleGuiFrameInspectorChannel(EGuiFrameInspectorChannel::Hud);
    EXPECT_TRUE(isGuiFrameInspectorChannelOn(EGuiFrameInspectorChannel::Hud));
    EXPECT_FALSE(isGuiFrameInspectorChannelOn(EGuiFrameInspectorChannel::Rebuild));

    toggleGuiFrameInspectorChannel(EGuiFrameInspectorChannel::Hud);
    EXPECT_FALSE(isGuiFrameInspectorChannelOn(EGuiFrameInspectorChannel::Hud));
}

TEST(GuiFrameInspectorTest, HudDefaultPosClearsMenuBarAndDragMovesIt)
{
    if constexpr (profiling::isCompiledOut()) {
        GTEST_SKIP() << "GUI Frame Inspector compiled out";
    }

    InspectorScope restore(false);
    resetGuiFrameInspectorHudPlacement();
    EXPECT_GE(getGuiFrameInspectorHudPos().y, 40.0f);

    toggleGuiFrameInspectorChannel(EGuiFrameInspectorChannel::Hud);
    const Rect2D hud = guiFrameInspectorHudRect();
    const glm::vec2 grab = hud.pos + hud.extent * 0.5f;
    const Extent2D fb{.width = 800, .height = 600};
    EXPECT_TRUE(handleGuiFrameInspectorHudInput(MouseButtonPressedEvent(EMouse::Left), grab, fb, false));
    EXPECT_TRUE(handleGuiFrameInspectorHudInput(MouseMoveEvent(grab.x + 40.0f, grab.y + 30.0f),
                                                {grab.x + 40.0f, grab.y + 30.0f},
                                                fb,
                                                false));
    EXPECT_NEAR(getGuiFrameInspectorHudPos().x, hud.pos.x + 40.0f, 0.5f);
    EXPECT_NEAR(getGuiFrameInspectorHudPos().y, hud.pos.y + 30.0f, 0.5f);
    EXPECT_TRUE(handleGuiFrameInspectorHudInput(MouseButtonReleasedEvent(EMouse::Left),
                                                {grab.x + 40.0f, grab.y + 30.0f},
                                                fb,
                                                false));
    resetGuiFrameInspectorHudPlacement();
}

} // namespace ya

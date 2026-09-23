// GAH-101: CPU-only clip-run compose. Adjacent items that share a flattened
// clip keep one scissor (one push / pop / screen flush, overflow excluded).
// Painter order and snapshot digest stay unchanged.

#include "GUI/Compose/UIFrameComposeReplay.h"
#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/UIFrameSnapshotDump.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

UIFrameDrawItem makeItem(UIFrameDrawItem::EKind kind,
                         glm::vec2              pos,
                         bool                   bClipped,
                         const Rect2D&          clip = {})
{
    UIFrameDrawItem item;
    item.kind     = kind;
    item.pos      = pos;
    item.size     = {8.0f, 8.0f};
    item.bClipped = bClipped;
    if (bClipped) {
        item.clip = clip;
    }
    if (kind == UIFrameDrawItem::EKind::Text) {
        item.text = "x";
    }
    else if (kind == UIFrameDrawItem::EKind::Line) {
        item.lineFrom      = pos;
        item.lineTo        = pos + glm::vec2(8.0f, 0.0f);
        item.lineThickness = 1.0f;
    }
    return item;
}

UIFrameSnapshot makeSnapshot(std::vector<UIFrameDrawItem> items)
{
    UIFrameSnapshot snapshot;
    snapshot.logicalExtent = {.width = 800, .height = 600};
    snapshot.items         = std::move(items);
    return snapshot;
}

const Rect2D kClipA{.pos = {10.0f, 20.0f}, .extent = {200.0f, 80.0f}};
const Rect2D kClipB{.pos = {40.0f, 60.0f}, .extent = {120.0f, 40.0f}};

void expectPainterOrderMatchesItems(const FUIFrameComposeReplayStats& stats,
                                    const UIFrameSnapshot&            snapshot)
{
    ASSERT_EQ(stats.painterOrder.size(), snapshot.items.size());
    for (size_t i = 0; i < snapshot.items.size(); ++i) {
        EXPECT_EQ(stats.painterOrder[i], snapshot.items[i].kind) << "item " << i;
    }
}

} // namespace

TEST(ComposeClipReplayTest, SameClipSpritesShareOneScreenFlush)
{
    constexpr uint32_t kFour = 4;
    constexpr uint32_t kEight = 8;

    std::vector<UIFrameDrawItem> fourItems;
    fourItems.reserve(kFour);
    for (uint32_t i = 0; i < kFour; ++i) {
        fourItems.push_back(makeItem(UIFrameDrawItem::EKind::Sprite,
                                     {0.0f, static_cast<float>(i) * 10.0f},
                                     true,
                                     kClipA));
    }
    const UIFrameSnapshot four = makeSnapshot(std::move(fourItems));

    std::vector<UIFrameDrawItem> eightItems;
    eightItems.reserve(kEight);
    for (uint32_t i = 0; i < kEight; ++i) {
        eightItems.push_back(makeItem(UIFrameDrawItem::EKind::Sprite,
                                      {0.0f, static_cast<float>(i) * 10.0f},
                                      true,
                                      kClipA));
    }
    const UIFrameSnapshot eight = makeSnapshot(std::move(eightItems));

    const FUIFrameComposeReplayStats statsFour  = measureUIFrameComposeReplay(four);
    const FUIFrameComposeReplayStats statsEight = measureUIFrameComposeReplay(eight);

    EXPECT_EQ(statsFour.itemCount, kFour);
    EXPECT_EQ(statsFour.clippedItemCount, kFour);
    EXPECT_EQ(statsFour.clipPushCount, 1u);
    EXPECT_EQ(statsFour.clipPopCount, 1u);
    EXPECT_EQ(statsFour.screenFlushCount, 1u);
    EXPECT_EQ(statsFour.scissorTransitionCount, 2u);

    EXPECT_EQ(statsEight.screenFlushCount, 1u);
    EXPECT_EQ(statsEight.clipPushCount, 1u);
    EXPECT_EQ(statsEight.clipPopCount, 1u);
    EXPECT_EQ(statsEight.scissorTransitionCount, 2u);
    expectPainterOrderMatchesItems(statsEight, eight);
}

TEST(ComposeClipReplayTest, UnclippedSpritesShareOneScreenFlush)
{
    std::vector<UIFrameDrawItem> items;
    for (uint32_t i = 0; i < 8; ++i) {
        items.push_back(makeItem(UIFrameDrawItem::EKind::Sprite,
                                 {0.0f, static_cast<float>(i) * 10.0f},
                                 false));
    }
    const UIFrameSnapshot snapshot = makeSnapshot(std::move(items));
    const FUIFrameComposeReplayStats stats = measureUIFrameComposeReplay(snapshot);

    EXPECT_EQ(stats.clippedItemCount, 0u);
    EXPECT_EQ(stats.clipPushCount, 0u);
    EXPECT_EQ(stats.clipPopCount, 0u);
    EXPECT_EQ(stats.scissorTransitionCount, 0u);
    EXPECT_EQ(stats.screenFlushCount, 1u);
    EXPECT_TRUE(stats.scissorSequence.empty());
}

TEST(ComposeClipReplayTest, ClipAThenBThenUnclippedRecordsEachTransition)
{
    const UIFrameSnapshot snapshot = makeSnapshot({
        makeItem(UIFrameDrawItem::EKind::Sprite, {0.0f, 0.0f}, true, kClipA),
        makeItem(UIFrameDrawItem::EKind::Sprite, {0.0f, 10.0f}, true, kClipB),
        makeItem(UIFrameDrawItem::EKind::Sprite, {0.0f, 20.0f}, false),
    });

    const uint64_t digestBefore = digestUIFrameSnapshot(snapshot);
    const FUIFrameComposeReplayStats stats = measureUIFrameComposeReplay(snapshot);

    EXPECT_EQ(digestUIFrameSnapshot(snapshot), digestBefore);
    EXPECT_EQ(stats.itemCount, 3u);
    EXPECT_EQ(stats.clippedItemCount, 2u);
    EXPECT_EQ(stats.clipPushCount, 2u);
    EXPECT_EQ(stats.clipPopCount, 2u);
    EXPECT_EQ(stats.scissorTransitionCount, 3u);
    EXPECT_EQ(stats.screenFlushCount, 3u);
    ASSERT_EQ(stats.scissorSequence.size(), 3u);
    EXPECT_TRUE(stats.scissorSequence[0].bClipped);
    EXPECT_EQ(stats.scissorSequence[0].clip.pos, kClipA.pos);
    EXPECT_EQ(stats.scissorSequence[0].clip.extent, kClipA.extent);
    EXPECT_TRUE(stats.scissorSequence[1].bClipped);
    EXPECT_EQ(stats.scissorSequence[1].clip.pos, kClipB.pos);
    EXPECT_EQ(stats.scissorSequence[1].clip.extent, kClipB.extent);
    EXPECT_FALSE(stats.scissorSequence[2].bClipped);
    expectPainterOrderMatchesItems(stats, snapshot);
}

TEST(ComposeClipReplayTest, MixedSpriteTextLineSameClipShareOneFlush)
{
    const UIFrameSnapshot snapshot = makeSnapshot({
        makeItem(UIFrameDrawItem::EKind::Sprite, {0.0f, 0.0f}, true, kClipA),
        makeItem(UIFrameDrawItem::EKind::Text, {0.0f, 10.0f}, true, kClipA),
        makeItem(UIFrameDrawItem::EKind::Line, {0.0f, 20.0f}, true, kClipA),
    });
    const FUIFrameComposeReplayStats stats = measureUIFrameComposeReplay(snapshot);

    EXPECT_EQ(stats.itemCount, 3u);
    EXPECT_EQ(stats.screenFlushCount, 1u);
    EXPECT_EQ(stats.clipPushCount, 1u);
    EXPECT_EQ(stats.clipPopCount, 1u);
    ASSERT_EQ(stats.painterOrder.size(), 3u);
    EXPECT_EQ(stats.painterOrder[0], UIFrameDrawItem::EKind::Sprite);
    EXPECT_EQ(stats.painterOrder[1], UIFrameDrawItem::EKind::Text);
    EXPECT_EQ(stats.painterOrder[2], UIFrameDrawItem::EKind::Line);
}

TEST(ComposeClipReplayTest, EmptyClipExtentStillFormsOneClipRun)
{
    const Rect2D emptyClip{.pos = {50.0f, 50.0f}, .extent = {0.0f, 0.0f}};
    const UIFrameSnapshot snapshot = makeSnapshot({
        makeItem(UIFrameDrawItem::EKind::Sprite, {50.0f, 50.0f}, true, emptyClip),
        makeItem(UIFrameDrawItem::EKind::Sprite, {60.0f, 50.0f}, true, emptyClip),
    });
    const FUIFrameComposeReplayStats stats = measureUIFrameComposeReplay(snapshot);

    EXPECT_EQ(stats.clippedItemCount, 2u);
    EXPECT_EQ(stats.clipPushCount, 1u);
    EXPECT_EQ(stats.clipPopCount, 1u);
    EXPECT_EQ(stats.screenFlushCount, 1u);
    EXPECT_EQ(stats.scissorTransitionCount, 2u);
}

TEST(ComposeClipReplayTest, NestedBuilderClipFlattensToOneClipRun)
{
    UIFrameBuilder builder(UIFrameBuildContext{});
    builder.pushClip(Rect2D{.pos = {0.0f, 0.0f}, .extent = {200.0f, 100.0f}});
    builder.pushClip(Rect2D{.pos = {10.0f, 10.0f}, .extent = {80.0f, 40.0f}});
    for (int i = 0; i < 4; ++i) {
        builder.addSprite(Rect2D{.pos = {12.0f, 12.0f + static_cast<float>(i) * 4.0f}, .extent = {8.0f, 8.0f}},
                          glm::vec4(1.0f),
                          nullptr);
    }
    builder.popClip();
    builder.popClip();
    const UIFrameSnapshot snapshot = builder.build({.width = 800, .height = 600});

    ASSERT_EQ(snapshot.items.size(), 4u);
    for (const auto& item : snapshot.items) {
        EXPECT_TRUE(item.bClipped);
        EXPECT_EQ(item.clip.pos, glm::vec2(10.0f, 10.0f));
        EXPECT_EQ(item.clip.extent, glm::vec2(80.0f, 40.0f));
    }

    const uint64_t digestBefore = digestUIFrameSnapshot(snapshot);
    const FUIFrameComposeReplayStats stats = measureUIFrameComposeReplay(snapshot);
    EXPECT_EQ(digestUIFrameSnapshot(snapshot), digestBefore);
    EXPECT_EQ(stats.screenFlushCount, 1u);
    EXPECT_EQ(stats.scissorTransitionCount, 2u);
    EXPECT_EQ(stats.clipPushCount, 1u);
    expectPainterOrderMatchesItems(stats, snapshot);
}

TEST(ComposeClipReplayTest, UnclippedGapPreventsMergingTheSameClip)
{
    const UIFrameSnapshot snapshot = makeSnapshot({
        makeItem(UIFrameDrawItem::EKind::Sprite, {0.0f, 0.0f}, true, kClipA),
        makeItem(UIFrameDrawItem::EKind::Sprite, {0.0f, 10.0f}, false),
        makeItem(UIFrameDrawItem::EKind::Sprite, {0.0f, 20.0f}, true, kClipA),
    });
    const FUIFrameComposeReplayStats stats = measureUIFrameComposeReplay(snapshot);

    EXPECT_EQ(stats.clipPushCount, 2u);
    EXPECT_EQ(stats.clipPopCount, 2u);
    EXPECT_EQ(stats.screenFlushCount, 3u);
    EXPECT_EQ(stats.scissorTransitionCount, 4u);
    ASSERT_EQ(stats.scissorSequence.size(), 4u);
    EXPECT_TRUE(stats.scissorSequence[0].bClipped);
    EXPECT_FALSE(stats.scissorSequence[1].bClipped);
    EXPECT_TRUE(stats.scissorSequence[2].bClipped);
    EXPECT_FALSE(stats.scissorSequence[3].bClipped);
}

TEST(ComposeClipReplayTest, SameComposeScissorComparesFlagAndRect)
{
    const FComposeScissorState a{true, kClipA};
    const FComposeScissorState aAgain{true, kClipA};
    const FComposeScissorState b{true, kClipB};
    const FComposeScissorState none{};
    const FComposeScissorState noneWithGarbage{false, kClipA};

    EXPECT_TRUE(sameComposeScissor(a, aAgain));
    EXPECT_FALSE(sameComposeScissor(a, b));
    EXPECT_FALSE(sameComposeScissor(a, none));
    EXPECT_TRUE(sameComposeScissor(none, noneWithGarbage));
}

} // namespace ya

#include "Render3D/Common/ViewResourceKey.h"

#include <gtest/gtest.h>


namespace ya
{

namespace
{

constexpr FRenderFeatureMask kGameFeatures     = toMask(ERenderFeature::Game);
constexpr FRenderFeatureMask kEditorFeatures   = toMask(ERenderFeature::Game) | toMask(ERenderFeature::Gizmo);
constexpr Extent2D           kWorldExtent      = {.width = 1280, .height = 720};
constexpr Extent2D           kThumbnailExtent  = {.width = 256, .height = 256};

ViewResourceKey makeKey(SceneViewId viewId, Extent2D extent, FRenderFeatureMask features = kGameFeatures)
{
    return ViewResourceKey{
        .viewId      = viewId,
        .extent      = extent,
        .colorFormat = EFormat::R16G16B16A16_SFLOAT,
        .depthFormat = EFormat::D32_SFLOAT,
        .featureMask = features,
    };
}

/// The View records this table holds. The test owns the values, so "B did not
/// overwrite A" is a statement about the values themselves, not about a key.
struct TestViewRecord
{
    int      id = 0;
    Extent2D extent{};
};

TEST(ViewResourceKeyTest, IdentityExtentFormatAndFeaturePolicyAllPartitionTheKey)
{
    const ViewResourceKey base = makeKey(11, kWorldExtent);

    EXPECT_EQ(base, makeKey(11, kWorldExtent));
    // Two Views that happen to be the same size are still two Views.
    EXPECT_NE(base, makeKey(12, kWorldExtent));
    // One View at two sizes is two sets of resources, not a rewrite of one.
    EXPECT_NE(base, makeKey(11, kThumbnailExtent));
    // A View that renders a different feature policy renders different work.
    EXPECT_NE(base, makeKey(11, kWorldExtent, kEditorFeatures));

    ViewResourceKey otherColor = base;
    otherColor.colorFormat     = EFormat::R8G8B8A8_UNORM;
    EXPECT_NE(base, otherColor);

    ViewResourceKey otherDepth = base;
    otherDepth.depthFormat     = EFormat::D32_SFLOAT_S8_UINT;
    EXPECT_NE(base, otherDepth);
}

TEST(ViewResourceTableTest, ASecondViewDoesNotOverwriteTheFirst)
{
    ViewResourceTable<TestViewRecord> table;

    table.publish(makeKey(11, kWorldExtent), TestViewRecord{.id = 1, .extent = kWorldExtent});
    table.publish(makeKey(12, kThumbnailExtent), TestViewRecord{.id = 2, .extent = kThumbnailExtent});

    ASSERT_EQ(table.size(), 2u);
    const TestViewRecord* world = table.findForView(11);
    const TestViewRecord* thumbnail = table.findForView(12);
    ASSERT_NE(world, nullptr);
    ASSERT_NE(thumbnail, nullptr);
    EXPECT_EQ(world->id, 1);
    EXPECT_EQ(world->extent.width, 1280u);
    EXPECT_EQ(world->extent.height, 720u);
    EXPECT_EQ(thumbnail->id, 2);
    EXPECT_EQ(thumbnail->extent.width, 256u);
    EXPECT_EQ(thumbnail->extent.height, 256u);
}

TEST(ViewResourceTableTest, ASizeChangeReplacesThatViewsResourcesRatherThanAddingASecond)
{
    ViewResourceTable<TestViewRecord> table;

    table.publish(makeKey(11, kWorldExtent), TestViewRecord{.id = 1, .extent = kWorldExtent});
    table.publish(makeKey(12, kThumbnailExtent), TestViewRecord{.id = 2, .extent = kThumbnailExtent});
    table.publish(makeKey(11, Extent2D{.width = 640, .height = 480}),
                  TestViewRecord{.id = 3, .extent = Extent2D{.width = 640, .height = 480}});

    ASSERT_EQ(table.size(), 2u);
    const TestViewRecord* resized = table.findForView(11);
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(resized->id, 3);
    EXPECT_EQ(resized->extent.width, 640u);
    EXPECT_EQ(table.findForView(12)->extent.width, 256u);

    // Looking up the resources *of that exact record* answers nothing now: the
    // View's resources are the new ones, not a second live set.
    EXPECT_EQ(table.find(makeKey(11, kWorldExtent)), nullptr);
    EXPECT_NE(table.find(makeKey(11, Extent2D{.width = 640, .height = 480})), nullptr);
}

TEST(ViewResourceTableTest, AViewThatWasNeverRecordedAnswersNothing)
{
    ViewResourceTable<TestViewRecord> table;

    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.findForView(11), nullptr);

    table.publish(makeKey(11, kWorldExtent), TestViewRecord{.id = 1, .extent = kWorldExtent});
    EXPECT_EQ(table.findForView(12), nullptr);

    table.clear();
    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.findForView(11), nullptr);
}

TEST(ViewResourceTableTest, RetainIfDropsOnlyTheViewsTheCriterionRejects)
{
    ViewResourceTable<TestViewRecord> table;

    table.publish(makeKey(11, kWorldExtent), TestViewRecord{.id = 1, .extent = kWorldExtent});
    table.publish(makeKey(12, kThumbnailExtent), TestViewRecord{.id = 2, .extent = kThumbnailExtent});
    table.publish(makeKey(13, kWorldExtent), TestViewRecord{.id = 3, .extent = kWorldExtent});

    table.retainIf([](SceneViewId viewId) { return viewId != 11; });

    ASSERT_EQ(table.size(), 2u);
    EXPECT_EQ(table.findForView(11), nullptr);
    // The Views the criterion accepted keep their own records, not a shifted
    // copy of a neighbour's.
    ASSERT_NE(table.findForView(12), nullptr);
    EXPECT_EQ(table.findForView(12)->id, 2);
    EXPECT_EQ(table.findForView(12)->extent.width, 256u);
    ASSERT_NE(table.findForView(13), nullptr);
    EXPECT_EQ(table.findForView(13)->id, 3);

    // Dropping everything is the same operation with a criterion that accepts
    // nothing, and clear() is still the whole-table reset it always was.
    table.retainIf([](SceneViewId) { return false; });
    EXPECT_TRUE(table.empty());
}

} // namespace
} // namespace ya

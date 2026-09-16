#include "Render3D/Common/RenderViewOutput.h"
#include "RHI/Core/RenderTexture.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(RenderViewOutputTableTest, PublishedViewsKeepIndependentHandles)
{
    RenderViewOutputTable table;
    ASSERT_TRUE(table.beginSubmission(0, 4u));

    auto colorA = std::make_shared<RenderTexture>();
    auto colorB = std::make_shared<RenderTexture>();

    const RenderViewOutput* viewA = table.publish(
        0,
        RenderViewOutput{
            .desc =
                {
                    .viewId      = 11,
                    .extent      = {.width = 1280, .height = 720},
                    .colorFormat = EFormat::R8G8B8A8_UNORM,
                    .depthFormat = EFormat::D32_SFLOAT,
                },
            .color = colorA,
        });
    const RenderViewOutput* viewB = table.publish(
        0,
        RenderViewOutput{
            .desc =
                {
                    .viewId      = 12,
                    .extent      = {.width = 512, .height = 512},
                    .colorFormat = EFormat::R16G16B16A16_SFLOAT,
                    .depthFormat = EFormat::D24_UNORM_S8_UINT,
                },
            .color = colorB,
        });

    ASSERT_NE(viewA, nullptr);
    ASSERT_NE(viewB, nullptr);
    EXPECT_NE(viewA, viewB);
    EXPECT_EQ(table.liveViewCount(0), 2u);
    EXPECT_EQ(table.find(0, 11), viewA);
    EXPECT_EQ(table.find(0, 12), viewB);
    EXPECT_EQ(viewA->color.get(), colorA.get());
    EXPECT_EQ(viewB->color.get(), colorB.get());
    EXPECT_NE(viewA->color.get(), viewB->color.get());
    EXPECT_EQ(viewA->desc.extent.width, 1280u);
    EXPECT_EQ(viewB->desc.extent.width, 512u);
    EXPECT_EQ(viewA->desc.colorFormat, EFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(viewB->desc.colorFormat, EFormat::R16G16B16A16_SFLOAT);
}

TEST(RenderViewOutputTableTest, PublishingSecondViewDoesNotRewriteFirst)
{
    RenderViewOutputTable table;
    ASSERT_TRUE(table.beginSubmission(0, 1u));

    auto colorA = std::make_shared<RenderTexture>();
    ASSERT_NE(table.publish(0,
                            RenderViewOutput{
                                .desc  = {.viewId = 1, .extent = {.width = 64, .height = 32}},
                                .color = colorA,
                            }),
              nullptr);

    auto colorB = std::make_shared<RenderTexture>();
    ASSERT_NE(table.publish(0,
                            RenderViewOutput{
                                .desc  = {.viewId = 2, .extent = {.width = 8, .height = 8}},
                                .color = colorB,
                            }),
              nullptr);

    const RenderViewOutput* first = table.find(0, 1);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->color.get(), colorA.get());
    EXPECT_EQ(first->desc.extent.width, 64u);
    EXPECT_EQ(first->desc.extent.height, 32u);
}

TEST(RenderViewOutputTableTest, NewTokenRewindsLiveOutputs)
{
    RenderViewOutputTable table;
    ASSERT_TRUE(table.beginSubmission(0, 1u));
    ASSERT_NE(table.publish(0, RenderViewOutput{.desc = {.viewId = 3, .extent = {.width = 16, .height = 16}}}),
              nullptr);
    EXPECT_EQ(table.liveViewCount(0), 1u);

    ASSERT_TRUE(table.beginSubmission(0, 2u));
    EXPECT_EQ(table.liveViewCount(0), 0u);
    EXPECT_EQ(table.find(0, 3), nullptr);
}

TEST(RenderViewOutputTableTest, OtherFlightPublishDoesNotDropOutputs)
{
    RenderViewOutputTable table;
    ASSERT_TRUE(table.beginSubmission(0, 9u));
    ASSERT_TRUE(table.beginSubmission(1, 9u));

    auto color0 = std::make_shared<RenderTexture>();
    ASSERT_NE(table.publish(0, RenderViewOutput{.desc = {.viewId = 21}, .color = color0}), nullptr);
    ASSERT_NE(table.publish(1, RenderViewOutput{.desc = {.viewId = 22}}), nullptr);

    EXPECT_EQ(table.find(0, 21)->color.get(), color0.get());
    EXPECT_EQ(table.find(1, 21), nullptr);
    EXPECT_EQ(table.liveViewCount(0), 1u);
    EXPECT_EQ(table.liveViewCount(1), 1u);
}

TEST(RenderViewOutputTableTest, PublishRequiresSubmissionAndViewId)
{
    RenderViewOutputTable table;
    EXPECT_EQ(table.publish(0, RenderViewOutput{.desc = {.viewId = 1}}), nullptr);
    ASSERT_TRUE(table.beginSubmission(0, 1u));
    EXPECT_EQ(table.publish(0, RenderViewOutput{}), nullptr);
    EXPECT_EQ(table.find(0, 0), nullptr);
    EXPECT_EQ(table.get(0, 0), nullptr);
}

} // namespace ya

#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/ViewResourceKey.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "RHI/Core/RenderTexture.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace ya
{

class ForwardRenderPipelineTestAccess
{
  public:
    static void publishViewResources(ForwardRenderPipeline&  pipeline,
                                     const RenderViewOutput& output,
                                     Extent2D                extent,
                                     FRenderFeatureMask      features)
    {
        pipeline.publishViewResources(output, extent, features);
    }
};

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

std::shared_ptr<RenderTexture> makeAttachment()
{
    return std::make_shared<RenderTexture>();
}

RenderViewOutput makeOutput(SceneViewId                       viewId,
                            Extent2D                          extent,
                            const std::shared_ptr<RenderTexture>& color,
                            const std::shared_ptr<RenderTexture>& depth)
{
    RenderViewOutput output;
    output.desc.viewId      = viewId;
    output.desc.extent      = extent;
    output.desc.colorFormat = EFormat::R16G16B16A16_SFLOAT;
    output.desc.depthFormat = EFormat::D32_SFLOAT;
    output.color            = color;
    output.depth            = depth;
    output.entityId         = makeAttachment();
    return output;
}

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

/// The acceptance evidence for this batch: one tick records a world View and a
/// thumbnail View of different sizes, and each keeps its own attachments.
TEST(ForwardRenderPipelineTest, TwoViewsKeepTheirOwnAttachmentsAndExtents)
{
    ForwardRenderPipeline pipeline;

    const auto worldColor      = makeAttachment();
    const auto worldDepth      = makeAttachment();
    const auto thumbnailColor  = makeAttachment();
    const auto thumbnailDepth  = makeAttachment();
    const auto editorColor     = makeAttachment();
    const auto editorDepth     = makeAttachment();

    ForwardRenderPipelineTestAccess::publishViewResources(
        pipeline, makeOutput(11, kWorldExtent, worldColor, worldDepth), kWorldExtent, kGameFeatures);
    ForwardRenderPipelineTestAccess::publishViewResources(
        pipeline, makeOutput(12, kThumbnailExtent, thumbnailColor, thumbnailDepth), kThumbnailExtent, kGameFeatures);
    // Same size as the world View, different identity: still its own resources.
    ForwardRenderPipelineTestAccess::publishViewResources(
        pipeline, makeOutput(13, kWorldExtent, editorColor, editorDepth), kWorldExtent, kEditorFeatures);

    const ForwardViewResources* world = pipeline.viewResourcesFor(11);
    const ForwardViewResources* thumbnail = pipeline.viewResourcesFor(12);
    const ForwardViewResources* editor = pipeline.viewResourcesFor(13);
    ASSERT_NE(world, nullptr);
    ASSERT_NE(thumbnail, nullptr);
    ASSERT_NE(editor, nullptr);

    EXPECT_EQ(world->colorOwner, worldColor);
    EXPECT_EQ(world->depthOwner, worldDepth);
    EXPECT_EQ(world->extent.width, 1280u);
    EXPECT_EQ(thumbnail->colorOwner, thumbnailColor);
    EXPECT_EQ(thumbnail->extent.width, 256u);
    EXPECT_EQ(editor->colorOwner, editorColor);
    EXPECT_NE(world->colorOwner, thumbnail->colorOwner);
    EXPECT_NE(world->colorOwner, editor->colorOwner);

    // The identity-carrying queries answer about the named View only.
    EXPECT_EQ(pipeline.getViewDepthImageShared(11), worldDepth);
    EXPECT_EQ(pipeline.getViewDepthImageShared(12), thumbnailDepth);
    EXPECT_EQ(pipeline.getViewDepthImageShared(13), editorDepth);
    EXPECT_EQ(pipeline.getViewDepthImageShared(99), nullptr);
    EXPECT_EQ(pipeline.viewResourcesFor(99), nullptr);

    RenderTargetCatalog catalog;
    pipeline.appendRenderTargetEntries(catalog);
    // Three View targets, one per recorded View, plus the pipeline's shadow map.
    ASSERT_EQ(catalog.entries.size(), 4u);
    EXPECT_EQ(catalog.entries[3].owner, RenderTargetCatalog::Entry::EOwner::ForwardShadow);
    EXPECT_EQ(catalog.entries[0].colorAttachments[0], worldColor);
    EXPECT_EQ(catalog.entries[0].extent.width, 1280u);
    EXPECT_EQ(catalog.entries[1].extent.width, 256u);
    EXPECT_EQ(catalog.entries[2].extent.width, 1280u);

    // The world View resizes. It is still one View: its entry is replaced, the
    // thumbnail keeps its own attachments and extent, and no third entry ap-
    // pears.
    const auto resizedColor = makeAttachment();
    const auto resizedDepth = makeAttachment();
    ForwardRenderPipelineTestAccess::publishViewResources(
        pipeline, makeOutput(11, Extent2D{.width = 640, .height = 480}, resizedColor, resizedDepth),
        Extent2D{.width = 640, .height = 480}, kGameFeatures);

    const ForwardViewResources* resized = pipeline.viewResourcesFor(11);
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(resized->colorOwner, resizedColor);
    EXPECT_EQ(resized->extent.width, 640u);
    EXPECT_EQ(resized->extent.height, 480u);
    EXPECT_EQ(pipeline.viewResourcesFor(12)->colorOwner, thumbnailColor);
    EXPECT_EQ(pipeline.viewResourcesFor(12)->extent.width, 256u);
    EXPECT_EQ(pipeline.viewResourcesFor(13)->colorOwner, editorColor);

    RenderTargetCatalog resizedCatalog;
    pipeline.appendRenderTargetEntries(resizedCatalog);
    ASSERT_EQ(resizedCatalog.entries.size(), 4u);
    EXPECT_EQ(resizedCatalog.entries[0].extent.width, 640u);
    EXPECT_EQ(resizedCatalog.entries[1].extent.width, 256u);
    EXPECT_EQ(resizedCatalog.entries[2].extent.width, 1280u);
}

} // namespace
} // namespace ya

#include "Render3D/Common/RenderTargetCatalog.h"
#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewResourceKey.h"
#include "Render3D/Forward/ForwardRenderPipeline.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/RenderFrameData.h"
#include "Render3D/Services/PipelineCoordinator.h"
#include "RHI/Core/RenderTexture.h"
#include "Scene/Core/Scene.h"

#include "RenderTestAccess.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>

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

/// A Scene view task as the plan carries it: the declaration, whose identity is
/// what a tick says about a View existing. Only the fields the criterion reads
/// are meaningful here.
SceneViewTask forwardPlanTask(SceneViewId viewId)
{
    SceneViewTask task;
    task.desc.viewId = viewId;
    task.output.viewId = viewId;
    return task;
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

/// The acceptance evidence for this batch: after a tick records View A and View
/// B, a tick that declares only B drops A -- its entry, its queries and the
/// attachments only A's entry owned -- and leaves B exactly as it was.
TEST(ForwardRenderPipelineTest, AViewTheNextTickDoesNotDeclareIsEvictedAndTheOtherIsKept)
{
    ForwardRenderPipeline pipeline;

    auto worldColor   = makeAttachment();
    auto worldDepth   = makeAttachment();
    auto previewColor = makeAttachment();
    auto previewDepth = makeAttachment();

    // One tick records both Views.
    ForwardRenderPipelineTestAccess::publishViewResources(
        pipeline, makeOutput(11, kWorldExtent, worldColor, worldDepth), kWorldExtent, kGameFeatures);
    ForwardRenderPipelineTestAccess::publishViewResources(
        pipeline, makeOutput(12, kThumbnailExtent, previewColor, previewDepth), kThumbnailExtent, kGameFeatures);

    // Weak references, and the test's own handles dropped, so "the entry is
    // gone" is provably also "the attachment was released": the pipeline's table
    // is the only owner left.
    std::weak_ptr<RenderTexture> previewColorRef = previewColor;
    std::weak_ptr<RenderTexture> previewDepthRef = previewDepth;
    previewColor.reset();
    previewDepth.reset();

    // The next tick declares the world View and nothing else.
    SceneRenderPlan plan;
    plan.viewTasks.push_back(forwardPlanTask(11));

    ForwardRenderPipelineTestAccess::reconcilePublishedViews(pipeline, plan);

    // A is gone: no entry, no attachment behind the identity-carrying queries,
    // and no second owner keeping its images alive.
    EXPECT_EQ(pipeline.viewResourcesFor(12), nullptr);
    EXPECT_EQ(pipeline.getViewDepthImageShared(12), nullptr);
    EXPECT_EQ(pipeline.getEntityIdImageShared(12), nullptr);
    EXPECT_TRUE(previewColorRef.expired());
    EXPECT_TRUE(previewDepthRef.expired());

    // B is untouched: the same entry with the same resource owners and extent,
    // not a re-published or shifted copy.
    const ForwardViewResources* world = pipeline.viewResourcesFor(11);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->colorOwner, worldColor);
    EXPECT_EQ(world->depthOwner, worldDepth);
    EXPECT_EQ(world->extent.width, 1280u);
    EXPECT_EQ(pipeline.getViewDepthImageShared(11), worldDepth);

    // Reconciling again in the same tick is the same answer: the criterion is
    // the tick's declarations, so a second family's recording of them (or a
    // repeated call) cannot drop a View the tick declares.
    ForwardRenderPipelineTestAccess::reconcilePublishedViews(pipeline, plan);
    ASSERT_NE(pipeline.viewResourcesFor(11), nullptr);
    EXPECT_EQ(pipeline.viewResourcesFor(11)->colorOwner, worldColor);

    // The panel's rows follow: one row per View, and the row names its View.
    RenderTargetCatalog catalog;
    pipeline.appendRenderTargetEntries(catalog);
    ASSERT_EQ(catalog.entries.size(), 2u);
    EXPECT_EQ(catalog.entries[0].owner, RenderTargetCatalog::Entry::EOwner::ForwardView);
    EXPECT_EQ(catalog.entries[0].viewId, 11u);
    EXPECT_EQ(catalog.entries[0].colorAttachments[0], worldColor);
    EXPECT_EQ(catalog.entries[1].owner, RenderTargetCatalog::Entry::EOwner::ForwardShadow);
    EXPECT_EQ(catalog.entries[1].viewId, 0u);
}

/// The case a per-family criterion would have missed: a tick that declares no
/// View at all (the editor's viewport tab closed) records no family, so nothing
/// would reconcile if the eviction were driven by the family list. The tick's
/// declarations are the criterion, and an empty declaration list is the answer
/// "nothing this pipeline published is still a View of this tick".
TEST(ForwardRenderPipelineTest, ATickThatDeclaresNoViewLeavesNothingPublished)
{
    ForwardRenderPipeline pipeline;

    auto color = makeAttachment();
    auto depth = makeAttachment();
    ForwardRenderPipelineTestAccess::publishViewResources(
        pipeline, makeOutput(11, kWorldExtent, color, depth), kWorldExtent, kGameFeatures);

    std::weak_ptr<RenderTexture> colorRef = color;
    color.reset();
    depth.reset();

    ForwardRenderPipelineTestAccess::reconcilePublishedViews(pipeline, SceneRenderPlan{});

    EXPECT_EQ(pipeline.viewResourcesFor(11), nullptr);
    EXPECT_EQ(pipeline.getViewDepthImageShared(11), nullptr);
    EXPECT_TRUE(colorRef.expired());
}

/// The wiring evidence for dropping a View the tick stopped declaring: it is
/// `RenderDeviceState::prepareFrameRecord` -- the renderer's pre-record step --
/// that reconciles against this tick's plan. The sibling case above covers the
/// pipeline's half with `reconcilePublishedViews()` called straight from the test;
/// this one goes through the device, so removing the reconcile call from
/// `RenderDeviceState.Frame.cpp` turns it red.
///
/// Boundary: this pins that `prepareFrameRecord` reconciles against the plan it is
/// handed, and that the call runs. It does not pin where that step sits in a
/// frame's recording order -- that is the application's order now, and
/// `RuntimeRenderContextTest` watches the part of it a test can reach without a
/// real command buffer.
TEST(RenderDeviceStateTest, PrepareFrameRecordDropsTheViewsTheTickStopsDeclaring)
{
    Scene scene("Authoring");

    auto              pipeline = std::make_shared<ForwardRenderPipeline>();
    RenderDeviceState device;
    RenderDeviceStateTestAccess::installActivePipeline(device, pipeline);
    // The seam worked: the device is asking this pipeline, not a real one.
    EXPECT_EQ(device.getRenderPipeline(), PipelineCoordinator::ERenderPipeline::Forward);

    auto worldColor   = makeAttachment();
    auto worldDepth   = makeAttachment();
    auto previewColor = makeAttachment();
    auto previewDepth = makeAttachment();

    // The world View's declaration is kept as a named value because the case
    // reads its entityId attachment back below.
    const RenderViewOutput worldOutput   = makeOutput(11, kWorldExtent, worldColor, worldDepth);
    const auto             worldEntityId = worldOutput.entityId;

    // Weak references taken before the preview View is published, and the
    // test's own handles dropped right after: the preview declaration is a
    // temporary and the local handles are the only owners outside the
    // pipeline's table, so "this View is gone" is provably also "its
    // attachments were released" rather than merely "the lookup misses".
    std::weak_ptr<RenderTexture> previewColorRef = previewColor;
    std::weak_ptr<RenderTexture> previewDepthRef = previewDepth;

    ForwardRenderPipelineTestAccess::publishViewResources(*pipeline, worldOutput, kWorldExtent, kGameFeatures);
    ForwardRenderPipelineTestAccess::publishViewResources(
        *pipeline, makeOutput(12, kThumbnailExtent, previewColor, previewDepth), kThumbnailExtent, kGameFeatures);
    previewColor.reset();
    previewDepth.reset();

    // This tick declares the world View and nothing else, and it declares it
    // through the scheduler the host uses: the criterion is the tick's own
    // declarations, not a list the test keeps on the side.
    SceneRenderScheduler scheduler;
    scheduler.beginTick(1);
    ASSERT_TRUE(scheduler.submit(SceneViewDesc{
        .scene      = &scene,
        .viewId     = 11,
        .outputRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
    }));

    RenderFramePlan plan;
    plan.sceneRender = buildSceneSnapshots(scheduler.seal(),
                                           [](Scene&) { return std::make_shared<const SceneSnapshot>(); });
    ASSERT_EQ(plan.sceneRender.plan().viewTasks.size(), 1u);

    // The step itself is reached directly: it is one of the steps an
    // application's recording order calls, so it is public on the renderer.
    device.prepareFrameRecord(plan);

    // The View this tick stopped declaring is gone: no entry, nothing behind the
    // identity-carrying queries, and no second owner keeping its images alive.
    EXPECT_EQ(pipeline->viewResourcesFor(12), nullptr);
    EXPECT_EQ(pipeline->getViewDepthImageShared(12), nullptr);
    EXPECT_EQ(pipeline->getEntityIdImageShared(12), nullptr);
    EXPECT_TRUE(previewColorRef.expired());
    EXPECT_TRUE(previewDepthRef.expired());

    // The View the tick still declares is untouched, field for field: the same
    // owners and extent, not a re-published or shifted copy.
    const ForwardViewResources* world = pipeline->viewResourcesFor(11);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->colorOwner, worldColor);
    EXPECT_EQ(world->depthOwner, worldDepth);
    EXPECT_EQ(world->resolveOwner, nullptr);
    EXPECT_EQ(world->entityIdOwner, worldEntityId);
    EXPECT_EQ(world->extent.width, kWorldExtent.width);
    EXPECT_EQ(world->extent.height, kWorldExtent.height);
    EXPECT_EQ(pipeline->getViewDepthImageShared(11), worldDepth);
    EXPECT_EQ(pipeline->getEntityIdImageShared(11), worldEntityId);
}

} // namespace
} // namespace ya

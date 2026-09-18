#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewPersistentResourceKey.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

namespace ya
{
namespace
{

/// A declaration the scheduler may accept: an owned Scene, a view id, and a
/// View rect that describes at least one pixel.
SceneViewDesc makeView(Scene* scene, SceneViewId viewId)
{
    return SceneViewDesc{
        .scene        = scene,
        .viewId       = viewId,
        .viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
    };
}

} // namespace

TEST(ViewFamilyRendererTest, DualViewExportNamesStayUniqueInOneFamilyGraph)
{
    const std::string colorA = makeViewGraphName("Deferred.Viewport.Color", 11);
    const std::string colorB = makeViewGraphName("Deferred.Viewport.Color", 12);
    const std::string bloomA = makeViewGraphName("Bloom.Output", 11);
    const std::string postA  = makeViewGraphName("Postprocessing.Output", 11);

    EXPECT_NE(colorA, colorB);
    EXPECT_NE(colorA, bloomA);
    EXPECT_NE(colorA, postA);
    EXPECT_EQ(colorA, "Deferred.Viewport.Color.view11");
    EXPECT_EQ(colorB, "Deferred.Viewport.Color.view12");
    EXPECT_EQ(makeViewGraphName("Deferred GBuffer", 11), "Deferred GBuffer.view11");
}

TEST(ViewFamilyRendererTest, SameSceneDualViewSealsOneFamilyPlan)
{
    Scene scene("Family");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(7);

    ASSERT_TRUE(scheduler.submit(makeView(&scene, 11)));
    ASSERT_TRUE(scheduler.submit(makeView(&scene, 12)));

    const ExtractedSceneRender extracted = buildSceneSnapshots(
        scheduler.seal(), [](Scene&) { return std::make_shared<const SceneSnapshot>(); });
    const SceneRenderPlan& plan = extracted.plan();
    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.viewFamilies.front().viewportTaskIndices.size(), 2u);
    EXPECT_EQ(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
}

TEST(ViewFamilyRendererTest, DualSceneSealsTwoFamilyPlans)
{
    Scene sceneA("DualA");
    Scene sceneB("DualB");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(8);

    ASSERT_TRUE(scheduler.submit(makeView(&sceneA, 11)));
    ASSERT_TRUE(scheduler.submit(makeView(&sceneB, 21)));

    const ExtractedSceneRender extracted = buildSceneSnapshots(
        scheduler.seal(), [](Scene&) { return std::make_shared<const SceneSnapshot>(); });
    const SceneRenderPlan& plan = extracted.plan();
    ASSERT_EQ(plan.viewFamilies.size(), 2u);
    EXPECT_NE(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
}

TEST(ViewFamilyRendererTest, ViewFamilyRenderResultPublishesWithoutPipelineGetters)
{
    RenderViewOutputTable table;
    ASSERT_TRUE(table.beginSubmission(0, 1));

    ViewFamilyRenderResult family;
    family.views.push_back(RenderViewOutput{.desc = {.viewId = 11, .extent = {.width = 128, .height = 72}}});
    family.views.push_back(RenderViewOutput{.desc = {.viewId = 12, .extent = {.width = 64, .height = 64}}});

    for (RenderViewOutput& output : family.views) {
        ASSERT_NE(table.publish(0, std::move(output)), nullptr);
    }

    const RenderViewOutput* a = table.find(0, 11);
    const RenderViewOutput* b = table.find(0, 12);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->desc.extent.width, 128u);
    EXPECT_EQ(b->desc.extent.width, 64u);
    EXPECT_NE(a, b);
}

template<typename Pipeline>
concept PipelineHasTick = requires(Pipeline& pipeline, const RenderPipelineFrameContext& frame) {
    pipeline.tick(frame);
};

template<typename Pipeline>
concept PipelineRecordsFamily = requires(Pipeline& pipeline, const ViewFamilyRecordContext& ctx) {
    pipeline.recordFamily(ctx);
};

TEST(ViewFamilyRendererTest, PipelineRecordsFamiliesNotTicks)
{
    static_assert(!PipelineHasTick<IRenderPipeline>);
    static_assert(PipelineRecordsFamily<IRenderPipeline>);
    static_assert(std::is_same_v<ISceneViewFamilyRenderer, IRenderPipeline>);
}

} // namespace ya

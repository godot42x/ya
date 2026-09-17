#include "Render3D/Common/IRenderPipeline.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewPersistentResourceKey.h"

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

namespace ya
{

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
    SceneRenderScheduler scheduler;
    scheduler.beginFrame(7);

    SceneRenderRequest viewA{.sceneId = 3, .viewId = 11};
    viewA.buildSnapshot = [] {
        return std::make_shared<const SceneFrameSnapshot>();
    };
    SceneRenderRequest viewB{.sceneId = 3, .viewId = 12};
    viewB.buildSnapshot = viewA.buildSnapshot;

    ASSERT_TRUE(scheduler.submit(viewA));
    ASSERT_TRUE(scheduler.submit(viewB));

    const SceneRenderPlan plan = scheduler.seal();
    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.viewFamilies.front().viewportTaskIndices.size(), 2u);
    EXPECT_EQ(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
}

TEST(ViewFamilyRendererTest, DualSceneSealsTwoFamilyPlans)
{
    SceneRenderScheduler scheduler;
    scheduler.beginFrame(8);

    SceneRenderRequest sceneA{.sceneId = 3, .viewId = 11};
    sceneA.buildSnapshot = [] {
        return std::make_shared<const SceneFrameSnapshot>();
    };
    SceneRenderRequest sceneB{.sceneId = 4, .viewId = 21};
    sceneB.buildSnapshot = [] {
        return std::make_shared<const SceneFrameSnapshot>();
    };

    ASSERT_TRUE(scheduler.submit(sceneA));
    ASSERT_TRUE(scheduler.submit(sceneB));

    const SceneRenderPlan plan = scheduler.seal();
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

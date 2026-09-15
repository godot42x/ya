#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/RenderRuntime.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <type_traits>

namespace ya
{
namespace
{

TEST(RenderRuntimeSnapshotTest, EmptyRuntimePublishesEmptyViewportResources)
{
    RenderRuntime runtime;

    const RenderViewportSnapshot viewport = runtime.buildViewportSnapshot();
    const RenderTargetCatalog    targets  = runtime.buildRenderTargetCatalog();

    EXPECT_EQ(viewport.viewportImageOwner, nullptr);
    EXPECT_EQ(viewport.viewportImageView, nullptr);
    EXPECT_FALSE(viewport.bPostprocessingEnabled);
    ASSERT_NE(viewport.debugCatalog, nullptr);
    EXPECT_FALSE(viewport.debugCatalog->categories.empty());
    EXPECT_TRUE(viewport.debugCatalog->slots.empty());
    EXPECT_TRUE(viewport.debugCatalog->groups.empty());
    EXPECT_TRUE(viewport.debugImages.empty());
    EXPECT_TRUE(targets.entries.empty());
}

TEST(RenderRuntimeSnapshotTest, FrameInputGroupsCameraViewDisplayPresent)
{
    static_assert(std::is_same_v<decltype(RenderRuntime::FrameInput{}.camera), CameraFrameInput>);
    static_assert(std::is_same_v<decltype(RenderRuntime::FrameInput{}.viewCompose), ViewComposeInput>);
    static_assert(std::is_same_v<decltype(RenderRuntime::FrameInput{}.displayCompose), DisplayComposeInput>);
    static_assert(std::is_same_v<decltype(RenderRuntime::FrameInput{}.present), PresentFrameInput>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.camera), CameraFrameInput>);

    const glm::mat4 view       = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(1.0f, 1.5f, 0.1f, 100.0f);

    RenderRuntime::FrameInput input{
        .camera = {
            .deltaTime      = 0.016f,
            .view           = view,
            .projection     = projection,
            .viewProjection = makeCameraViewProjection(projection, view),
            .viewportRect   = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
        },
        .viewCompose    = {},
        .displayCompose = {},
        .present        = {.surface = nullptr, .imageIndex = -1},
    };

    EXPECT_FLOAT_EQ(input.camera.deltaTime, 0.016f);
    EXPECT_EQ(input.camera.viewProjection, makeCameraViewProjection(projection, view));
    EXPECT_TRUE(input.camera.hasOffscreenExtent());
    EXPECT_TRUE(input.viewCompose.empty());
    EXPECT_TRUE(input.displayCompose.extensions.empty());
    EXPECT_EQ(input.present.surface, nullptr);
    EXPECT_EQ(input.present.imageIndex, -1);
}

TEST(RenderRuntimeSnapshotTest, RenderFrameDataSeparatesSceneAndViewOwnership)
{
    static_assert(std::is_base_of_v<SceneFrameSnapshot, RenderFrameData>);

    RenderFrameData frame;
    frame.view = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    frame.drawBuckets.staticMeshes.pbrDrawItems.resize(1);
    frame.skinningPalettes.resize(1);

    SceneFrameSnapshot& sceneSnapshot = frame;
    EXPECT_EQ(sceneSnapshot.drawBuckets.staticMeshes.pbrDrawItems.size(), 1u);
    EXPECT_EQ(sceneSnapshot.skinningPalettes.size(), 1u);
    EXPECT_EQ(frame.view[3][0], 4.0f);

    sceneSnapshot.clearScene();
    EXPECT_TRUE(frame.drawBuckets.staticMeshes.pbrDrawItems.empty());
    EXPECT_TRUE(frame.skinningPalettes.empty());
    EXPECT_EQ(frame.view[3][0], 4.0f);
}

TEST(RenderRuntimeSnapshotTest, SceneSchedulerDeduplicatesSnapshotPerScene)
{
    SceneRenderScheduler scheduler;
    scheduler.beginFrame(42);

    int buildCalls = 0;
    auto makeRequest = [&](SceneId sceneId, SceneViewId viewId)
    {
        SceneRenderRequest request;
        request.sceneId = sceneId;
        request.viewId = viewId;
        request.buildSnapshot = [&, sceneId]()
        {
            ++buildCalls;
            auto snapshot = std::make_shared<SceneFrameSnapshot>();
            snapshot->numPointLights = static_cast<uint32_t>(sceneId);
            return std::shared_ptr<const SceneFrameSnapshot>(std::move(snapshot));
        };
        return request;
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(1, 11)));
    ASSERT_TRUE(scheduler.submit(makeRequest(1, 12)));
    ASSERT_TRUE(scheduler.submit(makeRequest(2, 21)));

    const SceneRenderPlan plan = scheduler.seal();
    ASSERT_EQ(plan.frameId, 42u);
    ASSERT_EQ(plan.snapshots.size(), 2u);
    ASSERT_EQ(plan.viewportTasks.size(), 3u);
    EXPECT_EQ(buildCalls, 2);
    EXPECT_EQ(plan.viewportTasks[0].snapshotIndex, plan.viewportTasks[1].snapshotIndex);
    EXPECT_NE(plan.viewportTasks[0].snapshotIndex, plan.viewportTasks[2].snapshotIndex);
    EXPECT_EQ(plan.snapshots[plan.viewportTasks[0].snapshotIndex].snapshot,
              plan.snapshots[plan.viewportTasks[1].snapshotIndex].snapshot);
    EXPECT_NE(plan.snapshots[plan.viewportTasks[0].snapshotIndex].snapshot,
              plan.snapshots[plan.viewportTasks[2].snapshotIndex].snapshot);
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]),
              plan.snapshotFor(plan.viewportTasks[1]));
    EXPECT_FALSE(scheduler.isFrameOpen());
}

TEST(RenderRuntimeSnapshotTest, SceneSchedulerRebuildsSnapshotWhenSceneRevisionChanges)
{
    SceneRenderScheduler scheduler;
    scheduler.beginFrame(9);

    int buildCalls = 0;
    auto makeRequest = [&](uint64_t revision, SceneViewId viewId)
    {
        SceneRenderRequest request;
        request.sceneId = 5;
        request.sceneRevision = revision;
        request.viewId = viewId;
        request.buildSnapshot = [&buildCalls]
        {
            ++buildCalls;
            return std::make_shared<const SceneFrameSnapshot>();
        };
        return request;
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(1, 51)));
    ASSERT_TRUE(scheduler.submit(makeRequest(1, 52)));
    ASSERT_TRUE(scheduler.submit(makeRequest(2, 53)));

    const SceneRenderPlan plan = scheduler.seal();
    ASSERT_EQ(buildCalls, 2);
    ASSERT_EQ(plan.snapshots.size(), 2u);
    ASSERT_EQ(plan.viewportTasks[0].snapshotIndex, plan.viewportTasks[1].snapshotIndex);
    EXPECT_NE(plan.viewportTasks[0].snapshotIndex, plan.viewportTasks[2].snapshotIndex);
    EXPECT_EQ(plan.snapshots[0].sceneRevision, 1u);
    EXPECT_EQ(plan.snapshots[1].sceneRevision, 2u);
}

TEST(RenderRuntimeSnapshotTest, SceneRenderPlanRejectsInvalidSnapshotIndex)
{
    SceneRenderPlan plan;
    SceneViewportTask task;
    EXPECT_EQ(plan.snapshotFor(task), nullptr);
}

TEST(RenderRuntimeSnapshotTest, SceneRenderPlanRejectsSnapshotMetadataMismatch)
{
    SceneRenderPlan plan;
    plan.snapshots.push_back(SceneSnapshotEntry{
        .sceneId = 7,
        .sceneRevision = 3,
        .snapshot = std::make_shared<const SceneFrameSnapshot>(),
    });

    SceneViewportTask task;
    task.sceneId = 7;
    task.sceneRevision = 4;
    task.snapshotIndex = 0;
    EXPECT_EQ(plan.snapshotFor(task), nullptr);
}

TEST(RenderRuntimeSnapshotTest, SceneSchedulerRejectsRequestsOutsideFrame)
{
    SceneRenderScheduler scheduler;
    SceneRenderRequest request;
    request.sceneId = 1;
    request.viewId = 1;
    request.buildSnapshot = [] { return std::make_shared<const SceneFrameSnapshot>(); };

    EXPECT_FALSE(scheduler.submit(request));
    scheduler.beginFrame(7);
    request.sceneId = 0;
    EXPECT_FALSE(scheduler.submit(request));
    request.sceneId = 1;
    request.viewId = 1;
    EXPECT_TRUE(scheduler.submit(request));
    scheduler.clearFrame();
    EXPECT_EQ(scheduler.pendingRequestCount(), 0u);
}

} // namespace
} // namespace ya

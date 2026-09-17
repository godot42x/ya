#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewCompose.h"
#include "Render3D/RenderFrameData.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/RenderFrameCoordinator.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <type_traits>
#include <vector>

namespace ya
{
namespace
{

TEST(RenderRuntimeSnapshotTest, EmptyDevicePublishesEmptyViewportResources)
{
    RenderDeviceState device;
    RenderFrameCoordinator coordinator(device);

    EXPECT_EQ(device.getLiveSubmission(0), nullptr);
    EXPECT_EQ(device.getLiveSubmission(MAX_FLIGHTS_IN_FLIGHT), nullptr);
    EXPECT_EQ(device.getViewOutput(1), nullptr);

    const RenderViewportSnapshot viewport = device.buildViewportSnapshot();
    const RenderTargetCatalog    targets  = device.buildRenderTargetCatalog();

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

TEST(RenderRuntimeSnapshotTest, RenderFramePlanGroupsCameraViewDisplayPresent)
{
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.camera), CameraFrameInput>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.sceneRender), SceneRenderPlanInput>);
    static_assert(std::is_same_v<decltype(SceneRenderPlanInput{}.views), std::vector<SceneViewRecording>>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.viewCompose), ViewComposeInput>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.displayCompose), DisplayComposeInput>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.present), PresentFrameInput>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.derivedScene), Scene*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.camera), CameraFrameInput>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.submission), RenderSubmission*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.view), RenderViewRecordingContext>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.derivedScene), Scene*>);
    static_assert(std::is_same_v<decltype(ViewFamilyRecordContext{}.derivedScene), Scene*>);

    const glm::mat4 view       = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(1.0f, 1.5f, 0.1f, 100.0f);

    RenderFramePlan plan{
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
        .derivedScene   = nullptr,
    };

    EXPECT_FLOAT_EQ(plan.camera.deltaTime, 0.016f);
    EXPECT_EQ(plan.camera.viewProjection, makeCameraViewProjection(projection, view));
    EXPECT_TRUE(plan.camera.hasOffscreenExtent());
    EXPECT_TRUE(plan.viewCompose.empty());
    EXPECT_TRUE(plan.viewCompose.insets.empty());
    EXPECT_TRUE(plan.displayCompose.extensions.empty());
    EXPECT_EQ(plan.present.surface, nullptr);
    EXPECT_EQ(plan.present.imageIndex, -1);
    EXPECT_TRUE(plan.sceneRender.empty());
    EXPECT_EQ(plan.derivedScene, nullptr);
}

TEST(RenderRuntimeSnapshotTest, EmptySceneRenderIsUiOnlyFrame)
{
    const RenderFramePlan plan{};
    EXPECT_TRUE(plan.sceneRender.empty());
    EXPECT_FALSE(plan.sceneRender.complete());
    EXPECT_EQ(plan.derivedScene, nullptr);
}

TEST(RenderRuntimeSnapshotTest, RenderFrameDataSeparatesSceneAndViewOwnership)
{
    static_assert(!std::is_base_of_v<SceneFrameSnapshot, RenderFrameData>);
    static_assert(std::is_same_v<decltype(SceneFrameSnapshot{}.directionalLightSource), SceneDirectionalLightData>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(SceneFrameSnapshot{}.pointLightSources[0])>,
                                 ScenePointLightData>);
    static_assert(!std::is_same_v<decltype(SceneFrameSnapshot{}.directionalLightSource),
                                  FrameContext::DirectionalLightData>);

    RenderFrameData frame;
    frame.view = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    auto sceneSnapshot = std::make_shared<SceneFrameSnapshot>();
    sceneSnapshot->skinningPalettes.resize(1);
    sceneSnapshot->drawBuckets.staticMeshes.pbrDrawItems.resize(1);
    frame.sceneSnapshot = sceneSnapshot;
    frame.drawBuckets.staticMeshes.pbrDrawItems.source =
        &sceneSnapshot->drawBuckets.staticMeshes.pbrDrawItems;
    frame.drawBuckets.staticMeshes.pbrDrawItems.order = {0};

    EXPECT_EQ(frame.sceneSnapshot.get(), sceneSnapshot.get());
    EXPECT_EQ(frame.drawBuckets.staticMeshes.pbrDrawItems.size(), 1u);
    EXPECT_EQ(frame.sceneSnapshot->skinningPalettes.size(), 1u);
    EXPECT_EQ(frame.view[3][0], 4.0f);
    EXPECT_EQ(frame.sceneSnapshot->directionalLightSource.direction, glm::vec3(0.0f, 0.0f, -1.0f));

    frame.clear();
    EXPECT_FALSE(frame.sceneSnapshot);
    EXPECT_TRUE(frame.drawBuckets.staticMeshes.pbrDrawItems.empty());
    EXPECT_EQ(frame.view[3][0], 4.0f);

    auto sharedSnapshot = std::make_shared<const SceneFrameSnapshot>();
    RenderFrameData viewA;
    RenderFrameData viewB;
    viewA.sceneSnapshot = sharedSnapshot;
    viewB.sceneSnapshot = sharedSnapshot;
    viewA.drawBuckets.staticMeshes.pbrDrawItems.source =
        &sceneSnapshot->drawBuckets.staticMeshes.pbrDrawItems;
    viewB.drawBuckets.staticMeshes.pbrDrawItems.source =
        &sceneSnapshot->drawBuckets.staticMeshes.pbrDrawItems;
    viewA.drawBuckets.staticMeshes.pbrDrawItems.order = {2, 0, 1};
    viewB.drawBuckets.staticMeshes.pbrDrawItems.order = {1, 2, 0};
    EXPECT_EQ(viewA.sceneSnapshot.get(), viewB.sceneSnapshot.get());
    EXPECT_EQ(viewA.sceneSnapshot.use_count(), 3);
    EXPECT_EQ(viewA.drawBuckets.staticMeshes.pbrDrawItems.source,
              viewB.drawBuckets.staticMeshes.pbrDrawItems.source);
    EXPECT_NE(viewA.drawBuckets.staticMeshes.pbrDrawItems.order.data(),
              viewB.drawBuckets.staticMeshes.pbrDrawItems.order.data());
    EXPECT_EQ(viewA.drawBuckets.staticMeshes.pbrDrawItems.order[0], 2u);
    EXPECT_EQ(viewB.drawBuckets.staticMeshes.pbrDrawItems.order[0], 1u);
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
            snapshot->pointLightSourceCount = static_cast<uint32_t>(sceneId);
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
    ASSERT_EQ(plan.viewFamilies.size(), 2u);
    ASSERT_EQ(plan.viewFamilies[0].viewportTaskIndices.size(), 2u);
    ASSERT_EQ(plan.viewFamilies[1].viewportTaskIndices.size(), 1u);
    EXPECT_EQ(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
    EXPECT_NE(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[2]));
    EXPECT_EQ(plan.viewportTasks[0].familyIndex, plan.viewportTasks[1].familyIndex);
    EXPECT_NE(plan.viewportTasks[0].familyIndex, plan.viewportTasks[2].familyIndex);
    EXPECT_FALSE(scheduler.isFrameOpen());
}

TEST(RenderRuntimeSnapshotTest, SceneSchedulerCopiesIndependentViewOutputExtents)
{
    SceneRenderScheduler scheduler;
    scheduler.beginFrame(3);

    auto makeRequest = [](SceneViewId viewId, glm::vec2 extent)
    {
        SceneRenderRequest request;
        request.sceneId = 1;
        request.viewId = viewId;
        request.viewportRect = {.pos = {0.0f, 0.0f}, .extent = extent};
        request.buildSnapshot = []()
        {
            return std::make_shared<const SceneFrameSnapshot>();
        };
        return request;
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(11, {1280.0f, 720.0f})));
    ASSERT_TRUE(scheduler.submit(makeRequest(12, {256.0f, 256.0f})));

    const SceneRenderPlan plan = scheduler.seal();
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.viewportTasks[0].output.viewId, 11u);
    EXPECT_EQ(plan.viewportTasks[1].output.viewId, 12u);
    EXPECT_EQ(plan.viewportTasks[0].output.extent.width, 1280u);
    EXPECT_EQ(plan.viewportTasks[0].output.extent.height, 720u);
    EXPECT_EQ(plan.viewportTasks[1].output.extent.width, 256u);
    EXPECT_EQ(plan.viewportTasks[1].output.extent.height, 256u);
    EXPECT_NE(plan.viewportTasks[0].output.extent, plan.viewportTasks[1].output.extent);
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]),
              plan.snapshotFor(plan.viewportTasks[1]));
    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    EXPECT_EQ(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
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

TEST(RenderRuntimeSnapshotTest, SceneRenderPlanInputRequiresPlanAndMatchingViewRecordings)
{
    SceneRenderPlan plan;
    SceneViewportTask orphan;
    RenderFrameData frame;

    const SceneRenderPlanInput empty{};
    EXPECT_TRUE(empty.empty());
    EXPECT_FALSE(empty.complete());
    EXPECT_EQ(empty.primaryTask(), nullptr);

    SceneRenderPlan planWithTask;
    planWithTask.viewportTasks.push_back(SceneViewportTask{.viewId = 1});
    const SceneRenderPlanInput planOnly{.plan = &planWithTask};
    EXPECT_FALSE(planOnly.empty());
    EXPECT_FALSE(planOnly.complete());

    SceneRenderPlanInput viewsOnly;
    viewsOnly.views.push_back(SceneViewRecording{.task = &orphan, .frameData = &frame});
    EXPECT_FALSE(viewsOnly.empty());
    EXPECT_FALSE(viewsOnly.complete());
}

TEST(RenderRuntimeSnapshotTest, SceneRenderPlanInputRecordsEveryViewportTaskWithSharedSnapshot)
{
    SceneRenderScheduler scheduler;
    scheduler.beginFrame(8);

    auto makeRequest = [](SceneViewId viewId, const glm::mat4& view)
    {
        SceneRenderRequest request;
        request.sceneId = 1;
        request.viewId = viewId;
        request.view = view;
        request.viewportRect = {.pos = {0.0f, 0.0f}, .extent = {640.0f, 360.0f}};
        request.buildSnapshot = []()
        {
            return std::make_shared<const SceneFrameSnapshot>();
        };
        return request;
    };

    const glm::mat4 viewA = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 viewB = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
    ASSERT_TRUE(scheduler.submit(makeRequest(11, viewA)));
    ASSERT_TRUE(scheduler.submit(makeRequest(12, viewB)));

    const SceneRenderPlan plan = scheduler.seal();
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));

    RenderFrameData frameA;
    RenderFrameData frameB;
    frameA.sceneSnapshot = plan.snapshotFor(plan.viewportTasks[0]);
    frameB.sceneSnapshot = plan.snapshotFor(plan.viewportTasks[1]);

    SceneRenderPlanInput input{
        .plan = &plan,
        .views =
            {
                {.task = &plan.viewportTasks[0], .frameData = &frameA},
                {.task = &plan.viewportTasks[1], .frameData = &frameB},
            },
    };
    EXPECT_TRUE(input.complete());
    EXPECT_EQ(input.views.size(), 2u);
    EXPECT_EQ(input.primaryTask(), &plan.viewportTasks[0]);
    EXPECT_EQ(frameA.sceneSnapshot.get(), frameB.sceneSnapshot.get());
    EXPECT_NE(&frameA, &frameB);

    const CameraFrameInput host{
        .view         = glm::mat4(1.0f),
        .viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
        .frameData    = &frameA,
    };
    const CameraFrameInput cameraB = cameraForViewRecording(host, input.views[1]);
    EXPECT_EQ(cameraB.view, viewB);
    EXPECT_NE(cameraB.view, host.view);
    EXPECT_EQ(cameraB.frameData, &frameB);
    EXPECT_FLOAT_EQ(cameraB.viewportRect.extent.x, 640.0f);
    EXPECT_FLOAT_EQ(cameraB.viewportRect.extent.y, 360.0f);

    input.views.pop_back();
    EXPECT_FALSE(input.complete());
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

TEST(RenderRuntimeSnapshotTest, ViewComposeInsetsDescribePrimaryDisplayPreview)
{
    ViewComposeInput compose;
    EXPECT_TRUE(compose.empty());

    EXPECT_FLOAT_EQ(makeViewDisplayInsetRect({0.0f, 720.0f}).extent.x, 0.0f);

    const Rect2D dest = makeViewDisplayInsetRect({1280.0f, 720.0f});
    compose.insets.push_back(ViewDisplayInset{
        .viewId   = 2,
        .destRect = dest,
    });
    EXPECT_FALSE(compose.empty());
    ASSERT_EQ(compose.insets.size(), 1u);
    EXPECT_EQ(compose.insets.front().viewId, 2u);
    EXPECT_GT(compose.insets.front().destRect.extent.x, 0.0f);
    EXPECT_GT(compose.insets.front().destRect.pos.x, 640.0f);
    EXPECT_GT(compose.insets.front().destRect.pos.y, 360.0f);
    EXPECT_LE(compose.insets.front().destRect.pos.x + compose.insets.front().destRect.extent.x, 1280.0f);
}

TEST(RenderRuntimeSnapshotTest, OverlaySnapshotEmptyIncludesWorldLines)
{
    RenderViewportOverlaySnapshot snapshot;
    EXPECT_TRUE(snapshot.empty());
    snapshot.worldLines.push_back(RenderOverlayLine3D{
        .from  = {0.0f, 0.0f, 0.0f},
        .to    = {0.0f, 0.0f, -1.0f},
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
    });
    EXPECT_FALSE(snapshot.empty());
}

TEST(RenderRuntimeSnapshotTest, OverlayComposeRectDoesNotBecomeOutputExtent)
{
    SceneRenderScheduler scheduler;
    scheduler.beginFrame(9);

    auto buildSnapshot = []()
    {
        return std::make_shared<const SceneFrameSnapshot>();
    };

    SceneRenderRequest primary;
    primary.sceneId = 4;
    primary.viewId = kPrimarySceneViewId;
    primary.viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}};
    primary.buildSnapshot = buildSnapshot;

    const Rect2D composeRect = makeViewDisplayInsetRect({1280.0f, 720.0f});
    SceneRenderRequest overlay;
    overlay.sceneId = 4;
    overlay.viewId = 2;
    overlay.viewportRect = {.pos = {0.0f, 0.0f}, .extent = composeRect.extent};
    overlay.composeOntoViewId = kPrimarySceneViewId;
    overlay.composeRect = composeRect;
    overlay.buildSnapshot = buildSnapshot;

    ASSERT_TRUE(scheduler.submit(primary));
    ASSERT_TRUE(scheduler.submit(overlay));

    const SceneRenderPlan plan = scheduler.seal();
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_TRUE(plan.viewportTasks[0].ownsHostViewport());
    EXPECT_FALSE(plan.viewportTasks[1].ownsHostViewport());
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));
    EXPECT_EQ(plan.displayRootTask(), &plan.viewportTasks[0]);
    EXPECT_NE(plan.viewportTasks[0].output.extent, plan.viewportTasks[1].output.extent);
    EXPECT_EQ(plan.viewportTasks[1].output.extent.width,
              static_cast<uint32_t>(composeRect.extent.x));
    EXPECT_GT(plan.viewportTasks[1].composeRect.pos.x, 640.0f);
    EXPECT_FLOAT_EQ(plan.viewportTasks[1].viewportRect.pos.x, 0.0f);

    const auto insets = viewDisplayInsetsFromPlan(plan);
    ASSERT_EQ(insets.size(), 1u);
    EXPECT_EQ(insets.front().viewId, 2u);
    EXPECT_FLOAT_EQ(insets.front().destRect.pos.x, composeRect.pos.x);
    EXPECT_FLOAT_EQ(insets.front().destRect.pos.y, composeRect.pos.y);

    RenderFrameData overlayFrame;
    const SceneViewRecording recording{
        .task      = &plan.viewportTasks[1],
        .frameData = &overlayFrame,
    };
    CameraFrameInput host;
    host.viewportRect = {.pos = {40.0f, 80.0f}, .extent = {1280.0f, 720.0f}};
    const CameraFrameInput overlayCamera = cameraForViewRecording(host, recording);
    EXPECT_FLOAT_EQ(overlayCamera.viewportRect.pos.x, 0.0f);
    EXPECT_FLOAT_EQ(overlayCamera.viewportRect.pos.y, 0.0f);
    EXPECT_FLOAT_EQ(overlayCamera.viewportRect.extent.x,
                    static_cast<float>(plan.viewportTasks[1].output.extent.width));
    EXPECT_NE(overlayCamera.viewportRect.pos.x, composeRect.pos.x);
    EXPECT_LT(overlayCamera.viewportRect.extent.x, host.viewportRect.extent.x);
}

} // namespace
} // namespace ya

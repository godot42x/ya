#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewCompose.h"
#include "Render3D/RenderFrameData.h"
#include "Render3D/RenderDeviceState.h"
#include "Render3D/RenderFrameCoordinator.h"
#include "Scene/Core/Scene.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <type_traits>
#include <vector>

namespace ya
{
namespace
{

/// A declaration the scheduler may accept: an owned Scene, a view id, and a
/// View rect that describes at least one pixel, because a View's textures are
/// sized from its rect.
SceneViewDesc makeView(Scene* scene, SceneViewId viewId)
{
    return SceneViewDesc{
        .scene        = scene,
        .viewId       = viewId,
        .viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
    };
}

/// Seal and run the explicit extraction step with stub content. Most plan tests
/// only care about the structure seal() grouped, not about what the Scene holds.
ExtractedSceneRender sealWithEmptySnapshots(SceneRenderScheduler& scheduler)
{
    return buildSceneSnapshots(scheduler.seal(),
                               [](Scene&) { return std::make_shared<const SceneSnapshot>(); });
}

TEST(RenderRuntimeSnapshotTest, EmptyDevicePublishesEmptyViewportResources)
{
    RenderDeviceState device;
    RenderFrameCoordinator coordinator(device);

    EXPECT_EQ(device.getLiveSubmission(0), nullptr);
    EXPECT_EQ(device.getLiveSubmission(MAX_FLIGHTS_IN_FLIGHT), nullptr);
    EXPECT_EQ(device.getViewOutput(1), nullptr);

    // No tick has published a display root, so every host-viewport accessor
    // answers "nothing". Falling back to pipeline state would answer with an
    // image from another frame, which is how a stale viewport gets read as the
    // current one.
    EXPECT_EQ(device.getActiveViewportImageShared(), nullptr);
    EXPECT_EQ(device.getViewportDisplayImageShared(), nullptr);
    EXPECT_EQ(device.getPostprocessOutputImageShared(), nullptr);
    EXPECT_EQ(device.getViewportExtent().width, 0u);
    EXPECT_EQ(device.getViewportExtent().height, 0u);

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
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.sceneRender), ExtractedSceneRender>);
    static_assert(std::is_same_v<decltype(ExtractedSceneRender{}.views()), const std::vector<SceneViewRecording>&>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.viewCompose), ViewComposeInput>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.displayCompose), DisplayComposeInput>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.present), PresentFrameInput>);
    /// One declaration type: the plan entry holds the desc verbatim and only
    /// adds its own output identity and bookkeeping.
    static_assert(std::is_same_v<decltype(SceneViewportTask{}.desc), SceneViewDesc>);
    static_assert(std::is_same_v<decltype(SceneViewDesc{}.scene), Scene*>);
    static_assert(std::is_same_v<decltype(SceneSnapshotEntry{}.scene), Scene*>);
    static_assert(std::is_same_v<decltype(SceneViewRecording{}.task), const SceneViewportTask*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.camera), CameraFrameInput>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.submission), RenderSubmission*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.view), RenderViewRecordingContext>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.derivedScene), Scene*>);
    /// Only the extraction step may mint a plan, and the recordings point into
    /// it, so the pair is not copyable.
    static_assert(!std::is_copy_constructible_v<ExtractedSceneRender>);
    static_assert(std::is_move_constructible_v<ExtractedSceneRender>);

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
    EXPECT_TRUE(plan.sceneRender.views().empty());
}

TEST(RenderRuntimeSnapshotTest, EmptySceneRenderIsUiOnlyFrame)
{
    const RenderFramePlan plan{};
    EXPECT_TRUE(plan.sceneRender.empty());
    EXPECT_TRUE(plan.sceneRender.views().empty());
    EXPECT_EQ(plan.sceneRender.displayRootTask(), nullptr);
    EXPECT_EQ(plan.sceneRender.hostFrameData(), nullptr);
}

TEST(RenderRuntimeSnapshotTest, EveryTaskCarriesTheSceneItsDeclarationNamed)
{
    Scene sceneA("Authoring");
    Scene sceneB("Play");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(21);
    ASSERT_TRUE(scheduler.submit(makeView(&sceneA, 11)));
    ASSERT_TRUE(scheduler.submit(makeView(&sceneB, 21)));

    const ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan&      plan      = extracted.plan();
    ASSERT_EQ(plan.viewFamilies.size(), 2u);
    ASSERT_EQ(plan.viewportTasks.size(), 2u);

    EXPECT_EQ(plan.viewportTasks[0].desc.scene, &sceneA);
    EXPECT_EQ(plan.viewportTasks[1].desc.scene, &sceneB);
    // The plan owns one declaration entry per View, so the Scene has exactly
    // one spelling between the declaration and the extraction table.
    EXPECT_EQ(plan.snapshots[plan.viewportTasks[0].snapshotIndex].scene, &sceneA);
    EXPECT_EQ(plan.snapshots[plan.viewportTasks[1].snapshotIndex].scene, &sceneB);
    EXPECT_NE(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
}

TEST(RenderRuntimeSnapshotTest, SameSceneViewsShareOneSnapshotAndScene)
{
    Scene scene("World");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(22);
    ASSERT_TRUE(scheduler.submit(makeView(&scene, 11)));
    ASSERT_TRUE(scheduler.submit(makeView(&scene, 12)));

    const ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan&      plan      = extracted.plan();
    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    ASSERT_EQ(plan.snapshots.size(), 1u);
    EXPECT_EQ(plan.viewportTasks[0].desc.scene, &scene);
    EXPECT_EQ(plan.viewportTasks[1].desc.scene, &scene);
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));
    EXPECT_EQ(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
}

TEST(RenderRuntimeSnapshotTest, RenderFrameDataSeparatesSceneAndViewOwnership)
{
    static_assert(!std::is_base_of_v<SceneSnapshot, RenderFrameData>);
    static_assert(std::is_same_v<decltype(SceneSnapshot{}.directionalLightSource), SceneDirectionalLightData>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(SceneSnapshot{}.pointLightSources[0])>,
                                 ScenePointLightData>);
    static_assert(!std::is_same_v<decltype(SceneSnapshot{}.directionalLightSource),
                                  FrameContext::DirectionalLightData>);

    RenderFrameData frame;
    frame.view = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    auto sceneSnapshot = std::make_shared<SceneSnapshot>();
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

    auto sharedSnapshot = std::make_shared<const SceneSnapshot>();
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
    Scene sceneA("DedupA");
    Scene sceneB("DedupB");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(42);

    auto makeRequest = [](Scene* scene, SceneViewId viewId)
    {
        return SceneViewDesc{
            .scene        = scene,
            .viewId       = viewId,
            .viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
        };
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(&sceneA, 11)));
    ASSERT_TRUE(scheduler.submit(makeRequest(&sceneA, 12)));
    ASSERT_TRUE(scheduler.submit(makeRequest(&sceneB, 21)));

    SceneRenderPlan sealed = scheduler.seal();
    ASSERT_EQ(sealed.hostTick, 42u);
    ASSERT_EQ(sealed.snapshots.size(), 2u);
    ASSERT_EQ(sealed.viewportTasks.size(), 3u);
    // Grouping does not extract; the explicit step extracts once per Scene.
    std::vector<Scene*> extractedScenes;
    const ExtractedSceneRender extracted = buildSceneSnapshots(
        std::move(sealed),
        [&extractedScenes](Scene& scene)
        {
            extractedScenes.push_back(&scene);
            auto snapshot = std::make_shared<SceneSnapshot>();
            snapshot->pointLightSourceCount = static_cast<uint32_t>(scene.getInstanceId());
            return std::shared_ptr<const SceneSnapshot>(std::move(snapshot));
        });
    // The extractor is handed the declared Scene, one call per unique Scene.
    ASSERT_EQ(extractedScenes.size(), 2u);
    EXPECT_EQ(extractedScenes[0], &sceneA);
    EXPECT_EQ(extractedScenes[1], &sceneB);

    const SceneRenderPlan& plan = extracted.plan();
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
    EXPECT_FALSE(scheduler.isTickOpen());
}

TEST(RenderRuntimeSnapshotTest, SceneSchedulerCopiesIndependentViewOutputExtents)
{
    Scene scene("Extents");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(3);

    auto makeRequest = [&scene](SceneViewId viewId, glm::vec2 extent)
    {
        SceneViewDesc request;
        request.scene = &scene;
        request.viewId = viewId;
        request.viewportRect = {.pos = {0.0f, 0.0f}, .extent = extent};
        return request;
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(11, {1280.0f, 720.0f})));
    ASSERT_TRUE(scheduler.submit(makeRequest(12, {256.0f, 256.0f})));

    const ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan&      plan      = extracted.plan();
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
    Scene scene("Revised");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(9);

    int buildCalls = 0;
    auto makeRequest = [&scene](uint64_t revision, SceneViewId viewId)
    {
        SceneViewDesc request;
        request.scene = &scene;
        request.sceneRevision = revision;
        request.viewId = viewId;
        // A declaration a scheduler may accept: a View rect has to describe at
        // least one pixel, since its textures are sized from it.
        request.viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}};
        return request;
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(1, 51)));
    ASSERT_TRUE(scheduler.submit(makeRequest(1, 52)));
    ASSERT_TRUE(scheduler.submit(makeRequest(2, 53)));

    SceneRenderPlan sealed = scheduler.seal();
    const ExtractedSceneRender extracted = buildSceneSnapshots(std::move(sealed),
                                                               [&buildCalls](Scene&)
                                                               {
                                                                   ++buildCalls;
                                                                   return std::make_shared<const SceneSnapshot>();
                                                               });
    const SceneRenderPlan& plan = extracted.plan();
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

TEST(RenderRuntimeSnapshotTest, SceneSchedulerDropsViewsOfUnresolvedScene)
{
    SceneRenderScheduler scheduler;
    Scene sceneWithContent("WithContent");
    Scene sceneWithoutContent("NoContent");

    scheduler.beginTick(11);

    ASSERT_TRUE(scheduler.submit(makeView(&sceneWithContent, 11)));
    ASSERT_TRUE(scheduler.submit(makeView(&sceneWithContent, 12)));
    ASSERT_TRUE(scheduler.submit(makeView(&sceneWithoutContent, 21)));

    SceneRenderPlan sealed = scheduler.seal();
    ASSERT_EQ(sealed.viewFamilies.size(), 2u);

    // The second Scene has no content this tick. Its View is dropped, the first
    // Scene's Views still record, and the surviving Scene keeps its own family.
    const ExtractedSceneRender extracted =
        buildSceneSnapshots(std::move(sealed),
                            [&sceneWithContent](Scene& scene)
                            {
                                return &scene == &sceneWithContent
                                           ? std::make_shared<const SceneSnapshot>()
                                           : std::shared_ptr<const SceneSnapshot>{};
                            });
    const SceneRenderPlan& plan = extracted.plan();

    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.viewportTasks[0].desc.viewId, 11u);
    EXPECT_EQ(plan.viewportTasks[1].desc.viewId, 12u);
    EXPECT_TRUE(plan.snapshotFor(plan.viewportTasks[0]) != nullptr);
    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    EXPECT_EQ(plan.viewFamilies.front().viewportTaskIndices.size(), 2u);
    EXPECT_EQ(plan.viewportTasks[0].familyIndex, plan.viewportTasks[1].familyIndex);
    // Views are paired with tasks after extraction, so a dropped Scene cannot
    // leave a recording pointing at a task that no longer exists.
    EXPECT_TRUE(extracted.views().empty());
}

TEST(RenderRuntimeSnapshotTest, UiOnlyTickKeepsOneFrameSlotAndPairsNoView)
{
    SceneRenderScheduler scheduler;
    scheduler.beginTick(7);

    ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    EXPECT_TRUE(extracted.empty());
    EXPECT_EQ(extracted.displayRootTask(), nullptr);
    EXPECT_EQ(extracted.hostFrameData(), nullptr);

    // A previous tick left Scene content in the per-flight slot; a UI-only tick
    // must not hand the camera packet a stale snapshot.
    std::vector<RenderFrameData> frames(2);
    frames[0].sceneSnapshot = std::make_shared<const SceneSnapshot>();
    extracted.pairViewFrames(frames);

    ASSERT_EQ(frames.size(), 1u);
    EXPECT_FALSE(frames[0].sceneSnapshot);
    EXPECT_TRUE(extracted.views().empty());
}

TEST(RenderRuntimeSnapshotTest, HostFrameDataFollowsTheDisplayRootNotThePairingSlot)
{
    Scene scene("Shared");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(11);

    // The overlay View is declared first, so it lands in slot 0. Pairing order
    // is declaration order and says nothing about which View is the host's, so
    // the host camera packet must not read slot 0.
    SceneViewDesc overlay;
    overlay.scene             = &scene;
    overlay.viewId            = 2;
    overlay.viewportRect      = {.pos = {0.0f, 0.0f}, .extent = {320.0f, 180.0f}};
    overlay.composeOntoViewId = kPrimarySceneViewId;
    ASSERT_TRUE(scheduler.submit(overlay));
    ASSERT_TRUE(scheduler.submit(makeView(&scene, kPrimarySceneViewId)));

    ExtractedSceneRender   extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan& plan      = extracted.plan();
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.displayRootTask(), &plan.viewportTasks[1]);

    std::vector<RenderFrameData> frames;
    extracted.pairViewFrames(frames);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(extracted.hostFrameData(), &frames[1]);
}

TEST(RenderRuntimeSnapshotTest, TickThatDeclaresNoDisplayRootHasNoHostFrameData)
{
    Scene scene("Preview");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(12);

    // Only an overlay View: nothing owns the host viewport, so there is no
    // display root, no host frame data, and no substitute View to fall back to.
    SceneViewDesc overlay;
    overlay.scene             = &scene;
    overlay.viewId            = 2;
    overlay.viewportRect      = {.pos = {0.0f, 0.0f}, .extent = {320.0f, 180.0f}};
    overlay.composeOntoViewId = kPrimarySceneViewId;
    ASSERT_TRUE(scheduler.submit(overlay));

    ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    ASSERT_FALSE(extracted.empty());
    EXPECT_EQ(extracted.displayRootTask(), nullptr);

    std::vector<RenderFrameData> frames;
    extracted.pairViewFrames(frames);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(extracted.hostFrameData(), nullptr);
}

TEST(RenderRuntimeSnapshotTest, ExtractedSceneRenderPairsEveryTaskWithItsOwnFrameData)
{
    Scene scene("Shared");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(8);

    auto makeRequest = [&scene](SceneViewId viewId, const glm::mat4& view)
    {
        SceneViewDesc request;
        request.scene = &scene;
        request.viewId = viewId;
        request.view = view;
        request.viewportRect = {.pos = {0.0f, 0.0f}, .extent = {640.0f, 360.0f}};
        return request;
    };

    const glm::mat4 viewA = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 viewB = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
    ASSERT_TRUE(scheduler.submit(makeRequest(11, viewA)));
    ASSERT_TRUE(scheduler.submit(makeRequest(12, viewB)));

    ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan& plan   = extracted.plan();
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));

    std::vector<RenderFrameData> frames;
    extracted.pairViewFrames(frames);

    ASSERT_EQ(frames.size(), 2u);
    ASSERT_EQ(extracted.views().size(), 2u);
    EXPECT_EQ(extracted.views()[0].task, &plan.viewportTasks[0]);
    EXPECT_EQ(extracted.views()[1].task, &plan.viewportTasks[1]);
    EXPECT_EQ(extracted.views()[0].frameData, &frames[0]);
    EXPECT_EQ(extracted.views()[1].frameData, &frames[1]);
    EXPECT_EQ(extracted.displayRootTask(), &plan.viewportTasks[0]);

    const CameraFrameInput host{
        .view         = glm::mat4(1.0f),
        .viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
        .frameData    = &frames[0],
    };
    const CameraFrameInput cameraB = cameraForViewRecording(host, extracted.views()[1]);
    EXPECT_EQ(cameraB.view, viewB);
    EXPECT_NE(cameraB.view, host.view);
    EXPECT_EQ(cameraB.frameData, &frames[1]);
    EXPECT_FLOAT_EQ(cameraB.viewportRect.extent.x, 640.0f);
    EXPECT_FLOAT_EQ(cameraB.viewportRect.extent.y, 360.0f);
}

TEST(RenderRuntimeSnapshotTest, SceneRenderPlanRejectsSnapshotMetadataMismatch)
{
    Scene scene("Mismatch");

    SceneRenderPlan plan;
    plan.snapshots.push_back(SceneSnapshotEntry{
        .scene         = &scene,
        .sceneRevision = 3,
        .snapshot      = std::make_shared<const SceneSnapshot>(),
    });

    SceneViewportTask task;
    task.desc.scene         = &scene;
    task.desc.sceneRevision = 4;
    task.snapshotIndex      = 0;
    EXPECT_EQ(plan.snapshotFor(task), nullptr);
}

TEST(RenderRuntimeSnapshotTest, SceneSchedulerRejectsRequestsOutsideFrame)
{
    Scene scene("OutsideFrame");

    SceneRenderScheduler scheduler;
    SceneViewDesc request;
    request.scene = &scene;
    request.viewId = 1;
    request.viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}};

    EXPECT_FALSE(scheduler.submit(request));
    scheduler.beginTick(7);
    request.scene = nullptr;
    EXPECT_FALSE(scheduler.submit(request));
    request.scene = &scene;
    request.viewId = 1;
    EXPECT_TRUE(scheduler.submit(request));
    scheduler.clearTick();
    EXPECT_EQ(scheduler.declaredViewCount(), 0u);
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
    Scene scene("Preview");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(9);

    SceneViewDesc primary;
    primary.scene = &scene;
    primary.viewId = kPrimarySceneViewId;
    primary.viewportRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}};

    const Rect2D composeRect = makeViewDisplayInsetRect({1280.0f, 720.0f});
    SceneViewDesc overlay;
    overlay.scene = &scene;
    overlay.viewId = 2;
    overlay.viewportRect = {.pos = {0.0f, 0.0f}, .extent = composeRect.extent};
    overlay.composeOntoViewId = kPrimarySceneViewId;
    overlay.composeRect = composeRect;

    ASSERT_TRUE(scheduler.submit(primary));
    ASSERT_TRUE(scheduler.submit(overlay));

    const ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan&      plan      = extracted.plan();
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_TRUE(plan.viewportTasks[0].desc.ownsHostViewport());
    EXPECT_FALSE(plan.viewportTasks[1].desc.ownsHostViewport());
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));
    EXPECT_EQ(plan.displayRootTask(), &plan.viewportTasks[0]);
    EXPECT_NE(plan.viewportTasks[0].output.extent, plan.viewportTasks[1].output.extent);
    EXPECT_EQ(plan.viewportTasks[1].output.extent.width,
              static_cast<uint32_t>(composeRect.extent.x));
    EXPECT_GT(plan.viewportTasks[1].desc.composeRect.pos.x, 640.0f);
    EXPECT_FLOAT_EQ(plan.viewportTasks[1].desc.viewportRect.pos.x, 0.0f);

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

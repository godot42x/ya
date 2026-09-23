#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/ViewCompose.h"
#include "Render3D/RenderFrameData.h"
#include "Render3D/RenderDeviceState.h"
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

/// This test's own View owner. These cases declare Views straight into the
/// scheduler instead of going through a producer, so the test names its own
/// owner rather than borrowing a product's identity. The point of owner-scoped
/// keys is exactly that two owners never share an id space.
constexpr SceneViewOwnerId kTestOwner   = 0x7E57;
constexpr SceneViewKey     kDisplayView = SceneViewKey{.owner = kTestOwner, .local = 1};
constexpr SceneViewKey     kOverlayView = SceneViewKey{.owner = kTestOwner, .local = 2};

/// A declaration the scheduler may accept: an owned Scene, a view id, and a
/// View rect that describes at least one pixel, because a View's textures are
/// sized from its rect.
SceneViewDesc makeView(Scene* scene, SceneViewId viewId)
{
    return SceneViewDesc{
        .scene        = scene,
        .viewId       = viewId,
        .outputRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
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

    EXPECT_EQ(device.getLiveSubmission(0), nullptr);
    EXPECT_EQ(device.getLiveSubmission(MAX_FLIGHTS_IN_FLIGHT), nullptr);
    EXPECT_EQ(device.getViewOutput(0, 1), nullptr);

    // No tick has recorded anything, so every View query answers "nothing" for
    // its named identity. Which View the host window shows is the app's
    // arrangement, so the renderer has no unnamed host-viewport accessor left to
    // fall back through.
    EXPECT_EQ(device.surfaceImageFor(device.getViewOutput(0, 1)).image, nullptr);

    const RenderViewportSnapshot viewport = device.buildViewportSnapshot(0, 0);
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

TEST(RenderRuntimeSnapshotTest, RenderFramePlanGroupsFrameViewDisplayPresent)
{
    /// The plan carries frame-level facts, not a camera: each View's camera is
    /// on that View's own declaration and prepared data.
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.frame), FramePacket>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.sceneRender), ExtractedSceneRender>);
    static_assert(std::is_same_v<decltype(ExtractedSceneRender{}.views()), const std::vector<SceneViewRecording>&>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.viewCompose), ViewComposeInput>);
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.present), PresentFrameInput>);
    /// The plan carries behavior only as one named interface: the stages live
    /// in the interface and their order lives in the coordinator.
    static_assert(std::is_same_v<decltype(RenderFramePlan{}.recordExtensions), IFrameRecordExtensions*>);
    /// One declaration type: the plan entry holds the desc verbatim and only
    /// adds its own output identity and bookkeeping.
    static_assert(std::is_same_v<decltype(SceneViewTask{}.desc), SceneViewDesc>);
    static_assert(std::is_same_v<decltype(SceneViewDesc{}.scene), Scene*>);
    static_assert(std::is_same_v<decltype(SceneSnapshotEntry{}.scene), Scene*>);
    static_assert(std::is_same_v<decltype(SceneViewRecording{}.task), const SceneViewTask*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.frame), const FramePacket*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.submission), RenderSubmission*>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.view), RenderViewRecordingContext>);
    static_assert(std::is_same_v<decltype(RenderPipelineFrameContext{}.derivedScene), Scene*>);
    /// Only the extraction step may mint a plan, and the recordings point into
    /// it, so the pair is not copyable.
    static_assert(!std::is_copy_constructible_v<ExtractedSceneRender>);
    static_assert(std::is_move_constructible_v<ExtractedSceneRender>);

    RenderFramePlan plan{
        .frame          = {.deltaTime = 0.016f},
        .viewCompose    = {},
        .present        = {.surface = nullptr, .imageIndex = -1},
    };

    EXPECT_FLOAT_EQ(plan.frame.deltaTime, 0.016f);
    EXPECT_TRUE(plan.viewCompose.empty());
    EXPECT_TRUE(plan.viewCompose.insets.empty());
    EXPECT_EQ(plan.recordExtensions, nullptr);
    EXPECT_EQ(plan.present.surface, nullptr);
    EXPECT_EQ(plan.present.imageIndex, -1);
    /// The surface's backdrop is the host's declaration, and the default is the
    /// plain one: a host that says nothing gets the window showing the View.
    /// Only a host whose own passes fill the surface (the editor's chrome) says
    /// its content is the whole window.
    EXPECT_EQ(plan.present.backdrop, ESurfaceBackdrop::ViewDisplayImage);
    EXPECT_TRUE(plan.sceneRender.empty());
    EXPECT_TRUE(plan.sceneRender.views().empty());
}

TEST(RenderRuntimeSnapshotTest, EmptySceneRenderIsUiOnlyFrame)
{
    const RenderFramePlan plan{};
    EXPECT_TRUE(plan.sceneRender.empty());
    EXPECT_TRUE(plan.sceneRender.views().empty());
    EXPECT_EQ(plan.sceneRender.displayRootTask(), nullptr);
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
    ASSERT_EQ(plan.viewTasks.size(), 2u);

    EXPECT_EQ(plan.viewTasks[0].desc.scene, &sceneA);
    EXPECT_EQ(plan.viewTasks[1].desc.scene, &sceneB);
    // The plan owns one declaration entry per View, so the Scene has exactly
    // one spelling between the declaration and the extraction table.
    EXPECT_EQ(plan.snapshots[plan.viewTasks[0].snapshotIndex].scene, &sceneA);
    EXPECT_EQ(plan.snapshots[plan.viewTasks[1].snapshotIndex].scene, &sceneB);
    EXPECT_NE(plan.familyFor(plan.viewTasks[0]), plan.familyFor(plan.viewTasks[1]));
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
    EXPECT_EQ(plan.viewTasks[0].desc.scene, &scene);
    EXPECT_EQ(plan.viewTasks[1].desc.scene, &scene);
    EXPECT_EQ(plan.snapshotFor(plan.viewTasks[0]), plan.snapshotFor(plan.viewTasks[1]));
    EXPECT_EQ(plan.familyFor(plan.viewTasks[0]), plan.familyFor(plan.viewTasks[1]));
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
            .outputRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
        };
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(&sceneA, 11)));
    ASSERT_TRUE(scheduler.submit(makeRequest(&sceneA, 12)));
    ASSERT_TRUE(scheduler.submit(makeRequest(&sceneB, 21)));

    SceneRenderPlan sealed = scheduler.seal();
    ASSERT_EQ(sealed.hostTick, 42u);
    ASSERT_EQ(sealed.snapshots.size(), 2u);
    ASSERT_EQ(sealed.viewTasks.size(), 3u);
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
    EXPECT_EQ(plan.viewTasks[0].snapshotIndex, plan.viewTasks[1].snapshotIndex);
    EXPECT_NE(plan.viewTasks[0].snapshotIndex, plan.viewTasks[2].snapshotIndex);
    EXPECT_EQ(plan.snapshots[plan.viewTasks[0].snapshotIndex].snapshot,
              plan.snapshots[plan.viewTasks[1].snapshotIndex].snapshot);
    EXPECT_NE(plan.snapshots[plan.viewTasks[0].snapshotIndex].snapshot,
              plan.snapshots[plan.viewTasks[2].snapshotIndex].snapshot);
    EXPECT_EQ(plan.snapshotFor(plan.viewTasks[0]),
              plan.snapshotFor(plan.viewTasks[1]));
    ASSERT_EQ(plan.viewFamilies.size(), 2u);
    ASSERT_EQ(plan.viewFamilies[0].viewTaskIndices.size(), 2u);
    ASSERT_EQ(plan.viewFamilies[1].viewTaskIndices.size(), 1u);
    EXPECT_EQ(plan.familyFor(plan.viewTasks[0]), plan.familyFor(plan.viewTasks[1]));
    EXPECT_NE(plan.familyFor(plan.viewTasks[0]), plan.familyFor(plan.viewTasks[2]));
    EXPECT_EQ(plan.viewTasks[0].familyIndex, plan.viewTasks[1].familyIndex);
    EXPECT_NE(plan.viewTasks[0].familyIndex, plan.viewTasks[2].familyIndex);
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
        request.outputRect = {.pos = {0.0f, 0.0f}, .extent = extent};
        return request;
    };

    ASSERT_TRUE(scheduler.submit(makeRequest(11, {1280.0f, 720.0f})));
    ASSERT_TRUE(scheduler.submit(makeRequest(12, {256.0f, 256.0f})));

    const ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan&      plan      = extracted.plan();
    ASSERT_EQ(plan.viewTasks.size(), 2u);
    EXPECT_EQ(plan.viewTasks[0].output.viewId, 11u);
    EXPECT_EQ(plan.viewTasks[1].output.viewId, 12u);
    EXPECT_EQ(plan.viewTasks[0].output.extent.width, 1280u);
    EXPECT_EQ(plan.viewTasks[0].output.extent.height, 720u);
    EXPECT_EQ(plan.viewTasks[1].output.extent.width, 256u);
    EXPECT_EQ(plan.viewTasks[1].output.extent.height, 256u);
    EXPECT_NE(plan.viewTasks[0].output.extent, plan.viewTasks[1].output.extent);
    EXPECT_EQ(plan.snapshotFor(plan.viewTasks[0]),
              plan.snapshotFor(plan.viewTasks[1]));
    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    EXPECT_EQ(plan.familyFor(plan.viewTasks[0]), plan.familyFor(plan.viewTasks[1]));
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
        request.outputRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}};
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
    ASSERT_EQ(plan.viewTasks[0].snapshotIndex, plan.viewTasks[1].snapshotIndex);
    EXPECT_NE(plan.viewTasks[0].snapshotIndex, plan.viewTasks[2].snapshotIndex);
    EXPECT_EQ(plan.snapshots[0].sceneRevision, 1u);
    EXPECT_EQ(plan.snapshots[1].sceneRevision, 2u);
}

TEST(RenderRuntimeSnapshotTest, SceneRenderPlanRejectsInvalidSnapshotIndex)
{
    SceneRenderPlan plan;
    SceneViewTask task;
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

    ASSERT_EQ(plan.viewTasks.size(), 2u);
    EXPECT_EQ(plan.viewTasks[0].desc.viewId, 11u);
    EXPECT_EQ(plan.viewTasks[1].desc.viewId, 12u);
    EXPECT_TRUE(plan.snapshotFor(plan.viewTasks[0]) != nullptr);
    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    EXPECT_EQ(plan.viewFamilies.front().viewTaskIndices.size(), 2u);
    EXPECT_EQ(plan.viewTasks[0].familyIndex, plan.viewTasks[1].familyIndex);
    // Views are paired with tasks after extraction, so a dropped Scene cannot
    // leave a recording pointing at a task that no longer exists.
    EXPECT_TRUE(extracted.views().empty());
}

TEST(RenderRuntimeSnapshotTest, UiOnlyTickOwnsNoPreparedViewData)
{
    SceneRenderScheduler scheduler;
    scheduler.beginTick(7);

    ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    EXPECT_TRUE(extracted.empty());
    EXPECT_EQ(extracted.displayRootTask(), nullptr);

    extracted.pairViewFrames();
    EXPECT_TRUE(extracted.views().empty());
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
        request.outputRect = {.pos = {0.0f, 0.0f}, .extent = {640.0f, 360.0f}};
        return request;
    };

    const glm::mat4 viewA = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 viewB = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
    ASSERT_TRUE(scheduler.submit(makeRequest(11, viewA)));
    ASSERT_TRUE(scheduler.submit(makeRequest(12, viewB)));

    ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan& plan = extracted.plan();
    ASSERT_EQ(plan.viewTasks.size(), 2u);
    EXPECT_EQ(plan.snapshotFor(plan.viewTasks[0]), plan.snapshotFor(plan.viewTasks[1]));

    extracted.pairViewFrames();
    ASSERT_EQ(extracted.views().size(), 2u);
    EXPECT_EQ(extracted.views()[0].task, &plan.viewTasks[0]);
    EXPECT_EQ(extracted.views()[1].task, &plan.viewTasks[1]);
    ASSERT_NE(extracted.views()[0].frameData, nullptr);
    ASSERT_NE(extracted.views()[1].frameData, nullptr);
    EXPECT_NE(extracted.views()[0].frameData, extracted.views()[1].frameData);
    EXPECT_EQ(extracted.displayRootTask(), &plan.viewTasks[0]);

    // Each View is its own camera: the second View's matrices and extent are on
    // its own declaration and its own prepared slot, so there is no host camera
    // to copy and then override field by field.
    EXPECT_EQ(plan.viewTasks[1].desc.view, viewB);
    EXPECT_NE(plan.viewTasks[1].desc.view, plan.viewTasks[0].desc.view);
    EXPECT_EQ(plan.viewTasks[1].output.extent.width, 640u);
    EXPECT_EQ(plan.viewTasks[1].output.extent.height, 360u);
    extracted.views()[1].frameData->view = viewB;

    // The runtime moves the packet owner into RenderFramePlan before record().
    // Moving it must rebind both borrowed pointers to the destination storage,
    // not leave recordings pointing into the moved-from object.
    ExtractedSceneRender moved = std::move(extracted);
    ASSERT_EQ(moved.views().size(), 2u);
    EXPECT_EQ(moved.views()[0].task, &moved.plan().viewTasks[0]);
    EXPECT_EQ(moved.views()[1].task, &moved.plan().viewTasks[1]);
    ASSERT_NE(moved.views()[0].frameData, nullptr);
    ASSERT_NE(moved.views()[1].frameData, nullptr);
    EXPECT_NE(moved.views()[0].frameData, moved.views()[1].frameData);
    EXPECT_EQ(moved.views()[1].frameData->view, viewB);
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

    SceneViewTask task;
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
    request.outputRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}};

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

TEST(RenderRuntimeSnapshotTest, OverlayComposeRectDoesNotBecomeOutputExtent)
{
    Scene scene("Preview");

    SceneRenderScheduler scheduler;
    scheduler.beginTick(9);

    SceneViewDesc primary;
    primary.scene = &scene;
    primary.viewId = kDisplayView.viewId();
    primary.outputRect = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}};

    const Rect2D composeRect = makeViewDisplayInsetRect({1280.0f, 720.0f});
    SceneViewDesc overlay;
    overlay.scene = &scene;
    overlay.viewId = kOverlayView.viewId();
    overlay.outputRect = {.pos = {0.0f, 0.0f}, .extent = composeRect.extent};
    overlay.composeOntoViewId = kDisplayView.viewId();
    overlay.composeRect = composeRect;

    ASSERT_TRUE(scheduler.submit(primary));
    ASSERT_TRUE(scheduler.submit(overlay));

    const ExtractedSceneRender extracted = sealWithEmptySnapshots(scheduler);
    const SceneRenderPlan&      plan      = extracted.plan();
    ASSERT_EQ(plan.viewTasks.size(), 2u);
    EXPECT_TRUE(plan.viewTasks[0].desc.isDisplayRoot());
    EXPECT_FALSE(plan.viewTasks[1].desc.isDisplayRoot());
    EXPECT_EQ(plan.snapshotFor(plan.viewTasks[0]), plan.snapshotFor(plan.viewTasks[1]));
    EXPECT_EQ(plan.displayRootTask(), &plan.viewTasks[0]);
    EXPECT_NE(plan.viewTasks[0].output.extent, plan.viewTasks[1].output.extent);
    EXPECT_EQ(plan.viewTasks[1].output.extent.width,
              static_cast<uint32_t>(composeRect.extent.x));
    EXPECT_GT(plan.viewTasks[1].desc.composeRect.pos.x, 640.0f);
    EXPECT_FLOAT_EQ(plan.viewTasks[1].desc.outputRect.pos.x, 0.0f);

    const auto insets = viewDisplayInsetsFromPlan(plan);
    ASSERT_EQ(insets.size(), 1u);
    // The derived inset names the overlay View it composes onto the display
    // root -- whichever key that View was declared with, not a literal.
    EXPECT_EQ(insets.front().viewId, kOverlayView.viewId());
    EXPECT_FLOAT_EQ(insets.front().destRect.pos.x, composeRect.pos.x);
    EXPECT_FLOAT_EQ(insets.front().destRect.pos.y, composeRect.pos.y);

    // The overlay View records into its own output extent, and its compose dest
    // is a separate fact: the host view's rect plays no part in either.
    EXPECT_EQ(plan.viewTasks[1].output.extent.width, static_cast<uint32_t>(composeRect.extent.x));
    EXPECT_NE(plan.viewTasks[1].desc.composeRect.pos.x, plan.viewTasks[1].desc.outputRect.pos.x);
    EXPECT_LT(static_cast<float>(plan.viewTasks[1].output.extent.width),
              plan.viewTasks[0].output.extent.width);
}

} // namespace
} // namespace ya

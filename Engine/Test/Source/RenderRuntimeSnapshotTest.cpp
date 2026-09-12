#include "Render3D/Common/RenderFrameInputs.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/RenderRuntime.h"

#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <iterator>
#include <string>
#include <type_traits>

namespace ya
{
namespace
{

std::string readEngineSource(const std::filesystem::path& relative)
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / relative;
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "missing " << path.string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::size_t countLiteral(const std::string& haystack, const std::string& needle)
{
    std::size_t count = 0;
    for (std::size_t pos = 0; (pos = haystack.find(needle, pos)) != std::string::npos; pos += needle.size()) {
        ++count;
    }
    return count;
}

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

TEST(RenderRuntimeSnapshotTest, RenderFrameDataSeparatesWorldAndViewOwnership)
{
    static_assert(std::is_base_of_v<WorldFrameSnapshot, RenderFrameData>);

    RenderFrameData frame;
    frame.view = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    frame.drawBuckets.staticMeshes.pbrDrawItems.resize(1);
    frame.skinningPalettes.resize(1);

    WorldFrameSnapshot& world = frame;
    EXPECT_EQ(world.drawBuckets.staticMeshes.pbrDrawItems.size(), 1u);
    EXPECT_EQ(world.skinningPalettes.size(), 1u);
    EXPECT_EQ(frame.view[3][0], 4.0f);

    world.clearWorld();
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
            auto snapshot = std::make_shared<WorldFrameSnapshot>();
            snapshot->numPointLights = static_cast<uint32_t>(sceneId);
            return std::shared_ptr<const WorldFrameSnapshot>(std::move(snapshot));
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
            return std::make_shared<const WorldFrameSnapshot>();
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
        .snapshot = std::make_shared<const WorldFrameSnapshot>(),
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
    request.buildSnapshot = [] { return std::make_shared<const WorldFrameSnapshot>(); };

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

TEST(RenderRuntimeSnapshotTest, RenderFrameKeepsSinglePrepareAndSubmit)
{
    const std::string runtimeCpp = readEngineSource("Source/Framework/Render/Render3D/RenderRuntime.cpp");
    const std::string frameCpp   = readEngineSource("Source/Framework/Render/Render3D/RenderRuntimeFrame.cpp");

    EXPECT_EQ(countLiteral(runtimeCpp, "if (!prepareFrame("), 1u);
    EXPECT_EQ(countLiteral(runtimeCpp, "endFrameCommandBuffer("), 1u);
    EXPECT_EQ(countLiteral(frameCpp, "void RenderRuntime::endFrameCommandBuffer("), 1u);
    EXPECT_EQ(countLiteral(frameCpp, "present->end("), 0u);
    EXPECT_EQ(countLiteral(frameCpp, "present->begin("), 0u);
    EXPECT_EQ(countLiteral(runtimeCpp, "present->end("), 0u);
    EXPECT_EQ(countLiteral(runtimeCpp, "present->begin("), 0u);
    EXPECT_EQ(countLiteral(frameCpp, "getPrimarySurfaceContext"), 0u);
    EXPECT_EQ(countLiteral(runtimeCpp, "getPrimarySurfaceContext"), 0u);
    EXPECT_EQ(countLiteral(frameCpp, "acquirePresentFrame"), 0u);
    EXPECT_EQ(countLiteral(runtimeCpp, "acquirePresentFrame"), 0u);
}

TEST(RenderRuntimeSnapshotTest, HostPresentCoordinatorOwnsAcquireAndPresent)
{
    const std::string hostCpp = readEngineSource(
        "Source/Applications/GameRuntime/Lifecycle/GameRuntimeFrameOrchestrator.cpp");
    EXPECT_NE(hostCpp.find("acquirePresentFrame("), std::string::npos);
    EXPECT_NE(hostCpp.find("submitPresentFrame("), std::string::npos);

    const std::string extraCpp = readEngineSource("Source/Framework/GUI/Host/GUIWindowPresent.cpp");
    EXPECT_NE(extraCpp.find("acquirePresentFrame("), std::string::npos);
    EXPECT_NE(extraCpp.find("submitPresentFrame("), std::string::npos);
    EXPECT_EQ(countLiteral(extraCpp, "PresentationGraphService"), 0u);

    const std::string displayCpp =
        readEngineSource("Source/Framework/Render/Render3D/Services/PresentationGraphService.cpp");
    EXPECT_EQ(countLiteral(displayCpp, "acquirePresentFrame"), 0u);
    EXPECT_EQ(countLiteral(displayCpp, "present->begin("), 0u);
    EXPECT_EQ(countLiteral(displayCpp, "present->end("), 0u);
    EXPECT_NE(displayCpp.find("buildPresentationImages"), std::string::npos);
    EXPECT_EQ(countLiteral(displayCpp, "VulkanSwapChain"), 0u);
    EXPECT_EQ(countLiteral(displayCpp, "as<VulkanSwapChain>"), 0u);

    const std::string guiHostCpp = readEngineSource("Source/Framework/GUI/Host/GUIAppHost.cpp");
    EXPECT_EQ(countLiteral(guiHostCpp, "VulkanSwapChain"), 0u);
    EXPECT_EQ(countLiteral(guiHostCpp, "as<VulkanSwapChain>"), 0u);
    EXPECT_NE(guiHostCpp.find("requestRecreate("), std::string::npos);

    const std::string extraPresentCpp = readEngineSource("Source/Framework/GUI/Host/GUIWindowPresent.cpp");
    EXPECT_EQ(countLiteral(extraPresentCpp, "VulkanSwapChain"), 0u);
    EXPECT_EQ(countLiteral(extraPresentCpp, "as<VulkanSwapChain>"), 0u);

    const std::string surfaceTestCpp = readEngineSource("Test/Source/RHISurfaceContextTest.cpp");
    EXPECT_EQ(countLiteral(surfaceTestCpp, "VulkanSwapChain"), 0u);
    EXPECT_EQ(countLiteral(surfaceTestCpp, "vulkan.h"), 0u);
}

TEST(RenderRuntimeSnapshotTest, HostBuildsWorldAndUiSnapshotsBeforeRenderRuntime)
{
    const std::string hostCpp = readEngineSource(
        "Source/Applications/GameRuntime/Lifecycle/GameRuntimeFrameOrchestrator.cpp");

    const auto worldSnapshotPos = hostCpp.find("RenderFrameExtractor::extract(");
    const auto uiSnapshotPos    = hostCpp.find("buildSnapshot()");
    const auto renderFramePos   = hostCpp.find("renderRuntime->renderFrame(");

    ASSERT_NE(worldSnapshotPos, std::string::npos);
    ASSERT_NE(uiSnapshotPos, std::string::npos);
    ASSERT_NE(renderFramePos, std::string::npos);
    EXPECT_LT(worldSnapshotPos, renderFramePos);
    EXPECT_LT(uiSnapshotPos, renderFramePos);

    // The render graph receives immutable frame packets. The host must not
    // hand the live scene or WidgetTree to RenderRuntime during recording.
    const auto renderFrameBlock = hostCpp.substr(renderFramePos);
    EXPECT_EQ(renderFrameBlock.find("getRegistry()"), std::string::npos);
    EXPECT_EQ(renderFrameBlock.find("WidgetTree"), std::string::npos);
    EXPECT_EQ(renderFrameBlock.find("getActiveScene()"), std::string::npos);
}

TEST(RenderRuntimeSnapshotTest, RenderFrameRecordsViewComposeThenDisplayCompose)
{
    const std::string runtimeCpp = readEngineSource("Source/Framework/Render/Render3D/RenderRuntime.cpp");
    EXPECT_NE(runtimeCpp.find("recordCameraViewCompose("), std::string::npos);
    EXPECT_NE(runtimeCpp.find("recordDisplayCompose("), std::string::npos);
    EXPECT_EQ(countLiteral(runtimeCpp, "recordCameraViewCompose("), 1u);
    EXPECT_EQ(countLiteral(runtimeCpp, "recordDisplayCompose("), 1u);

    const auto viewPos    = runtimeCpp.find("recordCameraViewCompose(");
    const auto displayPos = runtimeCpp.find("recordDisplayCompose(");
    EXPECT_LT(viewPos, displayPos);
}

TEST(RenderRuntimeSnapshotTest, ViewComposeWritesCameraRtNotSwapchain)
{
    const std::string viewCpp = readEngineSource("Source/Framework/Render/Render3D/Common/ViewCompose.cpp");
    EXPECT_NE(viewCpp.find("recordRender2DComposePass("), std::string::npos);
    EXPECT_NE(viewCpp.find("RuntimeUIComposite"), std::string::npos);
    EXPECT_NE(viewCpp.find("viewCompose.recordCompose"), std::string::npos);
    EXPECT_EQ(countLiteral(viewCpp, "getSwapchain"), 0u);
    EXPECT_EQ(countLiteral(viewCpp, "IRenderSurfaceContext"), 0u);
    EXPECT_EQ(countLiteral(viewCpp, "PresentSrcKHR"), 0u);
    EXPECT_EQ(countLiteral(viewCpp, "WidgetTree"), 0u);
    EXPECT_EQ(countLiteral(viewCpp, "primarySwapchain"), 0u);

    const std::string runtimeCpp = readEngineSource("Source/Framework/Render/Render3D/RenderRuntime.cpp");
    EXPECT_NE(runtimeCpp.find("getViewportDisplayImageShared().get()"), std::string::npos);
}

TEST(RenderRuntimeSnapshotTest, DisplayComposeWritesSwapchainImage)
{
    const std::string displayH = readEngineSource("Source/Framework/Render/Render3D/Services/PresentationGraphService.h");
    EXPECT_NE(displayH.find("recordDisplayCompose("), std::string::npos);
    EXPECT_EQ(countLiteral(displayH, "void render("), 0u);

    const std::string displayCpp = readEngineSource("Source/Framework/Render/Render3D/Services/PresentationGraphService.cpp");
    EXPECT_NE(displayCpp.find("PresentSrcKHR"), std::string::npos);
    EXPECT_NE(displayCpp.find("Presentation.Output"), std::string::npos);
    EXPECT_NE(displayCpp.find("getCurrentPresentationImageShared"), std::string::npos);
}

TEST(RenderRuntimeSnapshotTest, CameraPipelineDoesNotGuessPresentSurface)
{
    const std::string frameCpp = readEngineSource("Source/Framework/Render/Render3D/RenderRuntimeFrame.cpp");
    EXPECT_NE(frameCpp.find(".camera                    = input.camera"), std::string::npos);
    EXPECT_EQ(countLiteral(frameCpp, "_viewportState.getRect()"), 1u);

    const char* sources[] = {
        "Source/Framework/Render/Render3D/Forward/ForwardRenderPipeline.cpp",
        "Source/Framework/Render/Render3D/Deferred/DeferredRenderPipeline.cpp",
        "Source/Framework/Render/Render3D/Pipelines/DebugPrimitives.cpp",
        "Source/Framework/Render/Render3D/Common/RenderOverlay.cpp",
        "Source/Framework/Render/Render3D/Deferred/ViewportOverlayStage.cpp",
    };
    for (const char* relative : sources) {
        const std::string text = readEngineSource(relative);
        EXPECT_EQ(countLiteral(text, "primaryFrameIndex"), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "getSwapchain("), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "getNativeWindow("), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "primarySwapchain"), 0u) << relative;
    }
}

} // namespace
} // namespace ya

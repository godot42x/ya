#include "Render3D/Common/RenderFrameInputs.h"
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

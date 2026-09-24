#include "GUI/Compose/GUIRenderSurface.h"

#include "TestSource.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

namespace ya
{
namespace
{

// Source-reading guards resolve the repo root through the shared walker so a
// test can move between suite directories without pointing at nothing.
using ::ya::test::readEngineSource;

std::size_t countLiteral(const std::string& haystack, const std::string& needle)
{
    std::size_t count = 0;
    for (std::size_t pos = 0; (pos = haystack.find(needle, pos)) != std::string::npos; pos += needle.size()) {
        ++count;
    }
    return count;
}

TEST(GUIRenderSurfaceTest, DisplayComposeLayoutIsPresentSrc)
{
    EXPECT_TRUE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::PresentSrcKHR));
    EXPECT_FALSE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::ShaderReadOnlyOptimal));
    EXPECT_FALSE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::ColorAttachmentOptimal));
}

TEST(GUIRenderSurfaceTest, ComposeTargetDoesNotAcquirePresentOrReadLiveTree)
{
    const char* sources[] = {
        "Source/Framework/GUI/Runtime/Compose/include/GUI/Compose/GUIRenderSurface.h",
        "Source/Framework/GUI/Runtime/Compose/GUIRenderSurface.cpp",
        "Source/Framework/GUI/Runtime/Compose/Render2DComposePass.cpp",
    };
    for (const char* relative : sources) {
        const std::string text = readEngineSource(relative);
        EXPECT_EQ(countLiteral(text, "WidgetTree"), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "IRenderSurfaceContext"), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "getSwapchain"), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "primarySwapchain"), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "->begin("), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "->end("), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "->present("), 0u) << relative;
        EXPECT_EQ(countLiteral(text, "acquire("), 0u) << relative;
    }

    const std::string passH = readEngineSource("Source/Framework/GUI/Runtime/Compose/include/GUI/Compose/Render2DComposePass.h");
    EXPECT_NE(passH.find("never touches the live widget tree"), std::string::npos);

    const std::string surfaceCpp = readEngineSource("Source/Framework/GUI/Runtime/Compose/GUIRenderSurface.cpp");
    EXPECT_NE(surfaceCpp.find("passDesc.finalLayout = _finalLayout"), std::string::npos);
    EXPECT_NE(surfaceCpp.find("recordRender2DComposePass("), std::string::npos);
}

// A surface used to carry the submission (`submit(imageIndex, cmdBufs)`), which
// made a WINDOW the thing a frame's queue work was handed to. Submission is a
// queue operation over command buffers, and the composability that matters --
// "one frame, N windows, one submission carrying N sync pairs" -- only holds
// while the surface contributes sync and a legalizing command, never a submit.
// The interface cannot regress silently: these guards fail if `submit` comes
// back or if the composable contribution is dissolved again.
TEST(GUIRenderSurfaceTest, SubmissionIsAQueueOperationNotASurfaceOne)
{
    const std::string surfaceH = readEngineSource("Source/Framework/RHI/include/RHI/Core/RenderSurfaceContext.h");
    EXPECT_EQ(countLiteral(surfaceH, "submit("), 0u) << "RenderSurfaceContext.h";
    EXPECT_NE(surfaceH.find("operation over command buffers"), std::string::npos)
        << "the header must keep the reason `submit` is absent, or the next author re-adds it";
    EXPECT_NE(surfaceH.find("IRender::submitFrame"), std::string::npos)
        << "the header must name where a submission is actually made";
    EXPECT_NE(surfaceH.find("presentFallbackCommand"), std::string::npos);
    EXPECT_NE(surfaceH.find("getRenderFinishedSemaphore"), std::string::npos);

    const std::string presentH = readEngineSource("Source/Framework/RHI/include/RHI/Core/PresentFrame.h");
    EXPECT_NE(presentH.find("struct FPresentSync"), std::string::npos)
        << "one acquired surface's contribution must stay a value, so a frame can merge several";
    EXPECT_NE(presentH.find("presentSyncOf"), std::string::npos);
    EXPECT_NE(presentH.find("submitPresentFrame(IRender&"), std::string::npos)
        << "the frame submits, so the helper needs the device, not a surface method";

    // Neither the app nor the GUI host may reach a submission through a surface.
    const char* productSources[] = {
        "Source/Framework/GUI/Host/GUIAppHost.cpp",
        "Source/Framework/GUI/Host/GUIWindowPresent.cpp",
        "Source/Applications/GameRuntime/Lifecycle/GameRuntimeTickOrchestrator.cpp",
    };
    for (const char* relative : productSources) {
        const std::string text = readEngineSource(relative);
        EXPECT_EQ(countLiteral(text, "->submit("), 0u) << relative;
    }
}

} // namespace
} // namespace ya

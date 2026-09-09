#include "GUI/Compose/GUIRenderSurface.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

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

TEST(GUIRenderSurfaceTest, DisplayComposeLayoutIsPresentSrc)
{
    EXPECT_TRUE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::PresentSrcKHR));
    EXPECT_FALSE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::ShaderReadOnlyOptimal));
    EXPECT_FALSE(GUIRenderSurface::isDisplayComposeFinalLayout(EImageLayout::ColorAttachmentOptimal));
}

TEST(GUIRenderSurfaceTest, ComposeTargetDoesNotAcquirePresentOrReadLiveTree)
{
    const char* sources[] = {
        "Source/Framework/GUI/Runtime/Compose/GUIRenderSurface.h",
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

    const std::string passH = readEngineSource("Source/Framework/GUI/Runtime/Compose/Render2DComposePass.h");
    EXPECT_NE(passH.find("never touches the live widget tree"), std::string::npos);

    const std::string surfaceCpp = readEngineSource("Source/Framework/GUI/Runtime/Compose/GUIRenderSurface.cpp");
    EXPECT_NE(surfaceCpp.find("passDesc.finalLayout = _finalLayout"), std::string::npos);
    EXPECT_NE(surfaceCpp.find("recordRender2DComposePass("), std::string::npos);
}

} // namespace
} // namespace ya

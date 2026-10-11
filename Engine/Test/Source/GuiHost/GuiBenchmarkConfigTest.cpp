#include "GUI/Host/GuiBenchmark.h"

#include <gtest/gtest.h>

#include <string>

namespace ya
{
namespace
{

TEST(GuiBenchmarkConfigTest, ParsesTheGalleryPageAndResolution)
{
    FGuiBenchmarkConfig config;
    std::string         error;
    const nlohmann::json json = {
        {"name", "GUIWorkbench"},
        {"page", "Dock"},
        {"resolution", {{"width", 1920}, {"height", 1080}}},
        {"presentMode", "immediate"},
        {"warmupFrames", 4},
        {"sampleFrames", 8},
        {"outputDir", "Engine/Saved/Benchmark/GUIWorkbench"},
    };
    ASSERT_TRUE(parseGuiBenchmarkJson(json, config, error)) << error;
    EXPECT_EQ(config.common.name, "GUIWorkbench");
    EXPECT_EQ(config.page, "Dock");
    EXPECT_EQ(config.common.width, 1920u);
    EXPECT_EQ(config.common.height, 1080u);
    EXPECT_EQ(config.common.presentMode, "immediate");
    EXPECT_EQ(config.common.warmupFrames, 4u);
    EXPECT_EQ(config.common.sampleFrames, 8u);
}

TEST(GuiBenchmarkConfigTest, RejectsGameOnlyKeys)
{
    FGuiBenchmarkConfig config;
    std::string         error;
    const nlohmann::json json = {
        {"name", "GUIWorkbench"},
        {"cameraPath", {{"keyframes", nlohmann::json::array()}}},
    };
    EXPECT_FALSE(parseGuiBenchmarkJson(json, config, error));
    EXPECT_NE(error.find("cameraPath"), std::string::npos);
}

TEST(GuiBenchmarkConfigTest, RejectsUncapFrameRate)
{
    FGuiBenchmarkConfig config;
    std::string         error;
    const nlohmann::json json = {{"uncapFrameRate", true}};
    EXPECT_FALSE(parseGuiBenchmarkJson(json, config, error));
    EXPECT_NE(error.find("uncapFrameRate"), std::string::npos);
}

TEST(GuiBenchmarkConfigTest, RejectsAnUnknownPresentMode)
{
    FGuiBenchmarkConfig config;
    std::string         error;
    const nlohmann::json json = {{"presentMode", "vsync"}};
    EXPECT_FALSE(parseGuiBenchmarkJson(json, config, error));
}

} // namespace
} // namespace ya

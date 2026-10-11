// PerfLab preset configs must stay inside the PB1 benchmark schema.

#include "GameRuntime/Benchmark/BenchmarkConfig.h"
#include "TestSource.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace
{

const char* kPresetNames[] = {
    "Lights_50",
    "Lights_200",
    "Lights_500",
    "Meshes_1000",
    "Meshes_5000",
    "Meshes_10000",
    "Sprites_500",
    "Sprites_2000",
    "Sprites_5000",
    "Sponza",
};

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << path.string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void expectPresetContract(const std::filesystem::path& perflab, const char* name)
{
    const auto path = perflab / "Benchmark" / (std::string(name) + ".bench.json");
    ASSERT_TRUE(std::filesystem::is_regular_file(path)) << path.string();

    const nlohmann::json json = nlohmann::json::parse(readFile(path));
    EXPECT_TRUE(json.contains("cpuTrace")) << name;
    EXPECT_FALSE(json.at("cpuTrace").get<bool>()) << name;

    ya::BenchmarkConfig config;
    std::string         error;
    ASSERT_TRUE(ya::loadBenchmarkConfig(path.string(), config, error)) << name << ": " << error;
    EXPECT_EQ(config.common.name, name);
    EXPECT_EQ(config.common.presentMode, "immediate");
    EXPECT_TRUE(config.bUncapFrameRate);
    EXPECT_EQ(config.common.warmupFrames, 120u);
    EXPECT_EQ(config.common.sampleFrames, 600u);
    EXPECT_FALSE(config.bCpuTrace);
    EXPECT_EQ(config.common.width, 1920u);
    EXPECT_EQ(config.common.height, 1080u);
    EXPECT_GE(config.cameraPath.keyframes.size(), 2u);
    EXPECT_TRUE(config.cameraPath.bLoop);
    ASSERT_TRUE(config.scene.has_value()) << name;

    const auto scene = perflab / *config.scene;
    EXPECT_TRUE(std::filesystem::is_regular_file(scene)) << scene.string();
}

} // namespace

TEST(PerfLabBenchmarkConfigTest, PresetsMatchBenchmarkContract)
{
    const auto perflab = ya::test::repoRoot() / "Example" / "PerfLab";
    ASSERT_TRUE(std::filesystem::is_directory(perflab / "Benchmark")) << perflab.string();

    for (const char* name : kPresetNames) {
        expectPresetContract(perflab, name);
    }

    for (const auto& entry : std::filesystem::directory_iterator(perflab / "Benchmark")) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        ya::BenchmarkConfig config;
        std::string         error;
        EXPECT_TRUE(ya::loadBenchmarkConfig(entry.path().string(), config, error))
            << entry.path().string() << ": " << error;
        EXPECT_FALSE(config.bCpuTrace) << entry.path().string();
        EXPECT_FALSE(config.cameraPath.keyframes.empty()) << entry.path().string();
    }
}

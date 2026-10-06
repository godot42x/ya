// `--benchmark` config parsing, present-mode fallback and camera path sampling.

#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "GameRuntime/AppOptions.h"
#include "GameRuntime/Benchmark/BenchmarkConfig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace
{

using namespace ya;

void expectVec3Near(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

FBenchmarkCameraPath twoKeyPath(bool bLoop)
{
    return FBenchmarkCameraPath{
        .keyframes =
            {
                {.frame = 10, .position = {0, 0, 0}, .rotation = {0, 0, 0}},
                {.frame = 20, .position = {10, 0, -10}, .rotation = {0, 90, 0}},
            },
        .bLoop = bLoop,
    };
}

TEST(BenchmarkConfigTest, EmptyObjectKeepsDefaults)
{
    BenchmarkConfig config;
    std::string     error;
    ASSERT_TRUE(parseBenchmarkConfig(nlohmann::json::object(), config, error)) << error;
    EXPECT_EQ(config.width, 1920u);
    EXPECT_EQ(config.height, 1080u);
    EXPECT_EQ(config.presentMode, EPresentMode::Immediate);
    EXPECT_TRUE(config.bUncapFrameRate);
    EXPECT_EQ(config.warmupFrames, 120u);
    EXPECT_EQ(config.sampleFrames, 600u);
    EXPECT_FALSE(config.scene.has_value());
    EXPECT_TRUE(config.cameraPath.keyframes.empty());
}

TEST(BenchmarkConfigTest, ParsesEveryField)
{
    const nlohmann::json json = nlohmann::json::parse(R"({
        "name": "orbit",
        "scene": "Content/Scenes/Main.yscene",
        "resolution": {"width": 1280, "height": 720},
        "presentMode": "mailbox",
        "uncapFrameRate": false,
        "warmupFrames": 30,
        "sampleFrames": 90,
        "fixedDeltaSeconds": 0,
        "cpuTrace": true,
        "outputDir": "Out",
        "cameraPath": {
            "framesPerSecond": 30,
            "loop": true,
            "keyframes": [
                {"frame": 0, "position": [0, 2, 8], "rotation": [0, 180, 0]},
                {"time": 2.0, "position": [6, 3, 8]}
            ]
        }
    })");
    BenchmarkConfig config;
    std::string     error;
    ASSERT_TRUE(parseBenchmarkConfig(json, config, error)) << error;
    EXPECT_EQ(config.name, "orbit");
    EXPECT_EQ(config.scene.value_or(""), "Content/Scenes/Main.yscene");
    EXPECT_EQ(config.width, 1280u);
    EXPECT_EQ(config.height, 720u);
    EXPECT_EQ(config.presentMode, EPresentMode::Mailbox);
    EXPECT_FALSE(config.bUncapFrameRate);
    EXPECT_EQ(config.warmupFrames, 30u);
    EXPECT_EQ(config.sampleFrames, 90u);
    EXPECT_FLOAT_EQ(config.fixedDeltaSeconds, 0.0f);
    EXPECT_TRUE(config.bCpuTrace);
    EXPECT_EQ(config.outputDir, "Out");
    ASSERT_EQ(config.cameraPath.keyframes.size(), 2u);
    EXPECT_TRUE(config.cameraPath.bLoop);
    EXPECT_EQ(config.cameraPath.keyframes[1].frame, 60u);
    expectVec3Near(config.cameraPath.keyframes[1].rotation, glm::vec3(0.0f));
}

TEST(BenchmarkConfigTest, RejectsMalformedConfigs)
{
    const char* cases[] = {
        R"({"unknownKey": 1})",
        R"({"presentMode": "vsync"})",
        R"({"sampleFrames": 0})",
        R"({"warmupFrames": -1})",
        R"({"resolution": {"width": 0, "height": 720}})",
        R"({"resolution": {"width": 1280, "height": 720, "depth": 1}})",
        R"({"cameraPath": {"keyframes": []}})",
        R"({"cameraPath": {"keyframes": [{"position": [0, 0, 0]}]}})",
        R"({"cameraPath": {"keyframes": [{"frame": 0, "time": 0, "position": [0, 0, 0]}]}})",
        R"({"cameraPath": {"keyframes": [{"frame": 0}]}})",
        R"({"cameraPath": {"keyframes": [{"frame": 0, "position": [0, 0]}]}})",
        R"({"cameraPath": {"keyframes": [{"frame": 5, "position": [0, 0, 0]}, {"frame": 5, "position": [1, 0, 0]}]}})",
        R"({"sampleFrames": "600"})",
        R"({"uncapFrameRate": 1})",
    };
    for (const char* text : cases) {
        BenchmarkConfig config;
        std::string     error;
        EXPECT_FALSE(parseBenchmarkConfig(nlohmann::json::parse(text), config, error)) << text;
        EXPECT_FALSE(error.empty()) << text;
    }
}

TEST(BenchmarkConfigTest, PresentModeFallsBackAwayFromVsync)
{
    namespace E = EPresentMode;
    EXPECT_EQ(chooseBenchmarkPresentMode(E::Mailbox, {E::FIFO, E::Mailbox, E::Immediate}), E::Mailbox);
    EXPECT_EQ(chooseBenchmarkPresentMode(E::Immediate, {E::FIFO, E::Mailbox}), E::Mailbox);
    EXPECT_EQ(chooseBenchmarkPresentMode(E::Mailbox, {E::FIFO, E::Immediate}), E::Immediate);
    EXPECT_EQ(chooseBenchmarkPresentMode(E::Immediate, {E::FIFO, E::FIFO_Relaxed}), E::FIFO_Relaxed);
    EXPECT_EQ(chooseBenchmarkPresentMode(E::Immediate, {E::FIFO}), E::FIFO);

    EPresentMode::T parsed = E::FIFO;
    EXPECT_TRUE(tryParseBenchmarkPresentMode(toBenchmarkPresentModeLabel(E::Mailbox), parsed));
    EXPECT_EQ(parsed, E::Mailbox);
}

TEST(BenchmarkConfigPathTest, EmptyPathLeavesTheCameraAlone)
{
    EXPECT_FALSE(sampleBenchmarkCameraPath(FBenchmarkCameraPath{}, 0).has_value());
}

TEST(BenchmarkConfigPathTest, InterpolatesLinearlyBetweenKeyframes)
{
    const FBenchmarkCameraPath path = twoKeyPath(false);

    const auto before = sampleBenchmarkCameraPath(path, 0);
    ASSERT_TRUE(before);
    expectVec3Near(before->position, {0, 0, 0});

    const auto mid = sampleBenchmarkCameraPath(path, 15);
    ASSERT_TRUE(mid);
    expectVec3Near(mid->position, {5, 0, -5});
    expectVec3Near(mid->rotation, {0, 45, 0});

    const auto quarter = sampleBenchmarkCameraPath(path, 12);
    ASSERT_TRUE(quarter);
    expectVec3Near(quarter->position, {2, 0, -2});

    const auto after = sampleBenchmarkCameraPath(path, 1000);
    ASSERT_TRUE(after);
    expectVec3Near(after->position, {10, 0, -10});
    expectVec3Near(after->rotation, {0, 90, 0});
}

TEST(BenchmarkConfigPathTest, LoopWrapsPastTheLastKeyframe)
{
    const FBenchmarkCameraPath path = twoKeyPath(true);

    const auto wrapped = sampleBenchmarkCameraPath(path, 25);
    ASSERT_TRUE(wrapped);
    expectVec3Near(wrapped->position, {5, 0, -5});

    const auto twice = sampleBenchmarkCameraPath(path, 32);
    ASSERT_TRUE(twice);
    expectVec3Near(twice->position, {2, 0, -2});

    // The last frame is the loop seam: same pose as the first keyframe.
    // Frame 21 is one step into the next cycle.
    const auto atSeam = sampleBenchmarkCameraPath(path, 20);
    ASSERT_TRUE(atSeam);
    expectVec3Near(atSeam->position, {0, 0, 0});
    expectVec3Near(atSeam->rotation, {0, 0, 0});

    const auto frame21 = sampleBenchmarkCameraPath(path, 21);
    ASSERT_TRUE(frame21);
    expectVec3Near(frame21->position, {1, 0, -1});
    expectVec3Near(frame21->rotation, {0, 9, 0});
}

TEST(BenchmarkConfigPathTest, SingleKeyframeHoldsItsPose)
{
    FBenchmarkCameraPath path;
    path.keyframes.push_back({.frame = 3, .position = {1, 2, 3}, .rotation = {4, 5, 6}});
    path.bLoop = true;
    for (uint64_t frame : {0ull, 3ull, 99ull}) {
        const auto pose = sampleBenchmarkCameraPath(path, frame);
        ASSERT_TRUE(pose);
        expectVec3Near(pose->position, {1, 2, 3});
        expectVec3Near(pose->rotation, {4, 5, 6});
    }
}

struct TempBenchConfig
{
    std::filesystem::path path;

    TempBenchConfig(std::string_view name, std::string_view body)
    {
        path = std::filesystem::temp_directory_path() / name;
        std::ofstream out(path);
        out << body;
    }

    ~TempBenchConfig()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

void initDesc(AppDesc& desc, std::vector<std::string> args)
{
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    Logger::init();
    desc.init(static_cast<int>(argv.size()), argv.data());
}

constexpr const char* kBenchJson = R"({
    "resolution": {"width": 640, "height": 480},
    "scene": "Content/Scenes/Bench.yscene",
    "cpuTrace": false,
    "warmupFrames": 1,
    "sampleFrames": 2
})";

TEST(BenchmarkConfigTest, CpuProfileDefaultsOnWhenCompiledIn)
{
    AppDesc desc;
    initDesc(desc, {"ya-test"});
    EXPECT_EQ(desc.profiling.bCpuProfileEnabled, !profiling::isCompiledOut());
    EXPECT_FALSE(desc.profiling.bCpuProfileOverridden);
    EXPECT_FALSE(desc.benchmark.has_value());

    AppDesc off;
    initDesc(off, {"ya-test", "--cpu-profile=false"});
    EXPECT_FALSE(off.profiling.bCpuProfileEnabled);
    EXPECT_TRUE(off.profiling.bCpuProfileOverridden);
}

TEST(BenchmarkConfigTest, BenchmarkCpuTraceAppliesWhenCpuProfileIsAbsent)
{
    const TempBenchConfig file("ya-pb1-cpu-trace.json", kBenchJson);
    AppDesc               desc;
    initDesc(desc, {"ya-test", "--benchmark", file.path.string()});
    ASSERT_TRUE(desc.benchmark.has_value());
    EXPECT_EQ(desc.width, 640);
    EXPECT_EQ(desc.height, 480);
    EXPECT_EQ(desc.automation.scenePath.value_or(""), "Content/Scenes/Bench.yscene");
    EXPECT_FALSE(desc.profiling.bCpuProfileEnabled);
    EXPECT_TRUE(desc.profiling.bCpuProfileOverridden);
}

TEST(BenchmarkConfigTest, ExplicitCpuProfileOverridesBenchmarkConfig)
{
    const TempBenchConfig file("ya-pb1-cpu-explicit.json", kBenchJson);
    AppDesc               desc;
    initDesc(desc, {"ya-test", "--benchmark", file.path.string(), "--cpu-profile=true"});
    ASSERT_TRUE(desc.benchmark.has_value());
    EXPECT_TRUE(desc.profiling.bCpuProfileEnabled);
    EXPECT_TRUE(desc.profiling.bCpuProfileOverridden);
    EXPECT_EQ(desc.width, 640);
}

TEST(BenchmarkConfigTest, EditorSkipsBenchmarkConfig)
{
    const TempBenchConfig file("ya-pb1-editor.json", kBenchJson);
    AppDesc               desc;
    initDesc(desc, {"ya-test", "--editor", "--benchmark", file.path.string()});
    EXPECT_TRUE(desc.bEditor);
    EXPECT_FALSE(desc.benchmark.has_value());
    EXPECT_EQ(desc.width, 1024);
    EXPECT_EQ(desc.height, 768);
    EXPECT_FALSE(desc.automation.scenePath.has_value());
    EXPECT_EQ(desc.profiling.bCpuProfileEnabled, !profiling::isCompiledOut());
    EXPECT_FALSE(desc.profiling.bCpuProfileOverridden);
}

} // namespace

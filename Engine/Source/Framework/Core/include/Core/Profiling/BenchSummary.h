#pragma once

#include "Core/Api.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ya::profiling
{

class BenchAggregator;

/// Facts about the process and checkout, the same for every host.
struct FBenchHostFacts
{
    std::string gitCommit;
    bool        bGitDirty = false;
    std::string buildMode;
    std::string platform;
    std::string arch;
    std::string timestampUtc;
};

/// `meta` of `bench_summary.json`: what was run, where, and under which
/// presentation conditions. Present modes are the host's labels; the summary
/// does not interpret them.
struct FBenchSummaryMeta
{
    std::string name;
    std::string configPath;
    std::string project;
    std::string scene;
    FBenchHostFacts host;
    std::string graphicsApi;
    std::string gpuName;
    std::string gpuDriver;
    uint32_t    renderWidth     = 0;
    uint32_t    renderHeight    = 0;
    uint32_t    swapchainWidth  = 0;
    uint32_t    swapchainHeight = 0;
    std::string requestedPresentMode;
    std::string presentMode;
    std::vector<std::string> availablePresentModes;
    bool        bFrameRateCapped  = false;
    float       fixedDeltaSeconds = 0.0f;
    bool        bCpuTrace         = false;
    uint64_t    warmupFrames      = 0;
    uint64_t    sampleFrames      = 0;
    uint64_t    sampledFrames     = 0;
    size_t      cameraKeyframes   = 0;
};

inline constexpr int kBenchSummarySchemaVersion = 1;

/// Git commit of the working directory (`git rev-parse HEAD`), build mode,
/// platform, architecture, and the current UTC time. A missing git yields
/// "unknown".
[[nodiscard]] YA_CORE_API FBenchHostFacts collectBenchHostFacts();

/// `{schemaVersion, meta, <sections from the aggregator>}`.
[[nodiscard]] YA_CORE_API nlohmann::json buildBenchSummaryJson(const FBenchSummaryMeta& meta, const BenchAggregator& aggregator);

YA_CORE_API bool writeBenchSummaryFile(const std::string& path, const nlohmann::json& summary);

} // namespace ya::profiling

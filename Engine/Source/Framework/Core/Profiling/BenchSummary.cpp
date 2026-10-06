#include "Core/Profiling/BenchSummary.h"

#include "Core/Log.h"
#include "Core/Profiling/BenchAggregator.h"
#include "Core/Profiling/Profiling.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>

namespace ya::profiling
{

namespace
{

/// First line of a shell command's stdout, trailing whitespace stripped.
/// Empty when the command cannot run or prints nothing.
std::string runCommandFirstLine(const char* command, bool* outAnyOutput = nullptr)
{
#if defined(_WIN32)
    FILE* pipe = _popen(command, "r");
#else
    FILE* pipe = popen(command, "r");
#endif
    if (!pipe) {
        return {};
    }
    std::string           firstLine;
    std::array<char, 256> buffer{};
    bool                  bAnyOutput = false;
    bool                  bFirstLine = true;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        bAnyOutput = true;
        if (bFirstLine) {
            firstLine += buffer.data();
            if (!firstLine.empty() && firstLine.back() == '\n') {
                bFirstLine = false;
            }
        }
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    while (!firstLine.empty() && (firstLine.back() == '\n' || firstLine.back() == '\r' || firstLine.back() == ' ')) {
        firstLine.pop_back();
    }
    if (outAnyOutput) {
        *outAnyOutput = bAnyOutput;
    }
    return firstLine;
}

const char* platformLabel()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

const char* archLabel()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x64";
#else
    return "unknown";
#endif
}

std::string utcTimestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm           utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z",
                       utc.tm_year + 1900,
                       utc.tm_mon + 1,
                       utc.tm_mday,
                       utc.tm_hour,
                       utc.tm_min,
                       utc.tm_sec);
}

} // namespace

FBenchHostFacts collectBenchHostFacts()
{
#if defined(_WIN32)
    constexpr const char* kNullDevice = "2>NUL";
#else
    constexpr const char* kNullDevice = "2>/dev/null";
#endif
    FBenchHostFacts facts;
    facts.gitCommit = runCommandFirstLine(std::format("git rev-parse HEAD {}", kNullDevice).c_str());
    if (facts.gitCommit.empty()) {
        facts.gitCommit = "unknown";
    }
    else {
        bool bChanges = false;
        (void)runCommandFirstLine(std::format("git status --porcelain --untracked-files=no {}", kNullDevice).c_str(), &bChanges);
        facts.bGitDirty = bChanges;
    }
    facts.buildMode    = getBuildModeLabel();
    facts.platform     = platformLabel();
    facts.arch         = archLabel();
    facts.timestampUtc = utcTimestamp();
    return facts;
}

nlohmann::json buildBenchSummaryJson(const FBenchSummaryMeta& meta, const BenchAggregator& aggregator)
{
    nlohmann::json summary = {
        {"schemaVersion", kBenchSummarySchemaVersion},
        {"meta",
         {
             {"name", meta.name},
             {"config", meta.configPath},
             {"project", meta.project},
             {"scene", meta.scene},
             {"gitCommit", meta.host.gitCommit},
             {"gitDirty", meta.host.bGitDirty},
             {"buildMode", meta.host.buildMode},
             {"platform", meta.host.platform},
             {"arch", meta.host.arch},
             {"timestampUtc", meta.host.timestampUtc},
             {"graphicsApi", meta.graphicsApi},
             {"gpu", meta.gpuName},
             {"gpuDriver", meta.gpuDriver},
             {"renderResolution", {{"width", meta.renderWidth}, {"height", meta.renderHeight}}},
             {"swapchainExtent", {{"width", meta.swapchainWidth}, {"height", meta.swapchainHeight}}},
             {"requestedPresentMode", meta.requestedPresentMode},
             {"presentMode", meta.presentMode},
             {"availablePresentModes", meta.availablePresentModes},
             {"frameRateCapped", meta.bFrameRateCapped},
             {"fixedDeltaSeconds", meta.fixedDeltaSeconds},
             {"cpuTrace", meta.bCpuTrace},
             {"warmupFrames", meta.warmupFrames},
             {"sampleFrames", meta.sampleFrames},
             {"sampledFrames", meta.sampledFrames},
             {"cameraKeyframes", meta.cameraKeyframes},
         }},
    };
    nlohmann::json sections = aggregator.buildSections();
    for (auto& [section, value] : sections.items()) {
        summary[section] = std::move(value);
    }
    return summary;
}

bool writeBenchSummaryFile(const std::string& path, const nlohmann::json& summary)
{
    const std::filesystem::path outputPath(path);
    if (outputPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            YA_CORE_ERROR("Failed to create benchmark output directory {}: {}", outputPath.parent_path().string(), ec.message());
            return false;
        }
    }
    std::ofstream stream(outputPath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        YA_CORE_ERROR("Failed to open benchmark summary {}", path);
        return false;
    }
    stream << summary.dump(2) << '\n';
    return stream.good();
}

} // namespace ya::profiling

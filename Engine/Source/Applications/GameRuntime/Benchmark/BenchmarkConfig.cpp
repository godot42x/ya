#include "GameRuntime/Benchmark/BenchmarkConfig.h"

#include "Core/Profiling/BenchmarkConfig.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

namespace ya
{

namespace
{

bool readVec3(const nlohmann::json& value, glm::vec3& out, std::string_view where, std::string& error)
{
    if (!value.is_array() || value.size() != 3 ||
        !std::all_of(value.begin(), value.end(), [](const nlohmann::json& v) { return v.is_number(); })) {
        error = std::format("{}: expected [x, y, z]", where);
        return false;
    }
    out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    return true;
}

bool parseCameraPath(const nlohmann::json& json, FBenchmarkCameraPath& out, std::string& error)
{
    if (!json.is_object()) {
        error = "'cameraPath' must be an object";
        return false;
    }
    if (!profiling::rejectUnknownBenchmarkKeys(json, {"framesPerSecond", "loop", "keyframes"}, "cameraPath", error)) {
        return false;
    }
    double framesPerSecond = 60.0;
    if (!profiling::readBenchmarkNumber(json, "framesPerSecond", framesPerSecond, error) ||
        !profiling::readBenchmarkBool(json, "loop", out.bLoop, error)) {
        error = "cameraPath." + error;
        return false;
    }
    if (framesPerSecond <= 0.0) {
        error = "cameraPath.framesPerSecond must be positive";
        return false;
    }
    if (!json.contains("keyframes") || !json.at("keyframes").is_array() || json.at("keyframes").empty()) {
        error = "cameraPath.keyframes must be a non-empty array";
        return false;
    }

    out.keyframes.clear();
    for (size_t i = 0; i < json.at("keyframes").size(); ++i) {
        const auto&       entry = json.at("keyframes")[i];
        const std::string where = std::format("cameraPath.keyframes[{}]", i);
        if (!entry.is_object() ||
            !profiling::rejectUnknownBenchmarkKeys(entry, {"frame", "time", "position", "rotation"}, where, error)) {
            if (error.empty()) {
                error = where + ": expected an object";
            }
            return false;
        }
        const bool bHasFrame = entry.contains("frame");
        const bool bHasTime  = entry.contains("time");
        if (bHasFrame == bHasTime) {
            error = where + ": give exactly one of 'frame' or 'time'";
            return false;
        }

        FBenchmarkCameraKeyframe keyframe;
        if (bHasFrame) {
            if (!profiling::readBenchmarkNumber(entry, "frame", keyframe.frame, error)) {
                error = where + ": " + error;
                return false;
            }
        }
        else {
            double seconds = 0.0;
            if (!profiling::readBenchmarkNumber(entry, "time", seconds, error) || seconds < 0.0) {
                error = where + ": 'time' must be a non-negative number of seconds";
                return false;
            }
            keyframe.frame = static_cast<uint64_t>(std::llround(seconds * framesPerSecond));
        }
        if (!entry.contains("position") || !readVec3(entry.at("position"), keyframe.position, where + ".position", error)) {
            if (error.empty()) {
                error = where + ": 'position' is required";
            }
            return false;
        }
        if (entry.contains("rotation") && !readVec3(entry.at("rotation"), keyframe.rotation, where + ".rotation", error)) {
            return false;
        }
        if (!out.keyframes.empty() && keyframe.frame <= out.keyframes.back().frame) {
            error = std::format("{}: frame {} does not follow frame {}", where, keyframe.frame, out.keyframes.back().frame);
            return false;
        }
        out.keyframes.push_back(keyframe);
    }
    return true;
}

} // namespace

std::optional<FBenchmarkCameraPose> sampleBenchmarkCameraPath(const FBenchmarkCameraPath& path, uint64_t frame)
{
    const auto& keys = path.keyframes;
    if (keys.empty()) {
        return std::nullopt;
    }
    // The last keyframe is the loop seam, so it coincides with the first:
    // phase 0, not "one frame past the end". Holding the last pose and only
    // wrapping when frame > last skips that seam.
    if (path.bLoop && keys.size() > 1 && keys.back().frame > keys.front().frame && frame >= keys.front().frame) {
        const uint64_t span = keys.back().frame - keys.front().frame;
        frame                = keys.front().frame + (frame - keys.front().frame) % span;
    }
    if (frame <= keys.front().frame) {
        return FBenchmarkCameraPose{.position = keys.front().position, .rotation = keys.front().rotation};
    }
    if (frame >= keys.back().frame) {
        return FBenchmarkCameraPose{.position = keys.back().position, .rotation = keys.back().rotation};
    }

    const auto next = std::upper_bound(keys.begin(), keys.end(), frame, [](uint64_t f, const FBenchmarkCameraKeyframe& key) {
        return f < key.frame;
    });
    const auto  prev = std::prev(next);
    const float t    = static_cast<float>(frame - prev->frame) / static_cast<float>(next->frame - prev->frame);
    return FBenchmarkCameraPose{
        .position = glm::mix(prev->position, next->position, t),
        .rotation = glm::mix(prev->rotation, next->rotation, t),
    };
}

bool parseBenchmarkConfig(const nlohmann::json& json, BenchmarkConfig& out, std::string& error)
{
    profiling::FBenchmarkCommonConfig common;
    common.sourcePath = out.common.sourcePath;
    if (!profiling::parseBenchmarkCommonConfig(json,
                                               {"scene", "uncapFrameRate", "fixedDeltaSeconds", "cpuTrace", "cameraPath"},
                                               common,
                                               error)) {
        return false;
    }
    if (!profiling::isBenchmarkPresentModeLabel(common.presentMode)) {
        error = std::format("'presentMode' must be immediate, mailbox, fifo or fifo_relaxed (got '{}')", common.presentMode);
        return false;
    }

    BenchmarkConfig config;
    config.common = std::move(common);

    std::string scene;
    if (!profiling::readBenchmarkString(json, "scene", scene, error) ||
        !profiling::readBenchmarkBool(json, "uncapFrameRate", config.bUncapFrameRate, error) ||
        !profiling::readBenchmarkNumber(json, "fixedDeltaSeconds", config.fixedDeltaSeconds, error) ||
        !profiling::readBenchmarkBool(json, "cpuTrace", config.bCpuTrace, error)) {
        return false;
    }
    if (!scene.empty()) {
        config.scene = std::move(scene);
    }
    if (config.fixedDeltaSeconds < 0.0f) {
        error = "'fixedDeltaSeconds' must be >= 0";
        return false;
    }
    if (json.contains("cameraPath") && !parseCameraPath(json.at("cameraPath"), config.cameraPath, error)) {
        return false;
    }

    out = std::move(config);
    return true;
}

bool loadBenchmarkConfig(const std::string& path, BenchmarkConfig& out, std::string& error)
{
    nlohmann::json json;
    if (!profiling::readBenchmarkJsonFile(path, json, error)) {
        return false;
    }
    out                   = {};
    out.common.sourcePath = path;
    if (!parseBenchmarkConfig(json, out, error)) {
        error = std::format("{}: {}", path, error);
        return false;
    }
    if (out.common.name.empty()) {
        out.common.name = std::filesystem::path(path).stem().string();
    }
    return true;
}

} // namespace ya

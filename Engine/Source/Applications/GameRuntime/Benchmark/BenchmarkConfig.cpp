#include "GameRuntime/Benchmark/BenchmarkConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <type_traits>

namespace ya
{

namespace
{

bool rejectUnknownKeys(const nlohmann::json& object, std::initializer_list<std::string_view> known, std::string_view where, std::string& error)
{
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (std::find(known.begin(), known.end(), key) == known.end()) {
            error = std::format("{}: unknown key '{}'", where, key);
            return false;
        }
    }
    return true;
}

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

template <typename T>
bool readNumber(const nlohmann::json& object, const char* key, T& out, std::string& error)
{
    if (!object.contains(key)) {
        return true;
    }
    const auto& value = object.at(key);
    if (!value.is_number()) {
        error = std::format("'{}' must be a number", key);
        return false;
    }
    if constexpr (std::is_unsigned_v<T>) {
        if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<int64_t>() >= 0)) {
            error = std::format("'{}' must be a non-negative integer", key);
            return false;
        }
    }
    out = value.get<T>();
    return true;
}

bool readBool(const nlohmann::json& object, const char* key, bool& out, std::string& error)
{
    if (!object.contains(key)) {
        return true;
    }
    if (!object.at(key).is_boolean()) {
        error = std::format("'{}' must be true or false", key);
        return false;
    }
    out = object.at(key).get<bool>();
    return true;
}

bool readString(const nlohmann::json& object, const char* key, std::string& out, std::string& error)
{
    if (!object.contains(key)) {
        return true;
    }
    if (!object.at(key).is_string()) {
        error = std::format("'{}' must be a string", key);
        return false;
    }
    out = object.at(key).get<std::string>();
    return true;
}

bool parseCameraPath(const nlohmann::json& json, FBenchmarkCameraPath& out, std::string& error)
{
    if (!json.is_object()) {
        error = "'cameraPath' must be an object";
        return false;
    }
    if (!rejectUnknownKeys(json, {"framesPerSecond", "loop", "keyframes"}, "cameraPath", error)) {
        return false;
    }
    double framesPerSecond = 60.0;
    if (!readNumber(json, "framesPerSecond", framesPerSecond, error) || !readBool(json, "loop", out.bLoop, error)) {
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
        if (!entry.is_object() || !rejectUnknownKeys(entry, {"frame", "time", "position", "rotation"}, where, error)) {
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
            if (!readNumber(entry, "frame", keyframe.frame, error)) {
                error = where + ": " + error;
                return false;
            }
        }
        else {
            double seconds = 0.0;
            if (!readNumber(entry, "time", seconds, error) || seconds < 0.0) {
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

const char* toBenchmarkPresentModeLabel(EPresentMode::T mode)
{
    switch (mode) {
    case EPresentMode::Immediate:
        return "immediate";
    case EPresentMode::Mailbox:
        return "mailbox";
    case EPresentMode::FIFO:
        return "fifo";
    case EPresentMode::FIFO_Relaxed:
        return "fifo_relaxed";
    }
    return "unknown";
}

bool tryParseBenchmarkPresentMode(std::string_view text, EPresentMode::T& outMode)
{
    for (const EPresentMode::T mode : {EPresentMode::Immediate, EPresentMode::Mailbox, EPresentMode::FIFO, EPresentMode::FIFO_Relaxed}) {
        if (text == toBenchmarkPresentModeLabel(mode)) {
            outMode = mode;
            return true;
        }
    }
    return false;
}

EPresentMode::T chooseBenchmarkPresentMode(EPresentMode::T requested, const std::vector<EPresentMode::T>& available)
{
    const std::array<EPresentMode::T, 5> preference{
        requested,
        EPresentMode::Immediate,
        EPresentMode::Mailbox,
        EPresentMode::FIFO_Relaxed,
        EPresentMode::FIFO,
    };
    for (const EPresentMode::T mode : preference) {
        if (std::find(available.begin(), available.end(), mode) != available.end()) {
            return mode;
        }
    }
    return EPresentMode::FIFO;
}

bool parseBenchmarkConfig(const nlohmann::json& json, BenchmarkConfig& out, std::string& error)
{
    if (!json.is_object()) {
        error = "benchmark config must be a JSON object";
        return false;
    }
    if (!rejectUnknownKeys(json,
                           {"name", "scene", "resolution", "presentMode", "uncapFrameRate", "warmupFrames", "sampleFrames",
                            "fixedDeltaSeconds", "cpuTrace", "outputDir", "cameraPath"},
                           "benchmark config",
                           error)) {
        return false;
    }

    BenchmarkConfig config;
    config.sourcePath = out.sourcePath;

    std::string scene;
    std::string presentMode;
    if (!readString(json, "name", config.name, error) || !readString(json, "scene", scene, error) ||
        !readString(json, "presentMode", presentMode, error) || !readBool(json, "uncapFrameRate", config.bUncapFrameRate, error) ||
        !readNumber(json, "warmupFrames", config.warmupFrames, error) || !readNumber(json, "sampleFrames", config.sampleFrames, error) ||
        !readNumber(json, "fixedDeltaSeconds", config.fixedDeltaSeconds, error) || !readBool(json, "cpuTrace", config.bCpuTrace, error) ||
        !readString(json, "outputDir", config.outputDir, error)) {
        return false;
    }
    if (!scene.empty()) {
        config.scene = std::move(scene);
    }
    if (!presentMode.empty() && !tryParseBenchmarkPresentMode(presentMode, config.presentMode)) {
        error = std::format("'presentMode' must be immediate, mailbox, fifo or fifo_relaxed (got '{}')", presentMode);
        return false;
    }
    if (config.sampleFrames == 0) {
        error = "'sampleFrames' must be positive";
        return false;
    }
    if (config.fixedDeltaSeconds < 0.0f) {
        error = "'fixedDeltaSeconds' must be >= 0";
        return false;
    }
    if (config.outputDir.empty()) {
        error = "'outputDir' must not be empty";
        return false;
    }

    if (json.contains("resolution")) {
        const auto& resolution = json.at("resolution");
        if (!resolution.is_object() || !rejectUnknownKeys(resolution, {"width", "height"}, "resolution", error)) {
            if (error.empty()) {
                error = "'resolution' must be {\"width\": w, \"height\": h}";
            }
            return false;
        }
        if (!readNumber(resolution, "width", config.width, error) || !readNumber(resolution, "height", config.height, error)) {
            error = "resolution." + error;
            return false;
        }
        if (config.width == 0 || config.height == 0) {
            error = "resolution width and height must be positive";
            return false;
        }
    }

    if (json.contains("cameraPath") && !parseCameraPath(json.at("cameraPath"), config.cameraPath, error)) {
        return false;
    }

    out = std::move(config);
    return true;
}

bool loadBenchmarkConfig(const std::string& path, BenchmarkConfig& out, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        error = std::format("cannot open benchmark config '{}'", path);
        return false;
    }
    nlohmann::json json = nlohmann::json::parse(stream, nullptr, false, /*ignore_comments=*/true);
    if (json.is_discarded()) {
        error = std::format("benchmark config '{}' is not valid JSON", path);
        return false;
    }
    out.sourcePath = path;
    if (!parseBenchmarkConfig(json, out, error)) {
        error = std::format("{}: {}", path, error);
        return false;
    }
    if (out.name.empty()) {
        out.name = std::filesystem::path(path).stem().string();
    }
    return true;
}

} // namespace ya

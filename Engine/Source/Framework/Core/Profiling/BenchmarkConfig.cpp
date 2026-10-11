#include "Core/Profiling/BenchmarkConfig.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ya::profiling
{

bool rejectUnknownBenchmarkKeys(const nlohmann::json&           object,
                                std::span<const std::string_view> known,
                                std::string_view                  where,
                                std::string&                      error)
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

bool readBenchmarkString(const nlohmann::json& object, const char* key, std::string& out, std::string& error)
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

bool readBenchmarkBool(const nlohmann::json& object, const char* key, bool& out, std::string& error)
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

bool parseBenchmarkCommonConfig(const nlohmann::json&                    json,
                                std::initializer_list<std::string_view> extensionKeys,
                                FBenchmarkCommonConfig&                 config,
                                std::string&                            error)
{
    if (!json.is_object()) {
        error = "benchmark config must be a JSON object";
        return false;
    }

    std::vector<std::string_view> known(std::begin(kBenchmarkCommonKeys), std::end(kBenchmarkCommonKeys));
    known.insert(known.end(), extensionKeys.begin(), extensionKeys.end());
    if (!rejectUnknownBenchmarkKeys(json, known, "benchmark config", error)) {
        return false;
    }

    std::string presentMode = config.presentMode;
    if (!readBenchmarkString(json, "name", config.name, error) || !readBenchmarkString(json, "presentMode", presentMode, error) ||
        !readBenchmarkNumber(json, "warmupFrames", config.warmupFrames, error) ||
        !readBenchmarkNumber(json, "sampleFrames", config.sampleFrames, error) ||
        !readBenchmarkString(json, "outputDir", config.outputDir, error)) {
        return false;
    }
    if (json.contains("presentMode")) {
        if (!isBenchmarkPresentModeLabel(presentMode)) {
            error = std::format("'presentMode' must be immediate, mailbox, fifo or fifo_relaxed (got '{}')", presentMode);
            return false;
        }
        config.presentMode = std::move(presentMode);
    }
    if (config.sampleFrames == 0) {
        error = "'sampleFrames' must be positive";
        return false;
    }
    if (config.outputDir.empty()) {
        error = "'outputDir' must not be empty";
        return false;
    }

    if (json.contains("resolution")) {
        const auto& resolution = json.at("resolution");
        if (!resolution.is_object() || !rejectUnknownBenchmarkKeys(resolution, {"width", "height"}, "resolution", error)) {
            if (error.empty()) {
                error = "'resolution' must be {\"width\": w, \"height\": h}";
            }
            return false;
        }
        if (!readBenchmarkNumber(resolution, "width", config.width, error) ||
            !readBenchmarkNumber(resolution, "height", config.height, error)) {
            error = "resolution." + error;
            return false;
        }
        if (config.width == 0 || config.height == 0) {
            error = "resolution width and height must be positive";
            return false;
        }
    }
    return true;
}

bool readBenchmarkJsonFile(const std::string& path, nlohmann::json& out, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        error = std::format("cannot open benchmark config '{}'", path);
        return false;
    }
    out = nlohmann::json::parse(stream, nullptr, false, /*ignore_comments=*/true);
    if (out.is_discarded()) {
        error = std::format("benchmark config '{}' is not valid JSON", path);
        return false;
    }
    return true;
}

} // namespace ya::profiling

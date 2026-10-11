#pragma once

// Host-agnostic benchmark config. Present mode is a label string so this
// header does not include RHI. A host parses its own extension keys on top.

#include "Core/Api.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace ya::profiling
{

struct FBenchmarkCommonConfig
{
    std::string sourcePath;
    std::string name;
    uint32_t    width        = 1920;
    uint32_t    height       = 1080;
    /// "immediate", "mailbox", "fifo", or "fifo_relaxed".
    std::string presentMode  = "immediate";
    uint64_t    warmupFrames = 120;
    uint64_t    sampleFrames = 600;
    std::string outputDir    = "Engine/Saved/Benchmark";
};

/// Top-level keys every host accepts. Extension keys are the host's own.
inline constexpr std::string_view kBenchmarkCommonKeys[] = {
    "name",
    "resolution",
    "presentMode",
    "warmupFrames",
    "sampleFrames",
    "outputDir",
};

[[nodiscard]] inline bool isBenchmarkPresentModeLabel(std::string_view text)
{
    return text == "immediate" || text == "mailbox" || text == "fifo" || text == "fifo_relaxed";
}

[[nodiscard]] YA_CORE_API bool rejectUnknownBenchmarkKeys(const nlohmann::json&           object,
                                                          std::span<const std::string_view> known,
                                                          std::string_view                  where,
                                                          std::string&                      error);

[[nodiscard]] inline bool rejectUnknownBenchmarkKeys(const nlohmann::json&                    object,
                                                     std::initializer_list<std::string_view> known,
                                                     std::string_view                        where,
                                                     std::string&                            error)
{
    return rejectUnknownBenchmarkKeys(object, std::span<const std::string_view>(known.begin(), known.end()), where, error);
}

[[nodiscard]] YA_CORE_API bool readBenchmarkString(const nlohmann::json& object, const char* key, std::string& out, std::string& error);
[[nodiscard]] YA_CORE_API bool readBenchmarkBool(const nlohmann::json& object, const char* key, bool& out, std::string& error);

template <typename T>
[[nodiscard]] bool readBenchmarkNumber(const nlohmann::json& object, const char* key, T& out, std::string& error)
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

/// Rejects keys outside the common set plus `extensionKeys`, then reads the
/// common fields. Extension fields are left for the caller. Missing common
/// keys keep the values already in `config`.
[[nodiscard]] YA_CORE_API bool parseBenchmarkCommonConfig(const nlohmann::json&                    json,
                                                          std::initializer_list<std::string_view> extensionKeys,
                                                          FBenchmarkCommonConfig&                 config,
                                                          std::string&                            error);

[[nodiscard]] YA_CORE_API bool readBenchmarkJsonFile(const std::string& path, nlohmann::json& out, std::string& error);

} // namespace ya::profiling

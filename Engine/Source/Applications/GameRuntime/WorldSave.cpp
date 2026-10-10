#include "GameRuntime/WorldSave.h"

#include "Core/Log.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace ya
{
namespace
{

constexpr int kWorldSaveVersion = 1;

} // namespace

FWorldSave captureWorldSave(std::string scenePath, std::string persistJson)
{
    FWorldSave save;
    save.version = kWorldSaveVersion;
    save.scene   = std::move(scenePath);
    save.persist = persistJson.empty() ? "{}" : std::move(persistJson);
    return save;
}

bool writeWorldSave(const std::string& path, const FWorldSave& save)
{
    nlohmann::json persist = nlohmann::json::object();
    try {
        persist = nlohmann::json::parse(save.persist.empty() ? "{}" : save.persist);
    }
    catch (const nlohmann::json::exception& error) {
        YA_CORE_WARN("world.save: Persist is not JSON ({}); writing an empty object", error.what());
    }
    const nlohmann::json document = {
        {"version", save.version},
        {"scene", save.scene},
        {"persist", persist},
    };

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        YA_CORE_WARN("world.save: cannot write '{}'", path);
        return false;
    }
    out << document.dump(2);
    return static_cast<bool>(out);
}

std::optional<FWorldSave> readWorldSave(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        YA_CORE_WARN("world.load: cannot read '{}'", path);
        return std::nullopt;
    }
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(in);
    }
    catch (const nlohmann::json::exception& error) {
        YA_CORE_WARN("world.load: '{}' is not JSON ({})", path, error.what());
        return std::nullopt;
    }
    if (!document.is_object() || !document.contains("version") || !document["version"].is_number_integer()) {
        YA_CORE_WARN("world.load: '{}' has no integer version", path);
        return std::nullopt;
    }
    if (document["version"].get<int>() != kWorldSaveVersion) {
        YA_CORE_WARN("world.load: '{}' version {} is not {}", path, document["version"].get<int>(), kWorldSaveVersion);
        return std::nullopt;
    }
    if (!document.contains("scene") || !document["scene"].is_string() || document["scene"].get<std::string>().empty()) {
        YA_CORE_WARN("world.load: '{}' has no scene path", path);
        return std::nullopt;
    }

    FWorldSave save;
    save.version = kWorldSaveVersion;
    save.scene   = document["scene"].get<std::string>();
    if (document.contains("persist")) {
        save.persist = document["persist"].dump();
    }
    return save;
}

} // namespace ya

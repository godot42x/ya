#include "AssetMeta.h"

#include "Core/Log.h"

#include <filesystem>
#include <fstream>
#include <functional> // std::hash

namespace ya
{

// ── Convenience accessors ───────────────────────────────────────────────────

std::string AssetMeta::getString(const std::string& key, const std::string& defaultValue) const
{
    if (properties.contains(key) && properties[key].is_string()) {
        return properties[key].get<std::string>();
    }
    return defaultValue;
}

bool AssetMeta::getBool(const std::string& key, bool defaultValue) const
{
    if (properties.contains(key) && properties[key].is_boolean()) {
        return properties[key].get<bool>();
    }
    return defaultValue;
}

int AssetMeta::getInt(const std::string& key, int defaultValue) const
{
    if (properties.contains(key) && properties[key].is_number_integer()) {
        return properties[key].get<int>();
    }
    return defaultValue;
}

float AssetMeta::getFloat(const std::string& key, float defaultValue) const
{
    if (properties.contains(key) && properties[key].is_number()) {
        return properties[key].get<float>();
    }
    return defaultValue;
}

// ── Properties hash ─────────────────────────────────────────────────────────

size_t AssetMeta::propertiesHash() const
{
    // Dump properties to a canonical (sorted-key) string and hash it.
    // nlohmann::json::dump() with sorted keys gives deterministic output.
    std::string canonical = properties.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return std::hash<std::string>{}(canonical);
}

// ── Serialization ───────────────────────────────────────────────────────────

nlohmann::json AssetMeta::toJson() const
{
    nlohmann::json j;
    j["type"]       = type;
    j["properties"] = properties;
    return j;
}

AssetMeta AssetMeta::fromJson(const nlohmann::json& j)
{
    AssetMeta meta;

    if (j.contains("type") && j["type"].is_string()) {
        meta.type = j["type"].get<std::string>();
    }

    if (j.contains("properties") && j["properties"].is_object()) {
        meta.properties = j["properties"];
    }
    else {
        // Legacy format: flat keys (e.g. {"colorSpace":"srgb","type":"texture"})
        // Migrate everything except "type" into properties.
        for (auto& [key, val] : j.items()) {
            if (key != "type") {
                meta.properties[key] = val;
            }
        }
    }

    return meta;
}

AssetMeta AssetMeta::loadFromFile(const std::string& metaPath)
{
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        YA_CORE_WARN("AssetMeta::loadFromFile - Cannot open '{}'", metaPath);
        return {};
    }

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        auto           meta = fromJson(j);
        YA_CORE_TRACE("AssetMeta: loaded '{}' (type={}, props={})", metaPath, meta.type, meta.properties.dump());
        return meta;
    }
    catch (const nlohmann::json::exception& e) {
        YA_CORE_ERROR("AssetMeta::loadFromFile - JSON parse error in '{}': {}", metaPath, e.what());
        return {};
    }
}

void AssetMeta::saveToFile(const std::string& metaPath) const
{
    // Ensure parent directory exists
    auto parent = std::filesystem::path(metaPath).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream file(metaPath);
    if (!file.is_open()) {
        YA_CORE_ERROR("AssetMeta::saveToFile - Cannot write '{}'", metaPath);
        return;
    }

    file << toJson().dump(4); // Pretty-print with 4-space indent
    YA_CORE_INFO("AssetMeta: saved '{}'", metaPath);
}

// ── Path helpers ────────────────────────────────────────────────────────────

std::string AssetMeta::metaPathFor(const std::string& assetPath)
{
    return assetPath + ".ya-meta.json";
}

bool AssetMeta::metaFileExists(const std::string& assetPath)
{
    return std::filesystem::exists(metaPathFor(assetPath));
}

// ── Defaults ────────────────────────────────────────────────────────────────

AssetMeta AssetMeta::defaultForTexture()
{
    AssetMeta meta;
    meta.type                       = "texture";
    meta.properties["colorSpace"]   = "srgb";
    meta.properties["generateMips"] = true;
    meta.properties["filter"]       = "linear";
    return meta;
}

AssetMeta AssetMeta::defaultForModel()
{
    AssetMeta meta;
    meta.type = "model";
    return meta;
}

// ── Comparison ──────────────────────────────────────────────────────────────

bool AssetMeta::operator==(const AssetMeta& other) const
{
    return type == other.type && properties == other.properties;
}

} // namespace ya

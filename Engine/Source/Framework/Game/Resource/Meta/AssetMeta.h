#pragma once

#include "Foundation/Core/Api.h"

#include <nlohmann/json.hpp>
#include <string>

namespace ya
{

/**
 * @brief Lightweight sidecar metadata for any asset file.
 *
 * Lives next to the source file as  `<asset-path>.ya-meta.json`.
 * Stores loading-time properties (colorSpace, filter, generateMips, ...)
 * that the AssetManager reads *before* it actually decodes the file.
 *
 * Design philosophy (Godot-style sidecar, NOT UE-style import):
 *   - The original file is kept as-is on disk.
 *   - The .ya-meta.json only *describes* how to load it.
 *   - Editing the meta triggers hot-reload of the affected resource.
 */
struct AssetMeta
{
    /// Asset type tag: "texture", "model", "shader", ...
    std::string type;

    /// Type-specific properties stored as free-form JSON.
    /// Examples for texture: { "colorSpace": "srgb", "generateMips": true, "filter": "linear" }
    nlohmann::json properties;

    // ── Convenience accessors ───────────────────────────────────────────

    YA_RESOURCE_API std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    YA_RESOURCE_API bool        getBool(const std::string& key, bool defaultValue = false) const;
    YA_RESOURCE_API int         getInt(const std::string& key, int defaultValue = 0) const;
    YA_RESOURCE_API float       getFloat(const std::string& key, float defaultValue = 0.0f) const;

    // ── Properties hash (for composite cache key) ───────────────────────

    /**
     * @brief Compute a stable hash of `properties` so that two metas with
     *        identical properties produce the same hash.
     */
    YA_RESOURCE_API size_t propertiesHash() const;

    // ── Serialization ───────────────────────────────────────────────────

    static YA_RESOURCE_API AssetMeta loadFromFile(const std::string& metaPath);
    YA_RESOURCE_API void             saveToFile(const std::string& metaPath) const;

    YA_RESOURCE_API nlohmann::json toJson() const;
    static YA_RESOURCE_API AssetMeta fromJson(const nlohmann::json& j);

    // ── Path helpers ────────────────────────────────────────────────────

    /** @brief Return `<assetPath>.ya-meta.json` */
    static YA_RESOURCE_API std::string metaPathFor(const std::string& assetPath);

    /** @brief Does the sidecar file exist on disk? */
    static YA_RESOURCE_API bool metaFileExists(const std::string& assetPath);

    // ── Defaults (when no sidecar is present) ───────────────────────────

    static YA_RESOURCE_API AssetMeta defaultForTexture();
    static YA_RESOURCE_API AssetMeta defaultForModel();

    // ── Comparison ──────────────────────────────────────────────────────

    YA_RESOURCE_API bool operator==(const AssetMeta& other) const;
    bool operator!=(const AssetMeta& other) const { return !(*this == other); }
};

} // namespace ya

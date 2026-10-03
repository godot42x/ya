#include "Core/Common/Tileset.h"

#include "Core/Common/AssetDocumentManager.h"
#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/System/VirtualFileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace ya
{

namespace
{

int32_t jsonInt(const nlohmann::json& j, const char* key, int32_t fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) {
        return fallback;
    }
    return static_cast<int32_t>(it->get<int64_t>());
}

} // namespace

bool Tileset::isSolidTile(int32_t tile) const
{
    return std::binary_search(solidTiles.begin(), solidTiles.end(), tile);
}

std::shared_ptr<Tileset> parseTilesetJson(const std::string& text, std::string& outError)
{
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::exception& error) {
        outError = error.what();
        return nullptr;
    }

    if (!json.is_object()) {
        outError = "tileset root must be an object";
        return nullptr;
    }

    auto tileset = std::make_shared<Tileset>();
    try {
        const auto atlasIt = json.find("atlas");
        if (atlasIt == json.end() || !atlasIt->is_object()) {
            outError = "tileset is missing the atlas TextureSlot object";
            return nullptr;
        }
        ReflectionSerializer::deserializeByRuntimeReflection(tileset->atlas, *atlasIt, "TextureSlot");
        if (!tileset->atlas.hasPath()) {
            outError = "tileset atlas has no texture path";
            return nullptr;
        }

        tileset->tileWidth  = jsonInt(json, "tileWidth", 16);
        tileset->tileHeight = jsonInt(json, "tileHeight", 16);
        tileset->margin     = jsonInt(json, "margin", 0);
        tileset->spacing    = jsonInt(json, "spacing", 0);
        tileset->columns    = jsonInt(json, "columns", 12);
        if (tileset->tileWidth <= 0 || tileset->tileHeight <= 0 || tileset->columns <= 0 ||
            tileset->margin < 0 || tileset->spacing < 0) {
            outError = "tileset tile geometry must be positive";
            return nullptr;
        }

        const auto solidIt = json.find("solid");
        if (solidIt != json.end()) {
            if (!solidIt->is_array()) {
                outError = "tileset solid must be an array of tile indices";
                return nullptr;
            }
            for (const auto& entry : *solidIt) {
                if (!entry.is_number_integer() || entry.get<int64_t>() < 0) {
                    outError = "tileset solid entries must be non-negative tile indices";
                    return nullptr;
                }
                tileset->solidTiles.push_back(static_cast<int32_t>(entry.get<int64_t>()));
            }
            std::sort(tileset->solidTiles.begin(), tileset->solidTiles.end());
            tileset->solidTiles.erase(std::unique(tileset->solidTiles.begin(), tileset->solidTiles.end()),
                                      tileset->solidTiles.end());
        }
    }
    catch (const std::exception& error) {
        outError = error.what();
        return nullptr;
    }
    return tileset;
}

namespace
{

struct TilesetDocumentTraits
{
    static std::shared_ptr<Tileset> parse(const std::string& text, std::string& error)
    {
        return parseTilesetJson(text, error);
    }

    static void logCannotRead(const std::string& path)
    {
        YA_CORE_ERROR("AssetTilesetManager: cannot read tileset '{}'", path);
    }

    static void logInvalid(const std::string& path, const std::string& error)
    {
        YA_CORE_ERROR("AssetTilesetManager: '{}' is not a valid tileset: {}", path, error);
    }
};

struct TilesetAssetRegistration
{
    TilesetAssetRegistration()
    {
        AssetTypeRegistry::registerDocument<Tileset, TilesetDocumentTraits, TilesetRef>(
            "Tileset", "Select Tileset", {".yatileset.json"});
    }
};

const TilesetAssetRegistration g_tilesetAssetRegistration;

} // namespace

} // namespace ya


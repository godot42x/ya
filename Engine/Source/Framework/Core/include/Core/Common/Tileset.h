#pragma once

#include "Core/Common/TextureSlot.h"
#include "Core/Reflection/Reflection.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

// Tile atlas description for a TilemapComponent, stored as its own asset
// file (.yatileset.json) and referenced by path.
//
// The file keeps authoring data only: which atlas image, how it is cut
// into tiles, and which tiles are solid. It never holds GPU state -- the
// embedded TextureSlot binds the atlas texture when the document is parsed,
// so a tileset whose atlas is still loading simply yields no candidates.
//
// Tile indices are row-major from the top-left of the image: tile t sits at
// (margin + col * (tileW + spacing), margin + row * (tileH + spacing)) with
// col = t % columns, row = t / columns. This matches the image-space uvRect
// convention the sprite pass already uses (v = 0 is the top row of the
// image; the shader flips the quad).
struct YA_CORE_API Tileset
{
    TextureSlot atlas;
    int32_t     tileWidth  = 16;
    int32_t     tileHeight = 16;
    int32_t     margin     = 0;
    int32_t     spacing    = 0;
    int32_t     columns    = 12;
    // Sorted tile indices that block movement. Stored sparse: a tileset
    // with no solid tiles writes an empty array.
    std::vector<int32_t> solidTiles;

    [[nodiscard]] bool isSolidTile(int32_t tile) const;
};

// Path reference to a .yatileset.json file. Only the path is serialized; the
// document is parsed synchronously on first request (tileset files are small
// authoring JSON, the same shape as .yaui.json documents, not GPU resources)
// and shared between refs naming the same file through the resource layer's
// slot. A ref with a path but no parsed tileset failed to load.
struct YA_CORE_API TilesetRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(TilesetRef, AssetRefBase)
    YA_REFLECT_END()
    YA_REFLECT_COPIES_AS_VALUE()

    // Shared slot for the path; copies share it. Null when the path is empty
    // or no resource layer can parse tilesets (reads as Failed).
    AssetHandle<Tileset> _handle;

    TilesetRef() = default;
    explicit TilesetRef(const std::string& path) : AssetRefBase(path) { rebind(); }

    Tileset*              get() const { return isLoaded() ? _handle->resource.get() : nullptr; }
    std::shared_ptr<Tileset> getShared() const { return isLoaded() ? _handle->resource : nullptr; }
    bool                  isLoaded() const { return _handle && _handle->state == EAssetSlotState::Ready; }
    EAssetResolveState    getResolveState() const
    {
        if (_path.empty()) {
            return EAssetResolveState::Empty;
        }
        if (!_handle) {
            return EAssetResolveState::Failed;
        }
        // Tileset slots fill synchronously (Ready or Failed); Loading is
        // unreachable but reads as not-yet-loaded.
        switch (_handle->state) {
        case EAssetSlotState::Ready:
            return EAssetResolveState::Ready;
        case EAssetSlotState::Loading:
        case EAssetSlotState::Failed:
            break;
        }
        return EAssetResolveState::Failed;
    }
    void rebind() override;
};

// Parses one .yatileset.json document. Returns nullptr with outError set
// when the document is malformed. The atlas member is deserialized through
// reflection, so its shape is exactly the TextureSlot shape scene files
// already use.
[[nodiscard]] YA_CORE_API std::shared_ptr<Tileset> parseTilesetJson(const std::string& text,
                                                                          std::string& outError);

} // namespace ya


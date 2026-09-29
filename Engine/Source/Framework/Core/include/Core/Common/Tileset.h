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
// atlas texture resolves through the embedded TextureSlot on the same
// per-frame path as Sprite2DComponent (see GameplayResourceBinding), so a
// tileset whose atlas is still loading simply yields no candidates.
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

// Path reference to a .yatileset.json file. Mirrors TextureRef: only the
// path is serialized, the parsed Tileset is cached beside it and loaded
// synchronously on first resolve (tileset files are small authoring JSON,
// the same shape as .yaui.json documents, not GPU resources).
struct YA_CORE_API TilesetRef : public AssetRefBase
{
    YA_REFLECT_BEGIN(TilesetRef, AssetRefBase)
    YA_REFLECT_END()

    std::shared_ptr<Tileset> _cached;
    EAssetResolveState       _resolveState = EAssetResolveState::Empty;

    TilesetRef() = default;
    explicit TilesetRef(const std::string& path) : AssetRefBase(path) {}

    TilesetRef(const TilesetRef& other)
        : AssetRefBase(other), _cached(other._cached), _resolveState(other._resolveState)
    {}

    TilesetRef& operator=(const TilesetRef& other)
    {
        if (this != &other) {
            AssetRefBase::operator=(other);
            _cached       = other._cached;
            _resolveState = other._resolveState;
        }
        return *this;
    }

    TilesetRef(TilesetRef&& other) noexcept            = default;
    TilesetRef& operator=(TilesetRef&& other) noexcept = default;

    Tileset* get() const { return _cached.get(); }
    std::shared_ptr<Tileset> getShared() const { return _cached; }
    bool isLoaded() const { return _resolveState == EAssetResolveState::Ready && _cached != nullptr; }
    EAssetResolveState getResolveState() const { return _resolveState; }
    EAssetResolveResult resolve() override;
    void invalidate() override;

    // Drops every cached Tileset. Tests use this to keep file-backed cases
    // from leaking into each other; the runtime never calls it.
    static void clearCache();
};

// Parses one .yatileset.json document. Returns nullptr with outError set
// when the document is malformed. The atlas member is deserialized through
// reflection, so its shape is exactly the TextureSlot shape scene files
// already use.
[[nodiscard]] YA_CORE_API std::shared_ptr<Tileset> parseTilesetJson(const std::string& text,
                                                                          std::string& outError);

} // namespace ya


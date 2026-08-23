#pragma once

// ============================================================================
// DynamicFontAtlas - growable single-texture glyph atlas (font-framework plan
// Module 2). Replaces the fixed-size atlas + per-missing-glyph standalone
// textures: every glyph lives in ONE texture (sprite pipeline texture-slot
// budget: TEXTURE_SET_SIZE 16), so a font consumes exactly one slot.
//
// - Shelf packing (row layout, 1px padding); when a glyph no longer fits, the
//   atlas DOUBLES and re-packs every retained slot (CPU copies are kept for
//   the re-upload). Existing glyph UVs move: callers re-read them via
//   getUv(slotIndex) after the onRepack callback fires.
// - Format is a constructor parameter: Bitmap flavor uses RGBA8 (white+alpha
//   coverage); MSDF flavor (Phase 2) will use an RGB distance field.
// - All texture creation happens in upload(), which must be called at a safe
//   frame point (never during command recording — Core Rule 6).
// ============================================================================

#include "Core/Api.h"
#include "RHI/Core/Texture.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace ya
{

class YA_RENDER_RESOURCES_API DynamicFontAtlas
{
public:
    struct FSlot
    {
        uint32_t             x = 0, y = 0, w = 0, h = 0;
        std::vector<uint8_t> pixels; // CPU copy (format = atlas format)
    };

    explicit DynamicFontAtlas(IRender& render, EFormat::T format, uint32_t initialSize = 512,
                              std::string label = "FontAtlas");
    ~DynamicFontAtlas();

    DynamicFontAtlas(const DynamicFontAtlas&) = delete;
    DynamicFontAtlas& operator=(const DynamicFontAtlas&) = delete;

    /// Append a glyph bitmap (atlas format, tightly packed rows). Triggers a
    /// grow+repack when the shelf is full; fires onRepack afterwards so
    /// callers re-read every glyph's UV. Returns the slot index.
    uint32_t addGlyph(uint32_t width, uint32_t height, const uint8_t* pixels);

    /// UV rect (offsetU, offsetV, scaleU, scaleV) for a slot.
    [[nodiscard]] glm::vec4 getUv(uint32_t slotIndex) const;
    [[nodiscard]] const FSlot& getSlot(uint32_t slotIndex) const { return _slots[slotIndex]; }
    [[nodiscard]] const std::shared_ptr<Texture>& texture() const { return _texture; }
    [[nodiscard]] EFormat::T format() const { return _format; }
    [[nodiscard]] const std::string& label() const { return _label; }
    [[nodiscard]] uint32_t size() const { return _size; }
    [[nodiscard]] bool empty() const { return _slots.empty(); }

    /// Fired after a grow+repack moved existing glyphs: callers re-read all
    /// UVs from getUv(). The texture pointer also changed (re-upload).
    void setOnRepack(std::function<void()> callback) { _onRepack = std::move(callback); }

    /// (Re)create the GPU texture from the retained CPU pixels. Must run at a
    /// safe frame point — never during command recording (Core Rule 6).
    void upload();

private:
    struct FShelf
    {
        uint32_t y = 0;
        uint32_t height = 0;
    };

    bool tryPack(uint32_t width, uint32_t height, uint32_t& outX, uint32_t& outY);
    void grow(); // double size, re-pack all slots into the CPU buffer

    IRender&                  _render;
    EFormat::T                _format;
    std::string               _label;
    uint32_t                  _size = 0;
    std::vector<uint8_t>      _cpuData;                 // RGBA/RGB bytes, _size*_size
    std::vector<FSlot>        _slots;
    std::vector<FShelf>       _shelves;
    uint32_t                  _rowHeight = 0;           // current shelf height
    std::shared_ptr<Texture>  _texture;
    std::function<void()>     _onRepack;
};

} // namespace ya
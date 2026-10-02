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
        uint32_t             codepoint = 0; // 0 = untagged (not part of a raster-size set)
        uint32_t             rasterPx  = 0;
        bool                 bLive     = true;
        std::vector<uint8_t> pixels; // CPU copy (format = atlas format)
    };

    explicit DynamicFontAtlas(IRender& render, EFormat::T format, uint32_t initialSize = 512,
                              std::string label = "FontAtlas");
    ~DynamicFontAtlas();

    DynamicFontAtlas(const DynamicFontAtlas&) = delete;
    DynamicFontAtlas& operator=(const DynamicFontAtlas&) = delete;

    /// Append a glyph bitmap (atlas format, tightly packed rows). When
    /// allowGrow() is true (default), the atlas doubles + repacks on overflow
    /// (single-atlas mode). When false (paged mode, used by FontAtlasBank),
    /// addGlyph returns ~0u once the fixed page is full so the bank can open a
    /// new page. Fires onRepack after any repack so callers re-read UVs.
    /// `codepoint` + `rasterPx` tag the slot so a shared page can drop one
    /// raster size without scanning fonts. Untagged slots (rasterPx == 0)
    /// are never released by releaseRasterSize.
    uint32_t addGlyph(uint32_t width, uint32_t height, const uint8_t* pixels,
                      uint32_t codepoint = 0, uint32_t rasterPx = 0);

    /// Mark every live slot of `rasterPx` dead. Does not repack; the bank
    /// compacts afterwards so the GPU image is replaced, not overwritten.
    size_t releaseRasterSize(uint32_t rasterPx);

    /// Drop dead slots, repack the rest into the smallest power-of-two page
    /// that fits, and replace the GPU texture (the previous image is retired
    /// through DeferredDeletionQueue). Does not fire onRepack — the bank
    /// fires once after every page has settled.
    void compactLive();

    [[nodiscard]] bool findGlyph(uint32_t codepoint, uint32_t rasterPx, uint32_t& outSlot) const;
    [[nodiscard]] size_t liveGlyphCount() const;
    [[nodiscard]] uint64_t allocatedBytes() const { return _cpuData.size(); }
    [[nodiscard]] std::vector<uint32_t> rasterSizes() const;

    /// When false, addGlyph never grows — it returns ~0u on overflow so a
    /// paged owner (FontAtlasBank) can append a fresh page (Core Rule: single
    /// atlas has a hard size ceiling; paging scales without bound).
    void setAllowGrow(bool allow) { _allowGrow = allow; }
    [[nodiscard]] bool allowGrow() const { return _allowGrow; }

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
    /// The first call creates the texture; later calls push only glyphs added
    /// since the previous upload via in-place sub-region copies.
    void upload();

    /// Hand the active texture to the DeferredDeletionQueue and clear it. Call
    /// BEFORE destroying the atlas (e.g. FontAtlasBank evicting a page) so the
    /// VkImage outlives in-flight submissions that referenced it (Core Rule 7).
    void retire();

private:
    struct FShelf
    {
        uint32_t y = 0;
        uint32_t height = 0;
    };

    bool tryPack(uint32_t width, uint32_t height, uint32_t& outX, uint32_t& outY);
    bool tryPack(uint32_t width, uint32_t height, uint32_t& outX, uint32_t& outY, uint32_t atlasSide);
    void grow(); // double size, re-pack all slots into the CPU buffer

    /// Swap in `next` as the active texture, retiring the previous one via
    /// DeferredDeletionQueue so its VkImage outlives in-flight submissions
    /// (Core Rule 7). Used on first upload, full rebuild, and repack.
    void replaceTexture(std::shared_ptr<Texture> next);

    IRender&                  _render;
    EFormat::T                _format;
    std::string               _label;
    uint32_t                  _size = 0;
    uint32_t                  _initialSize = 0;
    std::vector<uint8_t>      _cpuData;                 // RGBA/RGB bytes, _size*_size
    std::vector<FSlot>        _slots;
    std::vector<FShelf>       _shelves;
    uint32_t                  _rowHeight = 0;           // current shelf height
    std::shared_ptr<Texture>  _texture;
    std::function<void()>     _onRepack;
    bool                      _allowGrow = true;        // false => paged mode (FontAtlasBank)
    bool                      _uploaded = false;        // texture created at least once
    std::vector<FSlot>        _dirtyRects;              // glyphs added since last upload()
};

} // namespace ya
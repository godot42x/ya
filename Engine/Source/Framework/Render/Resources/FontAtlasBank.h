#pragma once

// ============================================================================
// FontAtlasBank - a paged glyph atlas for one font face (primary or a single
// fallback). Replaces "one giant growable atlas with a hard 4/8K ceiling":
// glyphs live across a list of fixed-size DynamicFontAtlas PAGES. When the
// current page is full, a new page is appended — so a font can hold
// arbitrarily many glyphs (CJK-heavy pages, long documents) without ever
// dropping a glyph.
//
// Slot encoding: a returned slot is (pageIndex << 16) | slotInPage, so callers
// store one uint32_t (Character.atlasSlot) and the bank resolves page + UV.
//
// LRU extension point: maxPages() caps the page count. When appendPage() would
// exceed it, onEvict() is invoked (default no-op). Wire onEvict() to an LRU
// policy later — evict the coldest page, drop its codepoints from the font's
// character map, and re-request them on next use. The bank already isolates
// each page's texture, so eviction only needs to retire a page + its slots.
// ============================================================================

#include "Core/Api.h"
#include "DynamicFontAtlas.h"
#include "RHI/Core/Texture.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ya
{

class YA_RENDER_RESOURCES_API FontAtlasBank
{
public:
    FontAtlasBank(IRender& render, EFormat::T format, uint32_t pageSize, std::string label,
                  bool allowPageGrow = true);
    ~FontAtlasBank();

    /// Add a glyph; returns an encoded slot, or ~0u if it cannot be placed
    /// (single glyph larger than a page even after that page grows).
    uint32_t addGlyph(uint32_t width, uint32_t height, const uint8_t* pixels);

    /// UV rect for an encoded slot (page + slot resolved internally).
    [[nodiscard]] glm::vec4 getUv(uint32_t encodedSlot) const;

    /// Live texture for an encoded slot (the page it landed on).
    [[nodiscard]] std::shared_ptr<Texture> textureForSlot(uint32_t encodedSlot) const;

    /// (Re)upload every page's GPU texture. Safe-point only (Core Rule 6).
    void upload();

    /// Number of live pages.
    [[nodiscard]] size_t pageCount() const { return _pages.size(); }
    /// Texture of a specific page (for sinks / debugging).
    [[nodiscard]] std::shared_ptr<Texture> pageTexture(size_t pageIndex) const;

    [[nodiscard]] EFormat::T format() const { return _format; }
    [[nodiscard]] const std::string& label() const { return _label; }
    [[nodiscard]] uint32_t pageSize() const { return _pageSize; }

    /// Fired after any page repacks/moves glyphs. Callers re-read all UVs.
    void setOnRepack(std::function<void()> callback) { _onRepack = std::move(callback); }

    // --- LRU extension point (reserved) -------------------------------------
    /// Max pages before onEvict() is consulted. Default: effectively unbounded.
    void setMaxPages(size_t max) { _maxPages = max; }
    [[nodiscard]] size_t maxPages() const { return _maxPages; }
    /// Hook invoked when a new page is needed but pageCount() >= maxPages().
    /// Default no-op (reserved for an LRU eviction policy).
    void setOnEvict(std::function<bool()> callback) { _onEvict = std::move(callback); }

private:
    /// Retire (deferred-destroy) the page at `index` and remove it. Used by an
    /// LRU policy: the page's texture is sent to DeferredDeletionQueue so it
    /// outlives in-flight submissions (Core Rule 7), then the atlas is dropped.
    void retirePage(size_t index);
    void appendPage();
    static constexpr uint32_t kPageBit  = 16;
    static constexpr uint32_t kSlotMask = (1u << kPageBit) - 1u;
    static uint32_t           encode(uint32_t page, uint32_t slot) { return (page << kPageBit) | (slot & kSlotMask); }

    IRender&                                    _render;
    EFormat::T                                  _format;
    std::string                                 _label;
    uint32_t                                    _pageSize;
    bool                                        _allowPageGrow;
    std::vector<std::unique_ptr<DynamicFontAtlas>> _pages;
    std::function<void()>                       _onRepack;
    size_t                                      _maxPages = static_cast<size_t>(-1); // reserved for LRU
    std::function<bool()>                       _onEvict;                            // reserved for LRU
};

} // namespace ya

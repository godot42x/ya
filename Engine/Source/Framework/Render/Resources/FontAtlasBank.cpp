#include "Render/Resources/FontAtlasBank.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"

namespace ya
{

FontAtlasBank::FontAtlasBank(IRender& render, EFormat::T format, uint32_t pageSize, std::string label,
                             bool allowPageGrow)
    : _render(render)
    , _format(format)
    , _label(std::move(label))
    , _pageSize(std::max(pageSize, 64u))
    , _allowPageGrow(allowPageGrow)
{
    // No page until the first glyph. A fallback face that is never drawn
    // must not allocate a 1024 staging buffer.
}

FontAtlasBank::~FontAtlasBank()
{
    // Defer-destroy every page's texture rather than letting unique_ptr drop the
    // VkImage immediately. A still-in-flight submission from the previous frame
    // may reference these images (Core Rule 7).
    for (auto& page : _pages) {
        page->retire();
    }
}

void FontAtlasBank::retirePage(size_t index)
{
    if (index >= _pages.size()) {
        return;
    }
    _pages[index]->retire(); // hand texture to DeferredDeletionQueue first
    _pages.erase(_pages.begin() + static_cast<ptrdiff_t>(index));
}

void FontAtlasBank::appendPage()
{
    // LRU extension point: if at capacity, ask the policy to free a page.
    // Default policy is a no-op, so paging simply grows until memory bounds.
    if (_pages.size() >= _maxPages && _onEvict && !_onEvict()) {
        YA_CORE_WARN("FontAtlasBank: {} at max pages ({}); glyph may be dropped",
                     _label, _maxPages);
        return;
    }

    auto page = std::make_unique<DynamicFontAtlas>(_render, _format, _pageSize, _label);
    page->setAllowGrow(_allowPageGrow); // paged mode: refuse to grow, return ~0u
    // Repack on any page must refresh the owning font's characters. The font
    // registers a single onRepack that re-reads all of its glyphs' UVs (the
    // bank resolves page + slot internally via getUv).
    page->setOnRepack([this]() {
        if (_onRepack) {
            _onRepack();
        }
    });
    _pages.push_back(std::move(page));
}

uint32_t FontAtlasBank::addGlyph(uint32_t width, uint32_t height, const uint8_t* pixels,
                                  uint32_t codepoint, uint32_t rasterPx)
{
    if (width == 0 || height == 0 || !pixels) {
        return static_cast<uint32_t>(-1);
    }
    if (_pages.empty()) {
        appendPage();
        if (_pages.empty()) {
            return static_cast<uint32_t>(-1);
        }
    }
    for (;;) {
        DynamicFontAtlas& page = *_pages.back();
        const uint32_t slot = page.addGlyph(width, height, pixels, codepoint, rasterPx);
        if (slot != ~0u) {
            return encode(static_cast<uint32_t>(_pages.size() - 1), slot);
        }
        // Current page full: open a new one and retry. appendPage refuses to
        // grow the page count past maxPages (LRU hook), in which case we fail.
        const size_t prevPages = _pages.size();
        appendPage();
        if (_pages.size() == prevPages) {
            return static_cast<uint32_t>(-1);
        }
    }
}

glm::vec4 FontAtlasBank::getUv(uint32_t encodedSlot) const
{
    const uint32_t page = encodedSlot >> kPageBit;
    const uint32_t slot = encodedSlot & kSlotMask;
    if (page >= _pages.size()) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    return _pages[page]->getUv(slot);
}

std::shared_ptr<Texture> FontAtlasBank::textureForSlot(uint32_t encodedSlot) const
{
    const uint32_t page = encodedSlot >> kPageBit;
    if (page >= _pages.size()) {
        return nullptr;
    }
    return _pages[page]->texture();
}

void FontAtlasBank::upload()
{
    for (auto& page : _pages) {
        page->upload();
    }
}

std::shared_ptr<Texture> FontAtlasBank::pageTexture(size_t pageIndex) const
{
    if (pageIndex >= _pages.size()) {
        return nullptr;
    }
    return _pages[pageIndex]->texture();
}

bool FontAtlasBank::findGlyph(uint32_t codepoint, uint32_t rasterPx, uint32_t& outSlot, glm::vec4& outUv) const
{
    for (uint32_t pageIndex = 0; pageIndex < static_cast<uint32_t>(_pages.size()); ++pageIndex) {
        uint32_t localSlot = 0;
        if (!_pages[pageIndex]->findGlyph(codepoint, rasterPx, localSlot)) {
            continue;
        }
        outSlot = encode(pageIndex, localSlot);
        outUv   = _pages[pageIndex]->getUv(localSlot);
        return true;
    }
    return false;
}

size_t FontAtlasBank::releaseRasterSize(uint32_t rasterPx)
{
    size_t released = 0;
    for (auto& page : _pages) {
        released += page->releaseRasterSize(rasterPx);
    }
    if (released == 0) {
        return 0;
    }
    for (size_t index = _pages.size(); index-- > 0;) {
        if (_pages[index]->liveGlyphCount() == 0) {
            _pages[index]->retire();
            _pages.erase(_pages.begin() + static_cast<ptrdiff_t>(index));
            continue;
        }
        _pages[index]->compactLive();
    }
    if (_onRepack) {
        _onRepack();
    }
    return released;
}

size_t FontAtlasBank::liveGlyphCount() const
{
    size_t count = 0;
    for (const auto& page : _pages) {
        count += page->liveGlyphCount();
    }
    return count;
}

uint64_t FontAtlasBank::allocatedBytes() const
{
    uint64_t bytes = 0;
    for (const auto& page : _pages) {
        bytes += page->allocatedBytes();
    }
    return bytes;
}

} // namespace ya

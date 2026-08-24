#include "DynamicFontAtlas.h"

#include "Core/Common/DeferredDeletionQueue.h"
#include "Core/Log.h"

#include <algorithm>
#include <cstring>

namespace ya
{

namespace
{
// Transparent border baked on EACH side of the glyph. Bitmap/coverage glyphs
// are sampled with NEAREST filtering so strokes snap to integer pixels and
// stay crisp at small sizes; the border guarantees the nearest sample point at
// a stroke edge never falls into a neighboring glyph.
constexpr uint32_t kGlyphPadding = 1;   // px of transparent border on EACH side of a glyph
constexpr uint32_t kMaxAtlasSize = 4096; // hard ceiling; a single glyph larger than this cannot be packed
} // namespace

DynamicFontAtlas::DynamicFontAtlas(IRender& render, EFormat::T format, uint32_t initialSize,
                                   std::string label)
    : _render(render)
    , _format(format)
    , _label(std::move(label))
    , _size(std::max(initialSize, 64u))
    , _cpuData(static_cast<size_t>(_size) * _size * 4, 0) // RGBA8 staging for Bitmap flavor
{
}

DynamicFontAtlas::~DynamicFontAtlas() = default;

bool DynamicFontAtlas::tryPack(uint32_t width, uint32_t height, uint32_t& outX, uint32_t& outY)
{
    // Allocate width+2*pad x height+2*pad in the atlas, but report the CONTENT
    // origin (shifted in by kGlyphPadding on every side) so callers place pixels
    // without worrying about the transparent border.
    const uint32_t occupyW = width + 2 * kGlyphPadding;
    const uint32_t occupyH = height + 2 * kGlyphPadding;

    if (_shelves.empty()) {
        return false;
    }
    FShelf& shelf = _shelves.back();
    if (shelf.height >= occupyH && _rowHeight + occupyW <= _size) {
        outX = _rowHeight + kGlyphPadding;
        outY = shelf.y + kGlyphPadding;
        _rowHeight += occupyW;
        return true;
    }
    // New shelf below the current one.
    const uint32_t nextY = shelf.y + shelf.height;
    if (nextY + occupyH > _size) {
        return false;
    }
    _shelves.push_back(FShelf{.y = nextY, .height = occupyH});
    outX = kGlyphPadding;
    outY = nextY + kGlyphPadding;
    _rowHeight = occupyW;
    return true;
}

void DynamicFontAtlas::grow()
{
    // Double repeatedly until every retained slot packs (or we hit the hard
    // ceiling). A single slot larger than kMaxAtlasSize can never fit and is
    // dropped loudly; everything else must be retained — silently dropping a
    // retained slot corrupts already-cached glyph UVs.
    uint32_t newSize = _size;
    for (;;) {
        newSize *= 2;
        if (newSize > kMaxAtlasSize) {
            YA_CORE_ERROR("DynamicFontAtlas: {} reached max size {}x{}; a glyph is too large to pack",
                          _label, kMaxAtlasSize, kMaxAtlasSize);
            return;
        }

        std::vector<uint8_t> newData(static_cast<size_t>(newSize) * newSize * 4, 0);
        _shelves.clear();
        _shelves.push_back(FShelf{.y = 0, .height = 0});
        _rowHeight = 0;

        const size_t bytesPerPixel = 4;
        bool allFit = true;
        for (FSlot& slot : _slots) {
            uint32_t x = 0, y = 0;
            if (!tryPack(slot.w, slot.h, x, y)) {
                allFit = false;
                break;
            }
            slot.x = x;
            slot.y = y;
            for (uint32_t row = 0; row < slot.h; ++row) {
                std::memcpy(newData.data() + (static_cast<size_t>(y + row) * newSize + x) * bytesPerPixel,
                            slot.pixels.data() + static_cast<size_t>(row) * slot.w * bytesPerPixel,
                            static_cast<size_t>(slot.w) * bytesPerPixel);
            }
        }

        if (!allFit) {
            continue; // try a larger atlas
        }

        _size = newSize;
        _cpuData.swap(newData);
        // grow() re-lays-out every slot and changes the texture size: force a
        // full recreate on the next upload() (incremental update is invalid).
        _uploaded = false;
        _dirtyRects.clear();
        upload();
        if (_onRepack) {
            _onRepack();
        }
        return;
    }
}

uint32_t DynamicFontAtlas::addGlyph(uint32_t width, uint32_t height, const uint8_t* inPixels)
{
    if (width == 0 || height == 0 || !inPixels) {
        return static_cast<uint32_t>(-1);
    }
    uint32_t x = 0, y = 0;
    if (!tryPack(width, height, x, y)) {
        if (!_allowGrow) {
            return static_cast<uint32_t>(-1); // paged mode: let the bank open a new page
        }
        while (!tryPack(width, height, x, y)) {
            // grow() doubles and re-packs all retained slots; if it hit the
            // ceiling it returns without growing, so stop here.
            const uint32_t prevSize = _size;
            grow();
            if (_size == prevSize) {
                YA_CORE_ERROR("DynamicFontAtlas: cannot pack glyph {}x{} (atlas at max size)", width, height);
                return static_cast<uint32_t>(-1);
            }
        }
    }

    const size_t bytesPerPixel = 4;
    FSlot slot;
    slot.x = x;
    slot.y = y;
    slot.w = width;
    slot.h = height;
    slot.pixels.assign(inPixels, inPixels + static_cast<size_t>(width) * height * bytesPerPixel);

    // Write into the CPU staging buffer (upload happens later at a safe point).
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(_cpuData.data() + (static_cast<size_t>(y + row) * _size + x) * bytesPerPixel,
                    slot.pixels.data() + static_cast<size_t>(row) * width * bytesPerPixel,
                    static_cast<size_t>(width) * bytesPerPixel);
    }

    const uint32_t index = static_cast<uint32_t>(_slots.size());
    _dirtyRects.push_back(slot); // pending in-place upload (if texture exists)
    _slots.push_back(std::move(slot));
    return index;
}

glm::vec4 DynamicFontAtlas::getUv(uint32_t slotIndex) const
{
    if (slotIndex >= _slots.size()) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    const FSlot& slot = _slots[slotIndex];
    return {
        static_cast<float>(slot.x) / static_cast<float>(_size),
        static_cast<float>(slot.y) / static_cast<float>(_size),
        static_cast<float>(slot.w) / static_cast<float>(_size),
        static_cast<float>(slot.h) / static_cast<float>(_size),
    };
}

/// Swap in a freshly built texture, retiring the old one so its VkImage stays
/// alive until every in-flight submission that referenced it has completed
/// (Core Rule 7). Without this, grow/repack/full-reupload would replace the
/// shared_ptr and immediately destroy the VkImage while a previous frame's
/// command buffer is still reading it -> VK_ERROR_DEVICE_LOST.
void DynamicFontAtlas::replaceTexture(std::shared_ptr<Texture> next)
{
    // Declare the sampler category ONCE at creation (not per-frame). The label
    // prefix encodes the flavor chosen by FontManager: SDF atlases want linear
    // clamp (lerp the distance field); bitmap/coverage atlases want nearest
    // clamp (snap texels, keep 1px strokes crisp). QuadRender reads this field
    // instead of string-matching the label on every glyph draw.
    if (_label.starts_with("SDFFontAtlas_")) {
        next->setSamplerCategory(ESamplerCategory::ClampLinear);
    } else if (_label.starts_with("FontAtlas_") || _label.starts_with("FontGlyph_")) {
        next->setSamplerCategory(ESamplerCategory::ClampNearest);
    }
    std::shared_ptr<Texture> old = std::move(_texture);
    _texture = std::move(next);
    DeferredDeletionQueue::get().retire(std::move(old));
}

void DynamicFontAtlas::upload()
{
    if (!_uploaded) {
        // First upload: create the texture from the whole CPU buffer.
        replaceTexture(Texture::fromData(_render,
                                         _size,
                                         _size,
                                         _cpuData.data(),
                                         _cpuData.size(),
                                         _format,
                                         _label));
        _uploaded = true;
        _dirtyRects.clear();
        return;
    }

    if (!_texture) {
        // Texture was never created (e.g. grew before first upload) — recreate.
        replaceTexture(Texture::fromData(_render,
                                         _size,
                                         _size,
                                         _cpuData.data(),
                                         _cpuData.size(),
                                         _format,
                                         _label));
        _uploaded = true;
        _dirtyRects.clear();
        return;
    }

    // Incremental: push only the glyphs added since the last upload via
    // in-place sub-region copies (no full re-upload, no image recreation).
    // Batch ALL dirty glyphs into ONE staging buffer + ONE submit: each slot's
    // tightly-packed pixels are concatenated, and its region points at the
    // offset via bufferOffset. This avoids one staging allocation and one
    // isolate submit per glyph (N glyphs -> 1 submit).
    std::vector<uint8_t>          staging;
    std::vector<Texture::RegionUpdate> batch;
    batch.reserve(_dirtyRects.size());
    for (const FSlot& slot : _dirtyRects) {
        if (slot.w == 0 || slot.h == 0) {
            continue;
        }
        const size_t offset = staging.size();
        staging.insert(staging.end(), slot.pixels.begin(), slot.pixels.end());
        batch.push_back(Texture::RegionUpdate{
            .x              = slot.x,
            .y              = slot.y,
            .w              = slot.w,
            .h              = slot.h,
            .offset         = offset,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });
    }
    if (!batch.empty()) {
        if (!_texture->updateRegions(_render, staging.data(), batch)) {
            YA_CORE_WARN("DynamicFontAtlas: batched updateRegions failed; falling back to full reupload");
            replaceTexture(Texture::fromData(_render, _size, _size, _cpuData.data(), _cpuData.size(), _format, _label));
            _uploaded = true;
        }
    }
    _dirtyRects.clear();
}

void DynamicFontAtlas::retire()
{
    // Drop the active texture into the deferred queue (instead of destroying it
    // here) so any in-flight submission still reading this VkImage survives
    // until the GPU is idle (Core Rule 7). After this the atlas is GPU-empty;
    // addGlyph + upload() can rebuild it later if needed.
    DeferredDeletionQueue::get().retire(std::move(_texture));
    _texture.reset();
    _uploaded = false;
}

} // namespace ya

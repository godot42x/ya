#include "DynamicFontAtlas.h"

#include "Core/Log.h"

#include <algorithm>
#include <cstring>

namespace ya
{

namespace
{
constexpr uint32_t kGlyphPadding = 1; // px between glyphs (avoid bleed with linear sampling)
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
    const uint32_t paddedW = width + kGlyphPadding;
    const uint32_t paddedH = height + kGlyphPadding;

    if (_shelves.empty()) {
        return false;
    }
    FShelf& shelf = _shelves.back();
    if (shelf.height >= paddedH && _rowHeight + paddedW <= _size) {
        outX = _rowHeight;
        outY = shelf.y;
        _rowHeight += paddedW;
        return true;
    }
    // New shelf below the current one.
    const uint32_t nextY = shelf.y + shelf.height;
    if (nextY + paddedH > _size) {
        return false;
    }
    _shelves.push_back(FShelf{.y = nextY, .height = paddedH});
    outX = 0;
    outY = nextY;
    _rowHeight = paddedW;
    return true;
}

void DynamicFontAtlas::grow()
{
    const uint32_t newSize = _size * 2;
    std::vector<uint8_t> newData(static_cast<size_t>(newSize) * newSize * 4, 0);

    // Re-pack all retained slots into the new buffer (shelf layout from scratch).
    _shelves.clear();
    _shelves.push_back(FShelf{.y = 0, .height = 0});
    _rowHeight = 0;

    for (FSlot& slot : _slots) {
        uint32_t x = 0, y = 0;
        if (!tryPack(slot.w, slot.h, x, y)) {
            YA_CORE_ERROR("DynamicFontAtlas: grow({}x{}) still cannot pack slot {}x{}", newSize, newSize, slot.w, slot.h);
            continue;
        }
        slot.x = x;
        slot.y = y;
        const size_t bytesPerPixel = 4;
        for (uint32_t row = 0; row < slot.h; ++row) {
            std::memcpy(newData.data() + (static_cast<size_t>(y + row) * newSize + x) * bytesPerPixel,
                        slot.pixels.data() + static_cast<size_t>(row) * slot.w * bytesPerPixel,
                        static_cast<size_t>(slot.w) * bytesPerPixel);
        }
    }

    _size = newSize;
    _cpuData.swap(newData);
    upload();
}

uint32_t DynamicFontAtlas::addGlyph(uint32_t width, uint32_t height, const uint8_t* pixels)
{
    if (width == 0 || height == 0 || !pixels) {
        return static_cast<uint32_t>(-1);
    }
    uint32_t x = 0, y = 0;
    if (!tryPack(width, height, x, y)) {
        grow();
        if (!tryPack(width, height, x, y)) {
            YA_CORE_ERROR("DynamicFontAtlas: cannot pack glyph {}x{} even after grow", width, height);
            return static_cast<uint32_t>(-1);
        }
    }

    const size_t bytesPerPixel = 4;
    FSlot slot;
    slot.x = x;
    slot.y = y;
    slot.w = width;
    slot.h = height;
    slot.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * bytesPerPixel);

    // Write into the CPU staging buffer (upload happens later at a safe point).
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(_cpuData.data() + (static_cast<size_t>(y + row) * _size + x) * bytesPerPixel,
                    slot.pixels.data() + static_cast<size_t>(row) * width * bytesPerPixel,
                    static_cast<size_t>(width) * bytesPerPixel);
    }

    const uint32_t index = static_cast<uint32_t>(_slots.size());
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

void DynamicFontAtlas::upload()
{
    _texture = Texture::fromData(_render,
                                 _size,
                                 _size,
                                 _cpuData.data(),
                                 _cpuData.size(),
                                 _format,
                                 _label);
}

} // namespace ya
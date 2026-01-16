#include "FontManager.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/VirtualFileSystem.h"
#include "freetype/freetype.h"

#include "Core/AssetManager.h"

namespace ya
{

FontManager *FontManager::get()
{
    static FontManager instance;
    return &instance;
}

std::shared_ptr<Font> FontManager::getFont(const FName &fontName, uint32_t fontSize)
{
    std::string cacheKey = makeCacheKey(fontName, fontSize);

    // Check if already loaded
    auto it = _fontCache.find(cacheKey);
    if (it != _fontCache.end()) {
        return it->second;
    }

    // Not in cache - need to load
    // Note: You'll need to store fontPath somewhere or pass it here
    YA_CORE_WARN("Font '{}' size {} not in cache. Call loadFont first.", fontName.toString(), fontSize);
    return nullptr;
}

void FontManager::unloadFont(const FName &fontName, uint32_t fontSize)
{
    std::string cacheKey = makeCacheKey(fontName, fontSize);
    auto        it       = _fontCache.find(cacheKey);
    if (it != _fontCache.end()) {
        _fontCache.erase(it);
        YA_CORE_INFO("Unloaded font '{}' size {}", fontName.toString(), fontSize);
    }
}

void FontManager::clearCache()
{
    _fontCache.clear();
    YA_CORE_INFO("Cleared all font cache");
}

std::shared_ptr<Font> FontManager::loadFont(const std::string &fontPath, const FName &fontName, uint32_t fontSize)
{
    YA_PROFILE_FUNCTION_LOG();
    FT_Library ft{};
    if (FT_Err_Ok != FT_Init_FreeType(&ft)) {
        YA_CORE_ERROR("Failed to initialize FreeType library");
        return nullptr;
    }
    // VirtualFileSystem::get().get
    // FT_New_Face()

    FT_Face face{};
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        YA_CORE_ERROR("Failed to load font: {}", fontPath);
        FT_Done_FreeType(ft);
        return nullptr;
    }

    FT_Set_Pixel_Sizes(face, 0, fontSize);

    auto font        = std::make_shared<Font>();
    font->fontSize   = (float)fontSize;
    font->fontPath   = fontPath;
    font->lineHeight = (float)(face->size->metrics.height >> 6);   // 26.6 fixed point to integer
    font->ascent     = (float)(face->size->metrics.ascender >> 6);  // Distance from baseline to top
    font->descent    = (float)(face->size->metrics.descender >> 6); // Distance from baseline to bottom (negative)

    // First pass: calculate max glyph dimensions
    uint32_t maxGlyphWidth  = 0;
    uint32_t maxGlyphHeight = 0;

    for (unsigned char c = 32; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            continue;
        }
        FT_GlyphSlot &glyph = face->glyph;
        maxGlyphWidth       = std::max(maxGlyphWidth, glyph->bitmap.width);
        maxGlyphHeight      = std::max(maxGlyphHeight, glyph->bitmap.rows);
    }


    // Choose atlas width - aim for ~16 glyphs per row
    // For 96 printable ASCII chars (32-127), this gives us ~6 rows
    constexpr uint32_t glyphsPerRow = 16;
    uint32_t           atlasWidth   = glyphsPerRow * (maxGlyphWidth + 2); // +2 for padding per glyph

    // Calculate required height
    constexpr uint32_t totalGlyphs = 96;                                              // ASCII 32-127
    uint32_t           numRows     = (totalGlyphs + glyphsPerRow - 1) / glyphsPerRow; // Ceiling division
    uint32_t           atlasHeight = numRows * (maxGlyphHeight + 2);                  // +2 for padding

    // 将尺寸向上取整到2的幂次方，以提高GPU兼容性和性能
    // 例如：300 -> 512, 100 -> 128
    auto nextPow2 = [](uint32_t v) -> uint32_t {
        v--;          // 减1，避免本身就是2的幂时被翻倍
        v |= v >> 1;  // 将最高位的1向右扩散
        v |= v >> 2;  // 继续扩散，填充所有低位为1
        v |= v >> 4;  // 例如：00100000 -> 00111111
        v |= v >> 8;  // 通过按位或运算逐步填充
        v |= v >> 16; // 最终得到全1的低位
        v++;          // 加1后得到下一个2的幂次方
        return v;
    };

    atlasWidth  = nextPow2(atlasWidth);  // 将宽度调整为2的幂次方
    atlasHeight = nextPow2(atlasHeight); // 将高度调整为2的幂次方

    YA_CORE_INFO("Font atlas dimensions of {}: {}x{} (maxGlyph={}x{}), fontSize: {}",
                 fontName.toString(),
                 atlasWidth,
                 atlasHeight,
                 maxGlyphWidth,
                 maxGlyphHeight,
                 fontSize);

    // Create atlas pixel data (RGBA)
    std::vector<ColorU8_t> atlasData(static_cast<size_t>(atlasWidth * atlasHeight),
                                     ColorU8_t{.r = 0, .g = 0, .b = 0, .a = 0});

    // Second pass: pack glyphs into atlas using simple row-based packing
    uint32_t penX      = 1; // Start with 1px padding
    uint32_t penY      = 1;
    uint32_t rowHeight = 0;

    for (unsigned char c = 32; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            YA_CORE_WARN("Failed to load glyph '{}'", c);
            continue;
        }

        FT_GlyphSlot &glyph  = face->glyph;
        FT_Bitmap    &bitmap = glyph->bitmap;

        // Check if we need to move to next row
        if (penX + bitmap.width + 1 > atlasWidth) {
            penX = 1;
            penY += rowHeight + 1; // Move to next row with padding
            rowHeight = 0;
        }

        // Check if we've run out of vertical space
        if (penY + bitmap.rows > atlasHeight) {
            YA_CORE_ERROR("Font atlas too small! Need to increase atlas size.");
            break;
        }

        // Copy glyph bitmap to atlas at (offsetX, offsetY)
        for (uint32_t row = 0; row < bitmap.rows; row++) {
            for (uint32_t col = 0; col < bitmap.width; col++) {
                uint8_t gray      = bitmap.buffer[row * bitmap.width + col];
                size_t  dstIdx    = ((penY + row) * atlasWidth) + (penX + col);
                atlasData[dstIdx] = ColorRGBA<uint8_t>{.r = 255, .g = 255, .b = 255, .a = gray};
            }
        }

        // Calculate UV coordinates (offset + scale format for drawSubTexture)
        float uOffset = static_cast<float>(penX) / static_cast<float>(atlasWidth);
        float vOffset = static_cast<float>(penY) / static_cast<float>(atlasHeight);
        float uScale  = static_cast<float>(bitmap.width) / static_cast<float>(atlasWidth);
        float vScale  = static_cast<float>(bitmap.rows) / static_cast<float>(atlasHeight);

        Character character{
            .uvRect  = glm::vec4(uOffset, vOffset, uScale, vScale),
            .size    = glm::ivec2(bitmap.width, bitmap.rows),
            .bearing = glm::ivec2(glyph->bitmap_left, glyph->bitmap_top),
            .advance = glm::vec2(static_cast<float>(glyph->advance.x) / 64.0f,
                                 static_cast<float>(glyph->advance.y) / 64.0f),
        };

        // 'e' bitmap_left=3, bitmap_top=27
        // 'H' bitmap_left=4, bitmap_top=35, bitmap.size=21x35

        font->characters[static_cast<char>(c)] = character;

        // Update row tracking
        rowHeight = std::max(rowHeight, bitmap.rows);
        penX += bitmap.width + 1; // +1 for padding
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Create atlas texture
    font->atlasTexture = makeShared<Texture>(atlasWidth, atlasHeight, atlasData);
    font->atlasTexture->setLabel(std::format("FontAtlas_{}", fontName.toString()));
    AssetManager::get()->registerTexture(std::format("FontAtlas_{}:{}", fontName.toString(), fontSize), font->atlasTexture);

    // Cache the loaded font
    std::string cacheKey = makeCacheKey(fontName, fontSize);
    _fontCache[cacheKey] = font;

    YA_CORE_INFO("Loaded font '{}' (size: {}, atlas: {}x{})", fontName.toString(), fontSize, atlasWidth, atlasHeight);
    YA_CORE_INFO("Memory used for font atlas: {:.2f} KB", ((float)atlasWidth * atlasHeight * sizeof(ColorU8_t)) / 1024.0f);

    return font;
}

std::shared_ptr<Font> FontManager::getAdaptiveFont(const std::string &fontPath,
                                                   const FName       &fontName,
                                                   uint32_t           baseSize,
                                                   uint32_t           windowHeight,
                                                   uint32_t           referenceHeight)
{
    // Calculate adapted font size based on window height
    float    scale       = static_cast<float>(windowHeight) / static_cast<float>(referenceHeight);
    uint32_t adaptedSize = static_cast<uint32_t>(static_cast<float>(baseSize) * scale);

    // Clamp to reasonable range
    adaptedSize = std::clamp(adaptedSize, 8u, 256u);

    // Try to get from cache first
    auto font = getFont(fontName, adaptedSize);
    if (font) {
        return font;
    }

    // Not in cache, load it
    return loadFont(fontPath, fontName, adaptedSize);
}

} // namespace ya
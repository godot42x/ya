#include "BitmapFontRasterizer.h"

#include "Core/Log.h"
#include "freetype/freetype.h"

#include <cstdlib>

namespace ya
{

GlyphBitmap BitmapFontRasterizer::rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize)
{
    GlyphBitmap out;

    if (FT_Set_Pixel_Sizes(face, 0, pixelSize)) {
        return out;
    }
    // Native TrueType bytecode in Apple CJK faces is authored for Core Text.
    // FreeType executing it at 12ppem snaps Hiragino `4`'s crossbar off the
    // pixel grid. FORCE_AUTOHINT follows the outline; NO_BITMAP ignores sbit
    // strikes that exist only at exact ppem and would make sizes inconsistent.
    // TARGET_NORMAL (the FT_LOAD_RENDER default) keeps more ink at 9–12px
    // than TARGET_LIGHT or NO_HINTING: Inter 'l' at 9px is a 1px stem
    // (coverage sum 1428) under these flags, and 2px wide with light or no
    // hint; Hiragino 中/国 at 9px cover 5174/8456 versus ~4k/6.4k without.
    // Do not switch the target.
    if (FT_Load_Char(face,
                     static_cast<FT_ULong>(codepoint),
                     FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_FORCE_AUTOHINT)) {
        return out;
    }

    const FT_GlyphSlot glyph = face->glyph;
    const FT_Bitmap&   bitmap = glyph->bitmap;
    if (bitmap.width == 0 || bitmap.rows == 0) {
        // Whitespace / .notdef: keep metrics only (advance/bearing still valid).
        out.width  = 0;
        out.height = 0;
        out.bearing = {glyph->bitmap_left, glyph->bitmap_top};
        out.advance = {static_cast<float>(glyph->advance.x) / 64.0f,
                       static_cast<float>(glyph->advance.y) / 64.0f};
        return out;
    }

    out.width   = bitmap.width;
    out.height  = bitmap.rows;
    out.bearing = {glyph->bitmap_left, glyph->bitmap_top};
    out.advance = {static_cast<float>(glyph->advance.x) / 64.0f,
                   static_cast<float>(glyph->advance.y) / 64.0f};

    // FreeType grayscale coverage -> RGBA8 (white + alpha), matching the
    // legacy atlas encoding so sampling/alpha behavior is unchanged.
    //
    // Respect bitmap.pitch: FreeType pads gray rows and may use negative
    // pitch for bottom-up bitmaps. Striding by bitmap.width caused torn glyphs.
    out.pixels.resize(static_cast<size_t>(bitmap.width) * bitmap.rows * 4);
    const int32_t srcPitch   = bitmap.pitch;
    const uint32_t rowStride = static_cast<uint32_t>(std::abs(srcPitch));
    (void)rowStride;
    const uint8_t* srcBase   = srcPitch >= 0 ? bitmap.buffer
                                               : bitmap.buffer + (bitmap.rows - 1) * srcPitch;
    for (uint32_t row = 0; row < bitmap.rows; ++row) {
        const uint8_t* srcRow = srcBase + static_cast<int32_t>(row) * srcPitch;
        for (uint32_t col = 0; col < bitmap.width; ++col) {
            const uint8_t gray = srcRow[col];
            uint8_t* dst = out.pixels.data() +
                           (static_cast<size_t>(row) * bitmap.width + col) * 4;
            dst[0] = 255;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = gray;
        }
    }
    return out;
}

} // namespace ya

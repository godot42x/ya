#include "BitmapFontRasterizer.h"

#include "Core/Log.h"
#include "freetype/freetype.h"

namespace ya
{

GlyphBitmap BitmapFontRasterizer::rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize)
{
    GlyphBitmap out;

    if (FT_Set_Pixel_Sizes(face, 0, pixelSize)) {
        return out;
    }
    if (FT_Load_Char(face, static_cast<FT_ULong>(codepoint), FT_LOAD_RENDER)) {
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
    out.pixels.resize(static_cast<size_t>(bitmap.width) * bitmap.rows * 4);
    for (uint32_t row = 0; row < bitmap.rows; ++row) {
        for (uint32_t col = 0; col < bitmap.width; ++col) {
            const uint8_t gray = bitmap.buffer[static_cast<size_t>(row) * bitmap.width + col];
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
#include "SDFFontRasterizer.h"

#include "Core/Log.h"
#include "freetype/freetype.h"

namespace ya
{

SDFFontRasterizer::SDFFontRasterizer() = default;

GlyphBitmap SDFFontRasterizer::rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize)
{
    GlyphBitmap out;

    if (FT_Set_Pixel_Sizes(face, 0, pixelSize)) {
        return out;
    }
    // Load the OUTLINE (no bitmap, no hinting — SDF is scale-free and must
    // not be hinted at a fixed size) and render it as a distance field.
    if (FT_Load_Char(face, static_cast<FT_ULong>(codepoint), FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING)) {
        return out;
    }
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF)) {
        return out;
    }

    const FT_GlyphSlot glyph = face->glyph;
    const FT_Bitmap&   bitmap = glyph->bitmap;
    if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
        // Whitespace: metrics only.
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

    // FreeType SDF bitmap: 8-bit distance, 128 = boundary, 0..255.
    // Store into RGBA8 with the distance in R (G/B/A = 255); the shader
    // reads .r and converts to coverage (smoothstep around the boundary).
    out.pixels.resize(static_cast<size_t>(bitmap.width) * bitmap.rows * 4);
    for (uint32_t row = 0; row < bitmap.rows; ++row) {
        for (uint32_t col = 0; col < bitmap.width; ++col) {
            const uint8_t dist = bitmap.buffer[static_cast<size_t>(row) * bitmap.width + col];
            uint8_t* dst = out.pixels.data() +
                           (static_cast<size_t>(row) * bitmap.width + col) * 4;
            dst[0] = dist;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = 255;
        }
    }
    return out;
}

} // namespace ya
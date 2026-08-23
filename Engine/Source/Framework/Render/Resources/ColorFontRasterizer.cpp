#include "ColorFontRasterizer.h"

#include "Core/Log.h"
#include "freetype/freetype.h"
#include "freetype/ftcolor.h"

namespace ya
{

GlyphBitmap ColorFontRasterizer::rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize)
{
    GlyphBitmap out;

    if (FT_Set_Pixel_Sizes(face, 0, pixelSize)) {
        return out;
    }
    // FT_LOAD_COLOR requests the color bitmap (CBDT/COLR); without it color
    // fonts may fall back to a monochrome outline.
    if (FT_Load_Char(face, static_cast<FT_ULong>(codepoint), FT_LOAD_RENDER | FT_LOAD_COLOR)) {
        return out;
    }

    const FT_GlyphSlot glyph = face->glyph;
    const FT_Bitmap&   bitmap = glyph->bitmap;
    if (bitmap.width == 0 || bitmap.rows == 0) {
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

    // FT_LOAD_COLOR bitmaps are BGRA (FT_PIXEL_MODE_BGRA); convert to RGBA.
    // Non-color fallback (grayscale) is converted to white+alpha like Bitmap.
    //
    // Source stride is bitmap.pitch, not bitmap.width * 4 / width: FreeType may
    // pad rows or return a negative-pitch top-down bitmap.
    out.pixels.resize(static_cast<size_t>(bitmap.width) * bitmap.rows * 4);
    const int32_t srcPitch   = bitmap.pitch;
    const uint8_t* srcBase   = srcPitch >= 0 ? bitmap.buffer
                                               : bitmap.buffer + (bitmap.rows - 1) * srcPitch;

    if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        out.bColor = true;
        for (uint32_t row = 0; row < bitmap.rows; ++row) {
            const uint8_t* srcRow = srcBase + static_cast<int32_t>(row) * srcPitch;
            for (uint32_t col = 0; col < bitmap.width; ++col) {
                const uint8_t* src = srcRow + col * 4;
                uint8_t* dst = out.pixels.data() +
                               (static_cast<size_t>(row) * bitmap.width + col) * 4;
                dst[0] = src[2]; // B -> R
                dst[1] = src[1]; // G
                dst[2] = src[0]; // R -> B
                dst[3] = src[3]; // A
            }
        }
    }
    else {
        out.bColor = false;
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
    }
    return out;
}

} // namespace ya
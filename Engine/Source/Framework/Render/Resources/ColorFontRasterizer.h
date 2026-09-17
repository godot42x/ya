#pragma once

// ============================================================================
// ColorFontRasterizer - full-color glyph rasterizer (font-framework plan
// Phase 3, emoji). Loads glyphs with FT_LOAD_COLOR (CBDT/COLRv0 -> BGRA
// bitmap) and stores them as opaque RGBA in the atlas; the draw path uses a
// WHITE tint for color glyphs (Character.bColor) so the bitmap's own colors
// show through, instead of the text-color modulation used for coverage/SDF
// glyphs.
// ============================================================================

#include "Render/Resources/IFontRasterizer.h"

namespace ya
{

class YA_RENDER_RESOURCES_API ColorFontRasterizer final : public IFontRasterizer
{
public:
    EFontRenderMode getMode() const override { return EFontRenderMode::Bitmap; } // color bitmaps are not distance fields
    EFormat::T      getAtlasFormat() const override { return EFormat::R8G8B8A8_UNORM; }

    GlyphBitmap rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize) override;
};

} // namespace ya
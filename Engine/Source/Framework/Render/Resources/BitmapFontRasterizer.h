#pragma once

// ============================================================================
// BitmapFontRasterizer - EFontRenderMode::Bitmap implementation (font-framework
// plan Module 3). Wraps FreeType grayscale coverage (autohinted, no sbit)
// into RGBA8 glyphs (white RGB + coverage alpha).
// RGBA8 glyphs (white RGB + coverage alpha), matching the legacy atlas format
// so Phase 1 stays behavior-preserving.
// ============================================================================

#include "Render/Resources/IFontRasterizer.h"

namespace ya
{

class YA_RENDER_RESOURCES_API BitmapFontRasterizer final : public IFontRasterizer
{
public:
    EFontRenderMode getMode() const override { return EFontRenderMode::Bitmap; }
    EFormat::T      getAtlasFormat() const override { return EFormat::R8G8B8A8_UNORM; }

    GlyphBitmap rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize) override;
};

} // namespace ya
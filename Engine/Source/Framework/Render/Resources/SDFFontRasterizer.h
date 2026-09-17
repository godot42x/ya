#pragma once

// ============================================================================
// SDFFontRasterizer - EFontRenderMode::SDF implementation (font-framework plan
// Phase 2, FreeType-native fallback while msdfgen is not vendored). Uses
// FreeType's FT_RENDER_MODE_SDF (2.11+): a single-channel signed distance
// field, scale-free at draw time — the blurry scaled-view bitmap path is
// replaced by a distance field sampled at ANY size.
//
// The atlas stores the distance in the R channel (RGBA8, G/B/A = 255); the
// sprite shader branches per draw (textureIdx high-bit flag) and converts the
// stored value to coverage with screen-space AA (fwidth).
//
// Upgrade path: when msdfgen becomes vendorable, MSDFFontRasterizer swaps in
// at this seam (median-of-3 sampling in the shader) without touching the
// atlas / glyph cache / draw path.
// ============================================================================

#include "Render/Resources/IFontRasterizer.h"

namespace ya
{

class YA_RENDER_RESOURCES_API SDFFontRasterizer final : public IFontRasterizer
{
public:
    SDFFontRasterizer();

    EFontRenderMode getMode() const override { return EFontRenderMode::SDF; }
    EFormat::T      getAtlasFormat() const override { return EFormat::R8G8B8A8_UNORM; }

    GlyphBitmap rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize) override;

private:
    int _spread = 8; // FreeType SDF spread property (px)
};

} // namespace ya
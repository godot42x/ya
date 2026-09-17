#pragma once

// ============================================================================
// IFontRasterizer - glyph rasterization flavor seam (font-framework plan
// Module 3). One implementation per EFontRenderMode; the FontManager only
// talks to this interface, so adding MSDF/SDF later is a new subclass + a
// shader branch, without touching the atlas / glyph cache / draw path.
// ============================================================================

#include "Core/Api.h"
#include "RHI/RenderDefines.h"

#include <cstdint>
#include <vector>

typedef struct FT_FaceRec_* FT_Face;

namespace ya
{

/// Glyph rasterization flavor (font-framework plan §1). The atlas
/// infrastructure is flavor-agnostic; the rasterizer + shader branch are the
/// only two seams per flavor.
enum class EFontRenderMode : uint8_t
{
    Bitmap = 0,  // FreeType grayscale coverage (RGBA atlas, white+alpha)
    MSDF,        // multi-channel signed distance field (Phase 2, msdfgen)
    SDF,         // single-channel distance field (FreeType FT_RENDER_MODE_SDF)
    Color,       // full-color bitmaps (emoji, FT_LOAD_COLOR -> BGRA)
};

/// A rasterized glyph: metrics + tightly packed pixels in the atlas format
/// (see IFontRasterizer::getAtlasFormat). The atlas writes these into a
/// DynamicFontAtlas slot.
struct GlyphBitmap
{
    uint32_t                width  = 0;
    uint32_t                height = 0;
    glm::ivec2              bearing;   // offset from baseline to top-left
    glm::vec2               advance;   // advance to the next glyph
    std::vector<uint8_t>    pixels;    // atlas format, row-major
    bool                    bColor = false;  // true = full-color glyph (emoji): draw with white tint
};

class YA_RENDER_RESOURCES_API IFontRasterizer
{
public:
    virtual ~IFontRasterizer() = default;

    virtual EFontRenderMode getMode() const = 0;
    /// Texture format the glyph pixels are written in. Bitmap flavor: RGBA8
    /// (white + alpha coverage); MSDF flavor (Phase 2): RGB distance field.
    virtual EFormat::T getAtlasFormat() const = 0;
    /// Rasterize `codepoint` at `pixelSize` (FreeType pixel size). Returns an
    /// empty GlyphBitmap (width==0) when the face has no glyph for it.
    virtual GlyphBitmap rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize) = 0;
};

} // namespace ya
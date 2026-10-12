#pragma once

#include "Core/Common/Types.h"
#include "Scene2D/Text2DComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace ya
{

struct WorldSpriteCandidate;
struct Font;
struct Sampler;
struct Texture;

/// Everything appendText2DCandidates needs besides the component itself.
/// `font` is the UI face whose metrics are the requested device pixel size;
/// the caller resolves it from FontManager (Render3D does not reach FontManager
/// itself). A null font, or a font with no rasterized glyph for the text yet,
/// yields no candidates: extraction never invents pixels.
///
/// Each glyph takes its own texture: a font atlas is paged, so glyphs of one
/// line can live on different pages. `resolveSampler` answers the sampler for a
/// glyph page (the atlas carries a sampler category, exactly like a sprite
/// slot), so this file does not pick a filtering policy.
struct Text2DExtractionInput
{
    const Text2DComponent*  component = nullptr;
    const Font*             font      = nullptr;
    std::function<Ptr<Sampler>(const Texture&)> resolveSampler;
    glm::mat4               world     = glm::mat4(1.0f);
    uint32_t                entityId  = 0;
    /// World height of one line, from `component->size` (passed resolved so
    /// this file needs no unit policy).
    float                   lineWorldHeight = 0.5f;
};

/// Expands one world-text component into one sprite candidate per glyph
/// (advance, bearing, line breaks). Characters the font has no rasterized
/// glyph for are skipped; the line still lays out around them. Appends to
/// `out`, so a scene with several labels accumulates.
YA_RENDER_3D_API void appendText2DCandidates(const Text2DExtractionInput&       in,
                                             std::vector<WorldSpriteCandidate>& out);

/// Whether `font` can draw at least one glyph of `text` right now. The
/// extractor uses this to skip resolving a font for text with no rasterized
/// glyphs yet (the glyph rasterizer runs a frame later).
[[nodiscard]] YA_RENDER_3D_API bool fontHasAnyGlyph(const Font& font, const std::string& text);

} // namespace ya
